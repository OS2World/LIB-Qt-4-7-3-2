TEMPLATE = lib
CONFIG += qt plugin

win32|mac:!wince*:!win32-msvc:!macx-xcode:CONFIG += debug_and_release
TARGET = $$qtLibraryTarget($$TARGET)
!isEmpty(TARGET_SHORT):TARGET_SHORT = $$qtLibraryTarget($$TARGET_SHORT)
contains(QT_CONFIG, reduce_exports):CONFIG += hide_symbols

include(../qt_version.pri)
include(../qt_targets.pri)

wince*:LIBS += $$QMAKE_LIBS_GUI

symbian: {
    TARGET.EPOCALLOWDLLDATA=1
    TARGET.CAPABILITY = All -Tcb
    TARGET = $${TARGET}$${QT_LIBINFIX}
    load(armcc_warnings)
}
