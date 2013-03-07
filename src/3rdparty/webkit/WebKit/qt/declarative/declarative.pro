TARGET  = qmlwebkitplugin
os2:TARGET_SHORT = qmlweb
TARGETPATH = QtWebKit

TEMPLATE = lib
CONFIG += qt plugin

win32|mac:!wince*:!win32-msvc:!macx-xcode:CONFIG += debug_and_release

isEmpty(OUTPUT_DIR): OUTPUT_DIR = ../../..

!isEmpty(TARGET_SHORT):PLUGINFILENAME = $$TARGET_SHORT
else:PLUGINFILENAME = $$TARGET

QMLDIRFILE.input = $${_PRO_FILE_PWD_}/qmldir.in
CONFIG(QTDIR_build) {
    QMLDIRFILE.output = $$QT_BUILD_TREE/imports/$$TARGETPATH/qmldir
} else {
    QMLDIRFILE.output = $$OUTPUT_DIR/imports/$$TARGETPATH/qmldir
}
QMAKE_SUBSTITUTES += QMLDIRFILE

TARGET = $$qtLibraryTarget($$TARGET)
!isEmpty(TARGET_SHORT):TARGET_SHORT = $$qtLibraryTarget($$TARGET_SHORT)
contains(QT_CONFIG, reduce_exports):CONFIG += hide_symbols

wince*:LIBS += $$QMAKE_LIBS_GUI

symbian: {
    TARGET.EPOCALLOWDLLDATA=1
    TARGET.CAPABILITY = All -Tcb
    load(armcc_warnings)
    TARGET = $$TARGET$${QT_LIBINFIX}
}

include(../../../WebKit.pri)

QT += declarative

!CONFIG(standalone_package) {
    linux-* {
        # From Creator's src/rpath.pri:
        # Do the rpath by hand since it's not possible to use ORIGIN in QMAKE_RPATHDIR
        # this expands to $ORIGIN (after qmake and make), it does NOT read a qmake var.
        QMAKE_RPATHDIR = $$OUTPUT_DIR/lib $$QMAKE_RPATHDIR
        MY_RPATH = $$join(QMAKE_RPATHDIR, ":")

        QMAKE_LFLAGS += -Wl,-z,origin \'-Wl,-rpath,$${MY_RPATH}\'
        QMAKE_RPATHDIR =
    } else {
        QMAKE_RPATHDIR = $$OUTPUT_DIR/lib $$QMAKE_RPATHDIR
    }
}

SOURCES += qdeclarativewebview.cpp plugin.cpp
HEADERS += qdeclarativewebview_p.h

CONFIG(QTDIR_build) {
    DESTDIR = $$QT_BUILD_TREE/imports/$$TARGETPATH
} else {
    DESTDIR = $$OUTPUT_DIR/imports/$$TARGETPATH
}
target.path = $$[QT_INSTALL_IMPORTS]/$$TARGETPATH

qmldir.files += $$QMLDIRFILE.output
qmldir.path +=  $$[QT_INSTALL_IMPORTS]/$$TARGETPATH

symbian:{
    TARGET.UID3 = 0x20021321
}

INSTALLS += target qmldir
