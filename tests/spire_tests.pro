QT += core gui qml quick quickcontrols2 testlib dbus sql
CONFIG += c++17 testcase
TARGET = spire_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += fakepicker.h ../src/backend.h ../src/filepicker.h ../src/linemodel.h ../src/theme.h ../src/translator.h
SOURCES += main.cpp spire_tests.cpp window_tests.cpp ../src/backend.cpp ../src/billing.cpp ../src/cli.cpp \
    ../src/linemodel.cpp ../src/money.cpp ../src/payapp.cpp ../src/plumbimport.cpp ../src/store.cpp ../src/theme.cpp \
    ../src/translator.cpp
RESOURCES += ../src/resources.qrc
