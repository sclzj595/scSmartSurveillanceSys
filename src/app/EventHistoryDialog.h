// EventHistoryDialog.h
// 历史事件查询（PRD 8.3 / V0.4）：类型筛选 + 时间倒序 + 大图查看
#ifndef EVENTHISTORYDIALOG_H
#define EVENTHISTORYDIALOG_H

#pragma execution_character_set("utf-8")

#include <QDialog>
#include <QComboBox>
#include <QTableWidget>
#include <QString>
#include <QVector>

class EventHistoryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EventHistoryDialog(QWidget* parent = nullptr);

private slots:
    void reload();          // 读取 events/*/events.jsonl，按筛选填充表格
    void showDetail(int row);

private:
    struct Row {
        QString tsText;     // yyyy-MM-dd HH:mm:ss
        QString typeKey;    // jsonl 内英文标识
        QString typeName;   // 中文名
        QString subject;
        QString simText;
        QString detail;
        QString imageAbs;   // 截图绝对路径（可能为空）
    };

    QComboBox* _filter = nullptr;
    QTableWidget* _table = nullptr;
    QVector<Row> _rows;     // 与表格行一一对应
};

#endif // EVENTHISTORYDIALOG_H
