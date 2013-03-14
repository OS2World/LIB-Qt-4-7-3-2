TEMPLATE        = app
TARGET          = lrelease
DESTDIR         = ../../../bin

DEFINES += QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII
SOURCES += main.cpp

os2:DEFINES += QT_NODLL

INCLUDEPATH += $$QT_BUILD_TREE/src/corelib/global # qlibraryinfo.cpp includes qconfig.cpp
SOURCES += \
    $$QT_SOURCE_TREE/src/corelib/global/qlibraryinfo.cpp \
    $$QT_SOURCE_TREE/src/corelib/io/qsettings.cpp
win32:SOURCES += $$QT_SOURCE_TREE/src/corelib/io/qsettings_win.cpp
os2:SOURCES += $$QT_SOURCE_TREE/src/corelib/io/qsettings_os2.cpp
macx:SOURCES += $$QT_SOURCE_TREE/src/corelib/io/qsettings_mac.cpp

include(../../../src/tools/bootstrap/bootstrap.pri)
include(../shared/formats.pri)
include(../shared/proparser.pri)
include(../../shared/symbian/epocroot.pri)

win32:LIBS += -ladvapi32   # for qsettings_win.cpp
os2:LIBS += -lregistry.dll   # for qsettings_os2.cpp

target.path=$$[QT_INSTALL_BINS]
INSTALLS        += target

include(../../../src/qt_targets.pri)
