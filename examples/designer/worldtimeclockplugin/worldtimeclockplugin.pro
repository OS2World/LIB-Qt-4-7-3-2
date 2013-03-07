#! [0]
TEMPLATE    = lib
#! [0]
TARGET      = $$qtLibraryTarget($$TARGET)
#! [1]
CONFIG      += designer plugin
#! [1]
QTDIR_build:DESTDIR     = $$QT_BUILD_TREE/plugins/designer

#! [2]
HEADERS     = worldtimeclock.h \
              worldtimeclockplugin.h
SOURCES     = worldtimeclock.cpp \
              worldtimeclockplugin.cpp
#! [2]

os2:TARGET_SHORT = $$qtLibraryTarget(wrldclk)

# install
target.path = $$[QT_INSTALL_PLUGINS]/designer
sources.files = $$SOURCES $$HEADERS *.pro
sources.path = $$[QT_INSTALL_EXAMPLES]/designer/worldtimeclockplugin
INSTALLS += target sources

symbian: include($$QT_SOURCE_TREE/examples/symbianpkgrules.pri)
