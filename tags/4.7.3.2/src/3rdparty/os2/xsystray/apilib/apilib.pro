TEMPLATE = lib

CONFIG += dll
QT =

TARGET = xsystray

SOURCES = xsystray.c

DEF_FILE = xsystray.def

INCLUDEPATH += .. ../plugin ../apilib

DESTDIR = ../tmp/dist/apilib
