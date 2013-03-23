TARGET = qsqlmysql
os2:TARGET_SHORT = qmysql

SOURCES = main.cpp
include(../../../sql/drivers/mysql/qsql_mysql.pri)

include(../qsqldriverbase.pri)
