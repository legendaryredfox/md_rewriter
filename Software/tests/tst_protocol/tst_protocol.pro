QT += testlib
QT -= gui
CONFIG += console testcase
CONFIG -= app_bundle
include(../../core.pri)
TARGET = tst_protocol
SOURCES += tst_protocol.cpp
