// EventStore.cpp
#include "EventStore.h"
#include "dataConfig.h"

#include <QDir>
#include <QFile>
#include <QMutexLocker>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// ===== SecurityEvent =====
QString SecurityEvent::typeKey(Type t)
{
    switch (t) {
    case FaceKnown:    return "face_known";
    case FaceUnknown:  return "face_unknown";
    case LeftObject:   return "left_object";
    case LeftCleared:  return "left_cleared";
    case Intrusion:    return "intrusion";
    }
    return "unknown";
}

QString SecurityEvent::typeName(Type t)
{
    switch (t) {
    case FaceKnown:    return QStringLiteral("人员识别");
    case FaceUnknown:  return QStringLiteral("陌生人出现");
    case LeftObject:   return QStringLiteral("遗留物");
    case LeftCleared:  return QStringLiteral("遗留物解除");
    case Intrusion:    return QStringLiteral("禁区入侵");
    }
    return QStringLiteral("未知");
}

// ===== EventWorker（单线程串行写，保证 jsonl 追加顺序与文件句柄独占）=====
class EventStore::EventWorker : public QObject
{
    Q_OBJECT
public slots:
    void onEnqueue(QVector<SecurityEvent> events, QImage annotated)
    {
        const QDateTime now = QDateTime::currentDateTime();
        const QString day = now.toString("yyyy-MM-dd");
        const QString root = DataConfig::eventsRoot();
        QDir().mkpath(root);
        QDir().mkpath(root + "/" + day + "/img");

        // 截图存证：一帧多事件共享同一张标注图（判定与截图时间差 <100ms）
        QString imageRel;
        if (!annotated.isNull() && !events.isEmpty()) {
            const QString fname = now.toString("HHmmss-zzz") + "_"
                + SecurityEvent::typeKey(events.first().type) + ".jpg";
            const QString abs = root + "/" + day + "/img/" + fname;
            if (annotated.save(abs, "JPG", 85)) {
                imageRel = day + "/img/" + fname;
            } else {
                emit storeError(QStringLiteral("截图存证写入失败: %1").arg(abs));
            }
        }

        // 元数据追加（JSONL：一行一条，崩溃不损已有记录）
        QFile jsonl(root + "/" + day + "/events.jsonl");
        if (!jsonl.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            emit storeError(QStringLiteral("事件记录写入失败: %1").arg(jsonl.fileName()));
            return;
        }
        for (SecurityEvent& e : events) {
            e.tsMs = now.toMSecsSinceEpoch();
            e.imagePath = imageRel;

            QJsonObject o;
            o.insert("ts", static_cast<qint64>(e.tsMs));
            o.insert("type", SecurityEvent::typeKey(e.type));
            o.insert("subject", e.subject);
            o.insert("similarity", e.similarity);
            o.insert("detail", e.detail);
            if (!e.imagePath.isEmpty())
                o.insert("image", e.imagePath);
            jsonl.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
            jsonl.write("\n");
        }
    }

signals:
    void storeError(const QString& msg);
};

// ===== EventStore =====
EventStore::EventStore(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<QVector<SecurityEvent>>("QVector<SecurityEvent>");

    preloadTodayLocked();   // 重启可查：当天已落盘事件回读进内存缓存

    _thread = new QThread(this);
    _worker = new EventWorker;              // 无 parent，允许 moveToThread
    _worker->moveToThread(_thread);
    connect(_thread, &QThread::finished, _worker, &QObject::deleteLater);
    connect(_worker, &EventStore::EventWorker::storeError, this, &EventStore::storeError);
    _thread->start();
}

EventStore::~EventStore()
{
    shutdown();
}

void EventStore::preloadTodayLocked()
{
    const QString path = DataConfig::eventsRoot() + "/"
        + QDateTime::currentDateTime().toString("yyyy-MM-dd") + "/events.jsonl";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;   // 当天无记录属正常
    while (!f.atEnd()) {
        const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
        SecurityEvent e;
        e.tsMs = static_cast<qint64>(o.value("ts").toDouble());
        e.subject = o.value("subject").toString();
        e.similarity = o.value("similarity").toDouble(-1.0);
        e.detail = o.value("detail").toString();
        e.imagePath = o.value("image").toString();
        const QString t = o.value("type").toString();
        if (t == "face_known")          e.type = SecurityEvent::FaceKnown;
        else if (t == "face_unknown")   e.type = SecurityEvent::FaceUnknown;
        else if (t == "left_object")    e.type = SecurityEvent::LeftObject;
        else if (t == "left_cleared")   e.type = SecurityEvent::LeftCleared;
        else if (t == "intrusion")      e.type = SecurityEvent::Intrusion;
        else continue;
        _recent.append(e);
    }
    if (_recent.size() > 500)
        _recent = _recent.mid(_recent.size() - 500);
}

void EventStore::enqueue(const QVector<SecurityEvent>& events, const QImage& annotated)
{
    if (events.isEmpty())
        return;
    // 同步进内存缓存（GUI 线程）：worker 只管落盘，HTTP 查询读缓存零等待
    {
        QMutexLocker lk(&_recentMutex);
        for (const SecurityEvent& e : events) {
            _recent.append(e);
            if (_recent.size() > 500)
                _recent.erase(_recent.begin());
        }
    }
    QMetaObject::invokeMethod(_worker, "onEnqueue", Qt::QueuedConnection,
        Q_ARG(QVector<SecurityEvent>, events), Q_ARG(QImage, annotated));
}

QByteArray EventStore::recentJsonBytes(int limit, const QString& typeKey) const
{
    QJsonArray arr;
    QMutexLocker lk(&_recentMutex);
    for (int i = _recent.size() - 1; i >= 0 && arr.size() < limit; --i) {
        const SecurityEvent& e = _recent[i];
        if (!typeKey.isEmpty() && SecurityEvent::typeKey(e.type) != typeKey)
            continue;
        QJsonObject o;
        o.insert("id", "e" + QString::number(e.tsMs));
        o.insert("ts", static_cast<qint64>(e.tsMs));
        o.insert("type", SecurityEvent::typeKey(e.type));
        o.insert("type_name", SecurityEvent::typeName(e.type));
        o.insert("subject", e.subject);
        o.insert("similarity", e.similarity);
        o.insert("detail", e.detail);
        if (!e.imagePath.isEmpty())
            o.insert("image", e.imagePath);
        arr.append(o);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

QString EventStore::resolveImagePath(const QString& rel) const
{
    if (rel.isEmpty()) return QString();
    const QString abs = QDir(DataConfig::eventsRoot()).absoluteFilePath(rel);
    return QFile::exists(abs) ? abs : QString();
}

void EventStore::shutdown()
{
    if (_thread && _thread->isRunning()) {
        _thread->quit();
        _thread->wait(3000);
    }
}

#include "EventStore.moc"
