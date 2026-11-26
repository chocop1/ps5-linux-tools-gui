QT += widgets
CONFIG += c++17 warn_on release
TARGET = ps5tools-qt6

INCLUDEPATH += src /usr/include/LayerShellQt
LIBS += -lLayerShellQtInterface

HEADERS += \
    src/config.h \
    src/sysinfo.h \
    src/workers.h \
    src/widgets.h \
    src/overlay.h \
    src/tabs.h \
    src/mainwindow.h

SOURCES += \
    src/config.cpp \
    src/sysinfo.cpp \
    src/workers.cpp \
    src/widgets.cpp \
    src/overlay.cpp \
    src/tabs_part1.cpp \
    src/tabs_part2.cpp \
    src/tabs_settings.cpp \
    src/mainwindow.cpp \
    src/main.cpp
