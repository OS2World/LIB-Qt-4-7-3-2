#! [0] #! [1]
TEMPLATE    = lib
#! [0]
TARGET      = $$qtLibraryTarget($$TARGET)
#! [2]
CONFIG      += designer plugin
#! [1] #! [2]
QTDIR_build:DESTDIR     = $$QT_BUILD_TREE/plugins/designer

#! [3]
HEADERS     = analogclock.h \
              customwidgetplugin.h
SOURCES     = analogclock.cpp \
              customwidgetplugin.cpp
#! [3]

os2:TARGET_SHORT = $$qtLibraryTarget(cstmwgt)

# install
target.path = $$[QT_INSTALL_PLUGINS]/designer
sources.files = $$SOURCES $$HEADERS *.pro
sources.path = $$[QT_INSTALL_EXAMPLES]/designer/customwidgetplugin
INSTALLS += target sources

symbian: include($$QT_SOURCE_TREE/examples/symbianpkgrules.pri)
