TARGET  = qmlparticlesplugin
os2:TARGET_SHORT = qmlpar
TARGETPATH = Qt/labs/particles
include(../qimportbase.pri)

QT += declarative

SOURCES += \
    qdeclarativeparticles.cpp \
    particles.cpp

HEADERS += \
    qdeclarativeparticles_p.h

symbian:{
    TARGET.UID3 = 0x2002131E
    
    isEmpty(DESTDIR):importFiles.sources = qmlparticlesplugin$${QT_LIBINFIX}.dll qmldir
    else:importFiles.sources = $$DESTDIR/qmlparticlesplugin$${QT_LIBINFIX}.dll qmldir
    importFiles.path = $$QT_IMPORTS_BASE_DIR/$$TARGETPATH
    
    DEPLOYMENT = importFiles
}

