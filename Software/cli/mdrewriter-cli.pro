include(cli.pri)
TARGET = mdrewriter-cli
TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
DEFINES += APP_VERSION=\\\"1.3.0\\\"
SOURCES += main.cpp

unix:!android:!macx {
    isEmpty(PREFIX): PREFIX = /usr/local
    target.path = $$PREFIX/bin
    INSTALLS += target
}
