######################################################################
# Automatically generated by qmake (1.08a) Fri Jan 7 15:25:07 2005
######################################################################

TEMPLATE = app
CONFIG -= moc app_bundle
QT -= gui
DEPENDPATH += .
INCLUDEPATH += .
QT = core

# Input
SOURCES += waitconditions.cpp
CONFIG += qt warn_on create_prl link_prl console

# install
target.path = $$[QT_INSTALL_EXAMPLES]/threads/waitconditions
sources.files = $$SOURCES $$HEADERS $$RESOURCES $$FORMS waitconditions.pro
sources.path = $$[QT_INSTALL_EXAMPLES]/threads/waitconditions
INSTALLS += target sources

symbian: include($$QT_SOURCE_TREE/examples/symbianpkgrules.pri)
