# canview · Qt 上位机（V1：读离线日志画曲线）
#
# 为什么不复制一份 canlog.c 进这个目录：
#   日志格式只能有一个实现。抄一份进来，两边就会各自演化，
#   某天 canreplay 读得出来、canview 读不出来，排查成本极高。
#   所以这里直接 ../canlog.c —— 它是 C，C++ 侧用 extern "C" 桥接。

QT       += core gui widgets printsupport

CONFIG   += c++11
CONFIG   -= app_bundle

TARGET   = canview
TEMPLATE = app

INCLUDEPATH += . ..

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    canlog_reader.cpp \
    qcustomplot.cpp \
    ../canlog.c

HEADERS += \
    mainwindow.h \
    canlog_reader.h \
    qcustomplot.h \
    ../canlog.h
