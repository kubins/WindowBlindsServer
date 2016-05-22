QT += core network
QT -= gui

TEMPLATE = app

TARGET = WindowBlindsServer

CONFIG += console C++11
CONFIG -= app_bundle

SOURCES += main.cpp

# pouze z duvodu #include "*.moc" (aby QtCreator nehlasil chybu)
INCLUDEPATH += $$OUT_PWD/release \
    $$OUT_PWD/debug
