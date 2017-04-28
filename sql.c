#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sql.h"
#include "common.h"

int sql_exec(sqlite3 *db, const char *fmt, ...)
{
	int ret = SUCCESS;
	char *errMsg = NULL;
	char *sql;
	va_list ap;
	//DPRINTF("SQL: %s\n", sql);
	if(db == NULL){
		DPRINTF("SQL db = NULL so fail here\n");
		cloud_set_errno(ESQL, EDB_NOPEN);
		return FAILURE;
	}
	va_start(ap, fmt);

	sql = sqlite3_vmprintf(fmt, ap);
	//DPRINTF("sql = %s\n", sql);
	if(sql == NULL){
		cloud_set_errno(ENOR, ENOR_OUTMEM);
		va_end(ap);
		return FAILURE;
	}
	ret = sqlite3_exec(db, sql, 0, 0, &errMsg);
	if( ret != SQLITE_OK )
	{
		DPRINTF("fail here SQL error %d [%s]\n%s\n", ret, errMsg, sql);
		if (errMsg)
			sqlite3_free(errMsg);

		cloud_set_errno(ESQL, ret);
		ret = -1;	
	}
	sqlite3_free(sql);

	va_end(ap);
	return (ret==-1? FAILURE:SUCCESS);	
}

int sql_exec_with_callback(sqlite3 *db, sqlcallback callback, void *data, const char *fmt, ...)
{
	int ret = SUCCESS;
	char *errmsg = NULL;
	char *sqlcmd = NULL;
	va_list ap;
	
	if(!db){
		cloud_set_errno(ESQL, EDB_NOPEN);
		return 0;
	}
	va_start(ap, fmt);
	sqlcmd = sqlite3_vmprintf(fmt, ap);
	if(sqlcmd == NULL){
		cloud_set_errno(ENOR, ENOR_OUTMEM);
		va_end(ap);
		return FAILURE;
	}
	ret = sqlite3_exec(db, sqlcmd, callback, data, &errmsg);
	sqlite3_free(sqlcmd);
	if (ret != SQLITE_OK 
			&& ret != SQLITE_ABORT)
	{
		DPRINTF("scan_local_table: %s\n", errmsg);
		if (errmsg)
		{
			sqlite3_free(errmsg);
		}
		cloud_set_errno(ESQL, ret);
		ret = -1;
	}
	if(ret == SQLITE_ABORT){
		DPRINTF("SQL QURERY INTERUPTED....\n");
		ret = -1;
	}
	va_end(ap);
	
	return (ret==-1? FAILURE:SUCCESS);	
}

int sql_get_table(sqlite3 *db, const char *sql, char ***pazResult, int *pnRow, int *pnColumn)
{
	int ret = SUCCESS;
	char *errMsg = NULL;
	if(db == NULL){
		DPRINTF("SQL ERROR db = NULL fail here\n");
		cloud_set_errno(ESQL, EDB_NOPEN);
		ret = FAILURE;
		return ret;
	}
	//DPRINTF("SQL: %s\n", sql);
	
	ret = sqlite3_get_table(db, sql, pazResult, pnRow, pnColumn, &errMsg);
	if( ret != SQLITE_OK)
	{
		DPRINTF("SQL ERROR %d [%s]\n%s\n", ret, errMsg, sql);
		if (errMsg)
			sqlite3_free(errMsg);
		cloud_set_errno(ESQL, ret);
		ret = FAILURE;
		return ret;
	}

	return SUCCESS;
}

