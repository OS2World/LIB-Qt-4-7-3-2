TARGET  = qmlgesturesplugin
os2:TARGET_SHORT = qmlges
TARGETPATH = Qt/labs/gestures
include(../qimportbase.pri)

QT += declarative

SOURCES += qdeclarativegesturearea.cpp plugin.cpp
HEADERS += qdeclarativegesturearea_p.h

symbian:{
    TARGET.UID3 = 0x2002131F
    
    isEmpty(DESTDIR):importFiles.sources = qmlgesturesplugin$${QT_LIBINFIX}.dll qmldir
    else:importFiles.sources = $$DESTDIR/qmlgesturesplugin$${QT_LIBINFIX}.dll qmldir
    importFiles.path = $$QT_IMPORTS_BASE_DIR/$$TARGETPATH
    
    DEPLOYMENT = importFiles
}
