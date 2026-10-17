// EventStore.cpp
#include "events/EventStore.h"
#include "core/dataConfig.h"

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
// 阶段02 持久化恢复能力：
//   - ts 语义 = 帧采集时刻（创建时已赋值），worker 不再改写，事件↔画面精确回溯
//   - 每行写后 flush：进程崩溃最多丢当前行，不损已有记录
//   - 连续 3 次写失败升级为显式告警（携带连续次数），恢复成功自动清零
class EventStore::EventWorker : public QObject
{
    Q_OBJECT
public:
    explicit EventWorker(std::atomic<int>* pendingPosts) : _pendingPosts(pendingPosts) {}
public slots:
    void onEnqueue(QVector<SecurityEvent> events, QImage annotated)
    {
        if (_pendingPosts) _pendingPosts->fetch_sub(1, std::memory_order_relaxed);   // 出队：在途计数回减
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
                _snapshotFails = qMin(_snapshotFails + 1, 3);
                emit storeError(QStringLiteral("截图存证写入失败%1: %2")
                    .arg(_snapshotFails >= 3 ? QStringLiteral("（连续 %1 次）").arg(_snapshotFails) : QString())
                    .arg(abs));
            }
        }

        // 元数据追加（JSONL：一行一条，崩溃不损已有记录；逐行 flush 防半行）
        QFile jsonl(root + "/" + day + "/events.jsonl");
        if (!jsonl.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            _writeFails = qMin(_writeFails + 1, 3);
            emit storeError(QStringLiteral("事件记录写入失败%1: %2")
                .arg(_writeFails >= 3 ? QStringLiteral("（连续 %1 次，请检查磁盘）").arg(_writeFails) : QString())
                .arg(jsonl.fileName()));
            return;
        }
        for (SecurityEvent& e : events) {
            e.imagePath = imageRel;

            QJsonObject o;
            o.insert("ts", static_cast<qint64>(e.tsMs));
            o.insert("seq", e.seq);   // 落盘：重启回读后 id 稳定
            if (e.captureTsMs > 0)
                o.insert("capture_ts", static_cast<qint64>(e.captureTsMs));
            o.insert("type", SecurityEvent::typeKey(e.type));
            o.insert("subject", e.subject);
            o.insert("similarity", e.similarity);
            o.insert("detail", e.detail);
            if (!e.imagePath.isEmpty())
                o.insert("image", e.imagePath);
            jsonl.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
            jsonl.write("\n");
            jsonl.flush();            // 崩溃恢复：最多丢当前行
        }
        if (jsonl.error() != QFileDevice::NoError) {
            _writeFails = qMin(_writeFails + 1, 3);
            emit storeError(QStringLiteral("事件记录写入异常%1（error=%2）")
                .arg(_writeFails >= 3 ? QStringLiteral("（连续 %1 次，请检查磁盘）").arg(_writeFails) : QString())
                .arg(int(jsonl.error())));
        } else if (_writeFails > 0) {
            _writeFails = 0;
            emit storeError(QStringLiteral("事件记录写入已恢复"));
        }
    }

signals:
    void storeError(const QString& msg);

private:
    std::atomic<int>* _pendingPosts = nullptr;  // 宿主在途计数（出队回减）
    int _writeFails = 0;      // 连续写失败计数（恢复清零；封顶 3 只升一次级）
    int _snapshotFails = 0;   // 连续截图失败计数（同上）
};

// ===== EventStore =====
EventStore::EventStore(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<QVector<SecurityEvent>>("QVector<SecurityEvent>");

    preloadTodayLocked();   // 重启可查：当天已落盘事件回读进内存缓存

    _thread = new QThread(this);
    _worker = new EventWorker(&_pendingPosts);   // 无 parent，允许 moveToThread
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
    qint64 lineNo = 0;
    int skipped = 0;
    while (!f.atEnd()) {
        ++lineNo;
        const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
        SecurityEvent e;
        e.tsMs = static_cast<qint64>(o.value("ts").toDouble());
        e.seq = o.value("seq").toInt();   // 旧格式无 seq 时为 0，id 仍可生成
        e.captureTsMs = static_cast<qint64>(o.value("capture_ts").toDouble());
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
        else {
            // 显式跳过坏行（崩溃半行/手改损坏）：不静默，计数并留痕，跳过不影响其余行
            ++skipped;
            qWarning() << "EventStore preload: 跳过无法解析的事件行"
                       << "line=" << lineNo << "file=" << path;
            continue;
        }
        _recent.append(e);
    }
    if (skipped > 0)
        qWarning() << "EventStore preload: 共跳过" << skipped << "条损坏事件行（文件:" << path << "）";
    if (_recent.size() > 500)
        _recent = _recent.mid(_recent.size() - 500);
}

void EventStore::enqueue(const QVector<SecurityEvent>& events, const QImage& annotated)
{
    if (events.isEmpty())
        return;
    // 有界队列（阶段02 持久化恢复）：磁盘慢/失效时在途任务封顶，超限丢弃并计数，
    // 决不让落盘积压无限吃内存——被丢弃事件已在内存缓存中可见，磁盘缺失可追溯
    if (_pendingPosts.load(std::memory_order_relaxed) >= kMaxPendingPosts) {
        _droppedTotal += events.size();
        emit storeError(QStringLiteral("事件落盘队列已满（在途 %1），本批 %2 条仅保留在内存缓存；累计丢弃 %3 条")
            .arg(_pendingPosts.load(std::memory_order_relaxed)).arg(events.size()).arg(_droppedTotal));
        return;
    }
    // 同步进内存缓存（GUI 线程）：worker 只管落盘，HTTP 查询读缓存零等待
    {
        QMutexLocker lk(&_recentMutex);
        for (const SecurityEvent& e : events) {
            _recent.append(e);
            if (_recent.size() > 500)
                _recent.erase(_recent.begin());
        }
    }
    _pendingPosts.fetch_add(1, std::memory_order_relaxed);
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
        // A5 去碰撞 ID：e<ts>-<seq>（seq 同毫秒单调；旧记录回读 seq=0 兼容）
        o.insert("id", QStringLiteral("e%1-%2").arg(e.tsMs).arg(e.seq));
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
