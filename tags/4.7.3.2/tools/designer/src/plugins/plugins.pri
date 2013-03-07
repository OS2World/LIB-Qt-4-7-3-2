CONFIG += designer
win32|mac: CONFIG+= debug_and_release
QTDIR_build:DESTDIR = $$QT_BUILD_TREE/plugins/designer
contains(TEMPLATE, ".*lib") {
    TARGET = $$qtLibraryTarget($$TARGET)
    !isEmpty(TARGET_SHORT):TARGET_SHORT = $$qtLibraryTarget($$TARGET_SHORT)
}

# install
target.path = $$[QT_INSTALL_PLUGINS]/designer
INSTALLS += target
