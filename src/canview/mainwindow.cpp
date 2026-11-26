#include "mainwindow.h"

#include "qcustomplot.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPen>
#include <QStatusBar>
#include <QToolBar>
#include <QVector>

#include <string>
#include <vector>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("canview · CAN 日志曲线"));
    resize(1000, 600);

    QToolBar *tb = addToolBar(tr("工具"));
    tb->setMovable(false);

    QAction *actOpen = tb->addAction(tr("打开日志…"));
    connect(actOpen, &QAction::triggered, this, &MainWindow::openLog);

    tb->addSeparator();
    tb->addWidget(new QLabel(tr(" ID "), this));
    m_idBox = new QComboBox(this);
    m_idBox->setMinimumWidth(110);
    tb->addWidget(m_idBox);

    tb->addWidget(new QLabel(tr(" 字节 "), this));
    m_byteBox = new QComboBox(this);
    for (int i = 0; i < 8; ++i)
        m_byteBox->addItem(QStringLiteral("data[%1]").arg(i));
    tb->addWidget(m_byteBox);

    m_plot = new QCustomPlot(this);
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->xAxis->setLabel(tr("时间 (ms)"));
    m_plot->yAxis->setLabel(tr("字节值"));
    m_plot->yAxis->setRange(0, 255);
    setCentralWidget(m_plot);

    m_status = new QLabel(tr("未打开文件"), this);
    statusBar()->addWidget(m_status);

    connect(m_idBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::refreshCurve);
    connect(m_byteBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::refreshCurve);
}

void MainWindow::openLog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 CAN 日志"), QString(),
        tr("CAN 日志 (*.log *.canlog);;所有文件 (*)"));
    if (!path.isEmpty())
        loadFile(path);
}

void MainWindow::loadFile(const QString &path)
{
    std::string err;

    if (!m_log.open(QFile::encodeName(path).toStdString(), &err)) {
        QMessageBox::warning(this, tr("打不开"),
                             QString::fromStdString(err) + "\n\n" + path);
        return;
    }

    setWindowTitle(tr("canview · %1").arg(QFileInfo(path).fileName()));
    fillIdBox();
}

void MainWindow::savePlot(const QString &path)
{
    /* savePng 内部会临时按给定尺寸 render，不依赖窗口真实大小，
       所以 QT_QPA_PLATFORM=offscreen 下也能用 —— 没有桌面时靠它验证 */
    m_plot->savePng(path, 1200, 700);
}

void MainWindow::fillIdBox()
{
    m_idBox->blockSignals(true);
    m_idBox->clear();

    const std::vector<uint32_t> idList = m_log.ids();
    for (uint32_t id : idList) {
        m_idBox->addItem(QStringLiteral("0x") + QString::number(id, 16).toUpper(),
                         QVariant::fromValue<uint>(id));
    }

    m_idBox->blockSignals(false);

    m_status->setText(tr("%1 帧 · %2 个 ID · 时长 %3 ms")
                          .arg(m_log.count())
                          .arg(idList.size())
                          .arg(m_log.durationMs(), 0, 'f', 1));

    if (m_idBox->count() > 0)
        refreshCurve();
    else
        m_plot->replot();
}

void MainWindow::refreshCurve()
{
    if (!m_log.isOpen() || m_idBox->count() == 0)
        return;

    bool ok = false;
    const uint32_t id = m_idBox->currentData().toUInt(&ok);
    if (!ok)
        return;

    const int byteIdx = m_byteBox->currentIndex();
    const std::vector<cv_sample_t> samples = m_log.samples(id);
    if (samples.empty()) {
        m_plot->clearGraphs();
        m_plot->replot();
        return;
    }

    QVector<double> xs, ys;
    xs.reserve(static_cast<int>(samples.size()));
    ys.reserve(static_cast<int>(samples.size()));
    for (const cv_sample_t &s : samples) {
        xs.append(s.t_ms);
        /* dlc 之外的字节是缓冲区残留，画出来是假数据 */
        ys.append(static_cast<double>(byteIdx < s.dlc ? s.data[byteIdx] : 0));
    }

    m_plot->clearGraphs();
    QCPGraph *g = m_plot->addGraph();
    g->setPen(QPen(QColor(0, 120, 215), 1.4));
    g->setLineStyle(QCPGraph::lsLine);
    if (xs.size() <= 5000)
        g->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 3));
    g->setData(xs, ys);

    m_plot->yAxis->setRange(0, 255);
    if (xs.size() == 1 || xs.first() == xs.last())
        m_plot->xAxis->setRange(xs.first(), xs.first() + 1.0);
    else
        m_plot->xAxis->setRange(xs.first(), xs.last());
    m_plot->replot();

    m_status->setText(tr("%1 帧 · %2 个 ID · 时长 %3 ms · 当前 0x%4 共 %5 点")
                          .arg(m_log.count())
                          .arg(m_idBox->count())
                          .arg(m_log.durationMs(), 0, 'f', 1)
                          .arg(id, 0, 16)
                          .arg(samples.size()));
}
