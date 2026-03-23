QT       += core gui widgets network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG   += c++17

TARGET   = rfsoc_tool
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/plotwidget.cpp \
    src/cmdworker.cpp \
    src/dataworker.cpp \
    src/waveformgenerator.cpp \
    src/waterfallwidget.cpp

HEADERS += \
    src/mainwindow.h \
    src/plotwidget.h \
    src/cmdworker.h \
    src/dataworker.h \
    src/waveformgenerator.h \
    src/waterfallwidget.h \
    src/commontypes.h \
    src/constants.h
