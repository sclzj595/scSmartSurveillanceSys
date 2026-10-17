// EventStore.h
// 安防事件中心（业务服务层，PRD 第 8 章 / V0.4 事件持久化）
//
// 职责与边界（第 4 章：事件管理属业务层，不进引擎）：
//   1. SecurityEvent   事件数据模型（类型/对象/相似度/详情/存证截图路径）
//   2. EventThrottle   冷却表（R2：人员识别/陌生人 同目标 30s，禁区入侵 10s）
//   3. EventStore      异步落盘：标注帧截图 + events.jsonl 元数据（R4：
//                      单 worker 线程串行写，磁盘失败仅上报信号，不阻塞识别）
//
// 存储布局（10.1 分区）：
//   events/2026-10-17/events.jsonl   一行一条 JSON 事件记录
//   events/2026-10-17/img/*.jpg      标注帧截图存证（质量 85）
#ifndef EVENTSTORE_H
#define EVENTSTORE_H

#pragma execution_character_set("utf-8")

#include <QObject>
#include <QThread>
#include <QImage>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>

// ===== 事件数据模型 =====
struct SecurityEvent {
    enum Type { FaceKnown, FaceUnknown, LeftObject, LeftCleared, Intrusion };

    Type type = FaceKnown;
    qint64 tsMs = 0;          // epoch ms（落盘时刻，判定与截图差 <100ms）
    int seq = 0;              // 同毫秒序号（A5 去碰撞：id = e<tsMs>-<seq>，进程内单调）
    QString subject;          // 对象：人名 / "陌生人" / "禁区" / "画面目标"
    double similarity = -1.0; // R1 口径 0~100；-1 = 不适用
    QString detail;           // 中文描述
    QString imagePath;        // 相对 eventsRoot 的截图路径（落盘时回填，可空）

    static QString typeKey(Type t);    // jsonl 内英文标识
    static QString typeName(Type t);   // 界面中文名
};

Q_DECLARE_METATYPE(SecurityEvent)
Q_DECLARE_METATYPE(QVector<SecurityEvent>)

// ===== 事件冷却表（R2）=====
// key = "类型:对象"；同一 key 在冷却窗口内只放行一次。
// 遗留物为状态沿判定（出现/解除各一次），另加 60s 保险冷却防检测抖动刷屏。
class EventThrottle
{
public:
    bool allow(const QString& key, qint64 nowMs, qint64 cooldownMs) {
        const auto it = _last.find(key);
        if (it != _last.end() && nowMs - it.value() < cooldownMs)
            return false;
        _last.insert(key, nowMs);
        return true;
    }
private:
    QHash<QString, qint64> _last;
};

// ===== 异步落盘器 =====
class EventStore : public QObject
{
    Q_OBJECT
public:
    explicit EventStore(QObject* parent = nullptr);
    ~EventStore() override;

    // GUI 线程调用：事件批量 + 对应时刻的标注帧（一帧可对应多条事件，共享同一截图）
    void enqueue(const QVector<SecurityEvent>& events, const QImage& annotated);

    // 退出前调用：停 worker 线程（未落盘的尾差可接受，进程即将退出）
    void shutdown();

    // ---- MediaLink 服务层数据源（HTTP API 用）----
    // 内存缓存快照 → QJsonDocument 数组 bytes（最新在前，字段与落盘 jsonl 一致）
    QByteArray recentJsonBytes(int limit, const QString& typeKey) const;
    // 事件存证图绝对路径（rel = jsonl 里的 image 字段）；无文件返回空串
    QString resolveImagePath(const QString& rel) const;

signals:
    void storeError(const QString& msg);   // R4：写失败仅提示，不中断识别

private:
    void preloadTodayLocked();   // 构造：回读当天 jsonl 填充 _recent（重启可查）
    mutable QMutex _recentMutex;           // 保护 _recent（GUI 写 / httplib 线程读）
    QVector<SecurityEvent> _recent;        // 内存缓存（cap 500）
    class EventWorker;      // 定义见 cpp（moveToThread 的无 parent worker）
    EventWorker* _worker = nullptr;
    QThread* _thread = nullptr;
};

#endif // EVENTSTORE_H
