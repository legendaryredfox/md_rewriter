# GUI part of the application, shared by the executable and the end-to-end test.
include(core.pri)
QT += gui widgets

SOURCES += $$PWD/mainwindow.cpp
HEADERS += $$PWD/mainwindow.h
FORMS += $$PWD/mainwindow.ui
