TEMPLATE = lib

CONFIG += dll
QT =

TARGET = xsystray

SOURCES = w_xsystray.c

DEF_FILE = w_xsystray.def

INCLUDEPATH += .. ../apilib

DESTDIR = ../tmp/dist/plugin
