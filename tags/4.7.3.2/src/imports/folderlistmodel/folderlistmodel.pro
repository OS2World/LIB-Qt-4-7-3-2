TARGET  = qmlfolderlistmodelplugin
os2:TARGET_SHORT = qmlflm
TARGETPATH = Qt/labs/folderlistmodel
include(../qimportbase.pri)

QT += declarative script

SOURCES += qdeclarativefolderlistmodel.cpp plugin.cpp
HEADERS += qdeclarativefolderlistmodel.h

symbian:{
    TARGET.UID3 = 0x20021320

    isEmpty(DESTDIR):importFiles.sources = qmlfolderlistmodelplugin$${QT_LIBINFIX}.dll qmldir
    else:importFiles.sources = $$DESTDIR/qmlfolderlistmodelplugin$${QT_LIBINFIX}.dll qmldir
    importFiles.path = $$QT_IMPORTS_BASE_DIR/$$TARGETPATH
    
    DEPLOYMENT = importFiles
}
