TARGET = qsqlpsql
os2:TARGET_SHORT = qpsql

SOURCES = main.cpp
include(../../../sql/drivers/psql/qsql_psql.pri)

include(../qsqldriverbase.pri)
