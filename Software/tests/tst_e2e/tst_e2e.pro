QT += testlib
CONFIG += console testcase
CONFIG -= app_bundle
include(../../app.pri)
INCLUDEPATH += ..
TARGET = tst_e2e
SOURCES += tst_e2e.cpp ../fakefirmware.cpp
HEADERS += ../fakefirmware.h