int sql_get_int_field(sqlite3 *db, int *num, const char *fmt, ...)
{
	va_list		ap;
	int		counter, result;
	char		*sql;
	int		ret = SUCCESS;
	sqlite3_stmt	*stmt;
	
	if(db == NULL){
		DPRINTF("SQL ERROR db = NULL fail here\n");
		cloud_set_errno(ESQL, EDB_NOPEN);
		return FAILURE;
	}
	*num = 0;
	va_start(ap, fmt);

	sql = sqlite3_vmprintf(fmt, ap);
	if (!sql)
	{
		va_end(ap);
		cloud_set_errno(ENOR, ENOR_OUTMEM);
		return FAILURE;
	}

	//DPRINTF("sql: %s\n", sql);

	ret = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	switch (ret)
	{
		case SQLITE_OK:
			break;
		default:
			DPRINTF("prepare failed error: %s\n%s\n", sqlite3_errmsg(db), sql);
			sqlite3_free(sql);
			va_end(ap);
			cloud_set_errno(ESQL, ret);
			return FAILURE;
	}

	for (counter = 0;
	     ((result = sqlite3_step(stmt)) == SQLITE_BUSY || result == SQLITE_LOCKED) && counter < 2;
	     counter++) {
		 /* While SQLITE_BUSY has a built in timeout,
		    SQLITE_LOCKED does not, so sleep */
		 if (result == SQLITE_LOCKED)
		 	sleep(1);
	}

	switch (result)
	{
		case SQLITE_DONE:
			/* no rows returned */
			*num = 0;
			break;
		case SQLITE_ROW:
			if (sqlite3_column_type(stmt, 0) == SQLITE_NULL)
			{
				*num = 0;
				break;
			}
			*num = sqlite3_column_int(stmt, 0);
			break;
		default:
			DPRINTF("%s: step failed error: %s\n%s\n", __func__, sqlite3_errmsg(db), sql);
			ret = -1;
			cloud_set_errno(ESQL, result);
			break;
 	}

	sqlite3_free(sql);
	sqlite3_finalize(stmt);
	va_end(ap);
	
	return (ret==-1? FAILURE:SUCCESS);	
}

int sql_get_string_field(sqlite3 *db, char *val, int *size, const char *fmt, ...)
{
	va_list		ap;
	int		counter, result;
	char		*sql;
	int		ret = SUCCESS, len;
	sqlite3_stmt	*stmt;
	
	if(db == NULL){
		DPRINTF("SQL ERROR db = NULL fail here\n");
		cloud_set_errno(ESQL, EDB_NOPEN);
		return FAILURE;
	}
	*val = '\0';
	va_start(ap, fmt);

	sql = sqlite3_vmprintf(fmt, ap);
	if (!sql)
	{
		va_end(ap);
		cloud_set_errno(ENOR, ENOR_OUTMEM);
		return FAILURE;
	}

	//DPRINTF("sql: %s\n", sql);

	ret = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	switch (ret)
	{
		case SQLITE_OK:
			break;
		default:
			DPRINTF("prepare failed error: %s\n%s\n", sqlite3_errmsg(db), sql);
			sqlite3_free(sql);
			va_end(ap);
			cloud_set_errno(ESQL, ret);
			return FAILURE;
	}

	for (counter = 0;
	     ((result = sqlite3_step(stmt)) == SQLITE_BUSY || result == SQLITE_LOCKED) && counter < 2;
	     counter++) {
		 /* While SQLITE_BUSY has a built in timeout,
		    SQLITE_LOCKED does not, so sleep */
		 if (result == SQLITE_LOCKED)
		 	sleep(1);
	}

	switch (result){
		case SQLITE_DONE:
			/* no rows returned */
			*size = 0;
			break;
		case SQLITE_ROW:
			if (sqlite3_column_type(stmt, 0) == SQLITE_NULL){
				*size = 0;
				break;
			}
			len = sqlite3_column_bytes(stmt, 0);
			len = len+1 > *size?*size:len;
			memcpy(val, (char *)sqlite3_column_text(stmt, 0), len + 1);
			*size = len+1;
			break;
		default:
			DPRINTF("%s: step failed error: %s\n%s\n", __func__, sqlite3_errmsg(db), sql);
			ret = -1;
			cloud_set_errno(ESQL, result);
			break;
 	}

	sqlite3_free(sql);
	sqlite3_finalize(stmt);
	va_end(ap);
	
	return (ret==-1? FAILURE:SUCCESS);	
}


