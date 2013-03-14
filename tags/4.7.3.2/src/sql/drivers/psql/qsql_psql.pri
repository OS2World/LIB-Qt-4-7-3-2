HEADERS += $$PWD/qsql_psql.h
SOURCES += $$PWD/qsql_psql.cpp

unix|win32-g++* {
    LIBS *= $$QT_LFLAGS_PSQL
    !contains(LIBS, .*pq.*):LIBS += -lpq
    QMAKE_CXXFLAGS *= $$QT_CFLAGS_PSQL
} else:os2 {
    INCLUDEPATH *= $$PSQL_INCLUDEPATH
    LIBS        *= $$PSQL_LIBS    
} else:win32 {
    !contains(LIBS, .*pq.*):LIBS += -llibpq -lws2_32 -ladvapi32
}
