// EventHistoryDialog.cpp
#include "EventHistoryDialog.h"
#include "events/EventStore.h"
#include "core/dataConfig.h"
#include "ui/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

namespace {
constexpr int kMaxRows = 500;   // 单次加载上限，防历史累积拖卡 UI（30 天量级足够回溯）
}

EventHistoryDialog::EventHistoryDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("历史事件"));
    resize(860, 560);

    auto* lay = new QVBoxLayout(this);

    // 筛选行
    auto* filterLay = new QHBoxLayout;
    filterLay->addWidget(new QLabel(QStringLiteral("类型："), this));
    _filter = new QComboBox(this);
    _filter->addItem(QStringLiteral("全部"), QString());
    for (int t = SecurityEvent::FaceKnown; t <= SecurityEvent::Intrusion; ++t)
        _filter->addItem(SecurityEvent::typeName(static_cast<SecurityEvent::Type>(t)),
                         SecurityEvent::typeKey(static_cast<SecurityEvent::Type>(t)));
    connect(_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { reload(); });
    filterLay->addWidget(_filter);
    filterLay->addStretch(1);
    lay->addLayout(filterLay);

    // 事件表
    _table = new QTableWidget(this);
    _table->setColumnCount(5);
    _table->setHorizontalHeaderLabels({ QStringLiteral("时间"), QStringLiteral("类型"),
                                        QStringLiteral("对象"), QStringLiteral("相似度"),
                                        QStringLiteral("详情") });
    _table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->verticalHeader()->setVisible(false);
    connect(_table, &QTableWidget::cellClicked, this, &EventHistoryDialog::showDetail);
    lay->addWidget(_table, 1);

    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    lay->addWidget(closeBtn, 0, Qt::AlignRight);

    reload();
}

void EventHistoryDialog::reload()
{
    _rows.clear();
    _table->setRowCount(0);

    const QString filterKey = _filter->currentData().toString();
    const QString root = DataConfig::eventsRoot();

    // 日期目录倒序（新事件优先）
    QDir rootDir(root);
    QStringList days = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                         QDir::Name | QDir::Reversed);

    for (const QString& day : days) {
        QFile jsonl(root + "/" + day + "/events.jsonl");
        if (!jsonl.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        // 单文件内倒序：先读全文件再逆序遍历（单日量级小，内存可承受）
        QVector<QByteArray> lines;
        while (!jsonl.atEnd())
            lines.append(jsonl.readLine());
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            const QJsonObject o = QJsonDocument::fromJson(*it).object();
            if (o.isEmpty())
                continue;
            const QString type = o.value("type").toString();
            if (!filterKey.isEmpty() && type != filterKey)
                continue;

            Row r;
            r.tsText = QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(o.value("ts").toDouble())).toString("yyyy-MM-dd HH:mm:ss");
            r.typeKey = type;
            r.typeName = [&]{
                for (int t = SecurityEvent::FaceKnown; t <= SecurityEvent::Intrusion; ++t)
                    if (SecurityEvent::typeKey(static_cast<SecurityEvent::Type>(t)) == type)
                        return SecurityEvent::typeName(static_cast<SecurityEvent::Type>(t));
                return type;
            }();
            r.subject = o.value("subject").toString();
            const double sim = o.value("similarity").toDouble(-1.0);
            r.simText = sim >= 0 ? QStringLiteral("%1%").arg(sim, 0, 'f', 0) : QStringLiteral("—");
            r.detail = o.value("detail").toString();
            const QString rel = o.value("image").toString();
            if (!rel.isEmpty())
                r.imageAbs = root + "/" + rel;
            _rows.append(r);
            if (_rows.size() >= kMaxRows)
                break;
        }
        if (_rows.size() >= kMaxRows)
            break;
    }

    _table->setRowCount(_rows.size());
    for (int i = 0; i < _rows.size(); ++i) {
        const Row& r = _rows[i];
        _table->setItem(i, 0, new QTableWidgetItem(r.tsText));
        _table->setItem(i, 1, new QTableWidgetItem(r.typeName));
        _table->setItem(i, 2, new QTableWidgetItem(r.subject));
        _table->setItem(i, 3, new QTableWidgetItem(r.simText));
        _table->setItem(i, 4, new QTableWidgetItem(r.detail));
    }
}

void EventHistoryDialog::showDetail(int row)
{
    if (row < 0 || row >= _rows.size())
        return;
    const Row& r = _rows[row];

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("事件详情"));
    dlg.resize(980, 720);
    ThemeManager::instance().applyNative(&dlg);   // 原生标题栏按当前主题令牌配色
    auto* lay = new QVBoxLayout(&dlg);

    QLabel* meta = new QLabel(
        QStringLiteral("%1 ｜ %2 ｜ %3 ｜ 相似度 %4").arg(r.tsText, r.typeName,
            r.subject.isEmpty() ? QStringLiteral("—") : r.subject, r.simText), &dlg);
    lay->addWidget(meta);

    QLabel* img = new QLabel(&dlg);
    img->setAlignment(Qt::AlignCenter);
    if (!r.imageAbs.isEmpty() && QFile::exists(r.imageAbs)) {
        img->setPixmap(QPixmap(r.imageAbs).scaled(960, 640,
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        img->setText(QStringLiteral("（该事件无截图存证）"));
    }
    lay->addWidget(img, 1);

    QLabel* detail = new QLabel(r.detail, &dlg);
    detail->setWordWrap(true);
    lay->addWidget(detail);

    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    lay->addWidget(closeBtn, 0, Qt::AlignRight);

    dlg.exec();
}
