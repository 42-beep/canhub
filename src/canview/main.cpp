#include <QApplication>
#include <QTimer>

#include "mainwindow.h"

/*
 * 两种用法：
 *   ./canview log.canlog              交互：窗口 + 下拉框 + 可拖动缩放
 *   ./canview --png out.png log.canlog  无界面渲染一张图（验证 / 出图用）
 *
 * 第二种是为了在没有桌面截屏工具的环境里也能确认"曲线真的画出来了"。
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("canview"));
    app.setApplicationVersion(QStringLiteral("0.1"));

    MainWindow w;

    const bool pngMode = (argc > 3 &&
                          QString::fromLocal8Bit(argv[1]) == QLatin1String("--png"));
    if (pngMode) {
        const QString pngPath = QString::fromLocal8Bit(argv[2]);
        w.loadFile(QString::fromLocal8Bit(argv[3]));

        /* 等布局稳定再渲染，否则图是空的 */
        QTimer::singleShot(300, &app, [&w, pngPath]() {
            w.savePlot(pngPath);
            QCoreApplication::quit();
        });
        return app.exec();
    }

    w.show();
    if (argc > 1)
        w.loadFile(QString::fromLocal8Bit(argv[1]));

    return app.exec();
}
