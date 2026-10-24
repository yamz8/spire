QT += core gui qml quick quickcontrols2 dbus sql

CONFIG += c++17 release
TARGET = spire
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/billing.h \
    src/cli.h \
    src/filepicker.h \
    src/linemodel.h \
    src/money.h \
    src/payapp.h \
    src/plumbimport.h \
    src/portalfilepicker.h \
    src/store.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/billing.cpp \
    src/cli.cpp \
    src/linemodel.cpp \
    src/money.cpp \
    src/payapp.cpp \
    src/plumbimport.cpp \
    src/portalfilepicker.cpp \
    src/store.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
