QT += testlib
QT -= gui
CONFIG += console testcase
CONFIG -= app_bundle
include(../../core.pri)
INCLUDEPATH += ..
TARGET = tst_session
SOURCES += tst_session.cpp ../fakefirmware.cpp
HEADERS += ../fakefirmware.h
