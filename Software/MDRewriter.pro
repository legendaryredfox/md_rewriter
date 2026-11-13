include(app.pri)

TARGET = MDRewriter
VERSION = 1.3.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"
TEMPLATE = app

# Written for Qt 6 (tested with 6.4). Qt 5.15 is intended to work but is untested. Qt Creator users: open this file.
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

# Warn about deprecated Qt APIs.
DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += main.cpp

# Translations: lrelease runs at build time and the .qm files are embedded (":/i18n").
# Update the .ts files with:  lupdate MDRewriter.pro   (Qt 6: lupdate -locations relative ...)
TRANSLATIONS += translations/MDRewriter_pt_BR.ts
CONFIG += lrelease embed_translations

win32: RC_ICONS = "md.ico"

# Linux installation
unix:!android:!macx {
    isEmpty(PREFIX): PREFIX = /usr/local
    target.path = $$PREFIX/bin
    desktop.files = linux/mdrewriter.desktop
    desktop.path = $$PREFIX/share/applications
    icon.files = linux/mdrewriter.png
    icon.path = $$PREFIX/share/icons/hicolor/256x256/apps
    udev.files = linux/99-md-rewriter.rules
    udev.path = $$PREFIX/lib/udev/rules.d
    INSTALLS += target desktop icon udev
}
