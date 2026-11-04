QT += testlib
QT -= gui
CONFIG += console testcase
CONFIG -= app_bundle
include(../../cli/cli.pri)
INCLUDEPATH += ..
TARGET = tst_cli
SOURCES += tst_cli.cpp ../fakefirmware.cpp
HEADERS += ../fakefirmware.h
