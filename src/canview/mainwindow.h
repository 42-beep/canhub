#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

#include "canlog_reader.h"

class QComboBox;
class QLabel;
class QCustomPlot;

/*
 * 第一版 canview 只做三件事：
 *   1. 打开一份 .canlog
 *   2. 列出文件里出现过的所有 CAN ID
 *   3. 选中 ID + 字节，画出该字节随时间的变化
 *
 * 刻意不做：接实时源、DBC 解码、DTC 面板、参数下发。
 * 那些要等 canhubd 和 M3 解码层，现在做等于给自己造需求。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    /* 供命令行参数直接打开文件用（./canview xxx.log） */
    void loadFile(const QString &path);

    /* 把当前曲线渲染成 PNG，用于无界面验证和出图 */
    void savePlot(const QString &path);

private slots:
    void openLog();
    void refreshCurve();

private:
    void fillIdBox();

    QCustomPlot *m_plot    = nullptr;
    QComboBox   *m_idBox   = nullptr;
    QComboBox   *m_byteBox = nullptr;
    QLabel      *m_status  = nullptr;

    /* 值成员：RAII，窗口析构时日志自动释放，不用手写 close */
    CanLogFile   m_log;
};

#endif /* MAINWINDOW_H */
