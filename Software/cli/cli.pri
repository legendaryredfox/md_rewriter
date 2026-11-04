# CLI front end, shared by mdrewriter-cli and its test.
include(../core.pri)
QT -= gui
INCLUDEPATH += $$PWD
SOURCES += $$PWD/clirunner.cpp
HEADERS += $$PWD/clirunner.h
