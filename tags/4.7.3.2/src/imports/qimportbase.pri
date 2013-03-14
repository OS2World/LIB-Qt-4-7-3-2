symbian:include(../plugins/qpluginbase.pri)
TEMPLATE = lib
CONFIG += qt plugin

win32|mac:!wince*:!win32-msvc:!macx-xcode:CONFIG += debug_and_release

isEmpty(TARGETPATH) {
    error("qimportbase.pri: You must provide a TARGETPATH!")
}
isEmpty(TARGET) {
    error("qimportbase.pri: You must provide a TARGET!")
}

!isEmpty(TARGET_SHORT):PLUGINFILENAME = $$TARGET_SHORT
else:PLUGINFILENAME = $$TARGET

QMLDIRFILE.input = $${_PRO_FILE_PWD_}/qmldir.in
QMLDIRFILE.output = $$QT_BUILD_TREE/imports/$$TARGETPATH/qmldir
QMAKE_SUBSTITUTES += QMLDIRFILE

TARGET = $$qtLibraryTarget($$TARGET)
!isEmpty(TARGET_SHORT):TARGET_SHORT = $$qtLibraryTarget($$TARGET_SHORT)
contains(QT_CONFIG, reduce_exports):CONFIG += hide_symbols

include(../qt_targets.pri)

wince*:LIBS += $$QMAKE_LIBS_GUI

symbian: {
    TARGET.EPOCALLOWDLLDATA=1
    TARGET.CAPABILITY = All -Tcb
    load(armcc_warnings)
}

QTDIR_build:DESTDIR = $$QT_BUILD_TREE/imports/$$TARGETPATH
target.path = $$[QT_INSTALL_IMPORTS]/$$TARGETPATH

qmldir.files += $$QMLDIRFILE.output
qmldir.path +=  $$[QT_INSTALL_IMPORTS]/$$TARGETPATH

INSTALLS += target qmldir
