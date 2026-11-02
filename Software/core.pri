# GUI-free core shared by the application and the tests.
QT += core serialport
CONFIG += c++17

INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/protocol.cpp \
    $$PWD/streamparser.cpp \
    $$PWD/devicesession.cpp \
    $$PWD/filecompare.cpp \
    $$PWD/romimage.cpp \
    $$PWD/romverifier.cpp \
    $$PWD/chipdb.cpp

HEADERS += \
    $$PWD/protocol.h \
    $$PWD/streamparser.h \
    $$PWD/devicesession.h \
    $$PWD/filecompare.h \
    $$PWD/romimage.h \
    $$PWD/romverifier.h \
    $$PWD/chipdb.h
