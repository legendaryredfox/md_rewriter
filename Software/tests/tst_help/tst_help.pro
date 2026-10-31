QT += testlib widgets xml
CONFIG += console testcase
CONFIG -= app_bundle
include(../../help.pri)
DEFINES += SRCDIR=\\\"$$PWD/../..\\\"
TARGET = tst_help
SOURCES += tst_help.cpp
