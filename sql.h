#ifndef _SQL_H
#define _SQL_H

#include "common.h"
#include <sqlite3.h>

#define create_userTable_sqlite  \
					"CREATE TABLE IF NOT EXISTS USERS  (" \
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, " \
					"UID TEXT DEFAULT NULL , " \
					"UNAME TEXT DEFAULT NULL , " \
					"LOGIN INTEGER DEFAULT NULL , " \
					"DISKPATH TEXT DEFAULT NULL , " \
					"CURSOR TEXT DEFAULT NULL , "\
					"SYNC INTEGER DEFAULT NULL, " \
					"MULTIDOWN INTEGER DEFAULT 0, " \
					"QUOTA TEXT DEFAULT NULL , " \
					"USED TEXT DEFAULT NULL , " \
					"DL_SPEED TEXT DEFAULT NULL , " \
					"UP_SPEED TEXT DEFAULT NULL , " \
					"ACCESS_TOKEN TEXT DEFAULT NULL , " \
					"ACCESS_TOKEN_EXPIRES TEXT DEFAULT NULL , " \
					"REFRESH_TOKEN TEXT DEFAULT NULL , " \
					"REFRESH_TOKEN_EXPIRES TEXT DEFAULT NULL , " \
					"APP_KEY TEXT DEFAULT NULL , " \
					"APP_SECRET TEXT DEFAULT NULL , " \
					"CLOUD_TYPE TEXT DEFAULT NULL , " \
					"DISK_LOC TEXT DEFAULT NULL ); "

#define create_serverTable_sqlite  \
					"CREATE TABLE IF NOT EXISTS SERVER  (" \
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, " \
					"PATH TEXT DEFAULT NULL, " \
					"SIZE TEXT DEFAULT NULL , " \
					"DIR INTEGER DEFAULT NULL, " \
					"MD5 TEXT DEFAULT NULL, " \
					"PMD5 TEXT DEFAULT NULL, " \
					"CTIME TEXT DEFAULT NULL, " \
					"MTIME TEXT DEFAULT NULL);" 

#define create_localTable_sqlite  \
					"CREATE TABLE IF NOT EXISTS LOCAL  (" \
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, " \
					"PATH TEXT DEFAULT NULL, " \
					"SIZE TEXT DEFAULT NULL, " \
					"DIR INTEGER DEFAULT NULL, " \
					"MD5 TEXT DEFAULT NULL, " \
					"PMD5 TEXT DEFAULT NULL, " \
					"CTIME TEXT DEFAULT NULL, " \
					"MTIME TEXT DEFAULT NULL);" 

#define create_downloadTable_sqlite  \
					"CREATE TABLE IF NOT EXISTS DOWN  (" \
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, " \
					"PATH TEXT DEFAULT NULL, " \
					"TPATH TEXT DEFAULT NULL, " \
					"SIZE TEXT DEFAULT NULL, " \
					"DIR INTEGER DEFAULT NULL, " \
					"MD5 TEXT DEFAULT NULL, " \
					"OPT INTEGER DEFAULT NULL, " \
					"STATUS INTEGER DEFAULT NULL);"

#define create_uploadTable_sqlite  \
					"CREATE TABLE IF NOT EXISTS UP  (" \
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, " \
					"PATH TEXT DEFAULT NULL, " \
					"TPATH TEXT DEFAULT NULL, " \
					"SIZE TEXT DEFAULT NULL, " \
					"DIR INTEGER DEFAULT NULL, " \
					"MD5 TEXT DEFAULT NULL, " \
					"PMD5 TEXT DEFAULT NULL, " \
					"CRC TEXT DEFAULT NULL, " \
					"OPT INTEGER DEFAULT NULL, " \
					"STATUS INTEGER DEFAULT NULL);"

#define create_deleteTable_sqlite  \
					"CREATE TABLE IF NOT EXISTS DETABLE  (" \
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, " \
					"PATH TEXT DEFAULT NULL, " \
					"DIR INTEGER DEFAULT NULL);"

/* upload an download tmp table  */
#define create_uptmpTable_sqlite  \
					"CREATE TABLE IF NOT EXISTS UPTMP  (" \
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, " \
					"PATH TEXT DEFAULT NULL, " \
					"SIZE TEXT DEFAULT NULL, " \
					"DIR INTEGER DEFAULT NULL, " \
					"PMD5 TEXT DEFAULT NULL, " \
					"OPT INTEGER DEFAULT NULL, " \
					"STATUS INTEGER DEFAULT NULL);"

#define create_downtmpTable_sqlite  \
					"CREATE TABLE IF NOT EXISTS DOWNTMP  (" \
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, " \
					"PATH TEXT DEFAULT NULL, " \
					"SIZE TEXT DEFAULT NULL , " \
					"DIR INTEGER DEFAULT NULL, " \
					"MD5 TEXT DEFAULT NULL, " \
					"OPT INTEGER DEFAULT NULL, " \
					"STATUS INTEGER DEFAULT NULL);"

#define create_doneTable_sqlite  \
					"CREATE TABLE IF NOT EXISTS DONE  (" \
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, " \
					"PATH TEXT DEFAULT NULL, " \
					"SIZE TEXT DEFAULT NULL, " \
					"DIR INTEGER DEFAULT NULL, " \
					"MD5 TEXT DEFAULT NULL, " \
					"ACTION INTEGER DEFAULT NULL, " \
					"LTIME INTEGER DEFAULT NULL, " \
					"OPT INTEGER DEFAULT NULL, " \
					"STATUS INTEGER DEFAULT NULL);"


typedef  int (*sqlcallback)(void*, int, char**, char**);

int sql_exec(sqlite3 *db, const char *fmt, ...);
int sql_exec_with_callback(sqlite3 *db, sqlcallback callback, void *data, const char *fmt, ...);
int sql_get_table(sqlite3 *db, const char *sql, char ***pazResult, int *pnRow, int *pnColumn);
int sql_get_int_field(sqlite3 *db, int *num, const char *fmt, ...);
char * sql_get_text_field(sqlite3 *db, const char *fmt, ...);



#endif
