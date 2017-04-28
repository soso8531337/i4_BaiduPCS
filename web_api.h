#ifndef _WEB_API_H
#define _WEB_API_H
#include <string.h>
#include <stdio.h>

#define PCS_DONE_RECORD	100

enum CLOUD_OPT_STATUS{
	CLOUD_WAITING,
	CLOUD_ING,
	CLOUD_SUSPEND,
	CLOUD_PRIORITY,
	CLOUD_FAILED,
	CLOUD_RELOAD,
	CLOUD_MYDELETE	
};

enum CLOUD_OPERATON{
	CLOUD_CREATE = 10,
	CLOUD_UPDATE,
	CLOUD_MOVE,
	CLOUD_RENAME,
	CLOUD_DELETE,
	CLOUD_COPY
};

typedef enum{
	PCS_DOWN_ACTION,
	PCS_UP_ACTION
}PCS_ACTION;

typedef struct _rec_t {
	int64_t id;
	char path[1024];  /* key */ 
	char to[1024];
	char size[64];
	int dir;
	char md5[64];
	char pmd5[64]; /* pseudo md5 */	
	char crc[64]; 
	char ctime[64];
	char mtime[64];
	int opt;
	int status;
}rec_t;

typedef struct _progress_t{
	int action;	/*UPLOAD OR DOWNLOAD*/
	volatile int interp;	/*INTERUPT*/
	volatile int invaild;
	volatile int method; /*spec use multidown or normal/ rapiad,chunk,simple 20150608*/
	volatile int status; /*Some method need to compute md5 crc, or create tmp file 20150608*/
	rec_t record;
	double total;
	volatile double now;
}progress_t;


int pcs_web_api_init(void);
int pcs_web_api_login(char *username, char *password, char *verifycode);
int pcs_web_api_verifycode(char *verifycode, int codesize);
int pcs_web_api_logout(void);
int pcs_web_api_sync(void);
int pcs_web_api_setsync(void);
int pcs_web_api_link(int *login, int *sync);
int pcs_web_api_getinfo(char *username, char *quato, char *used);
int pcs_web_api_location(char *path);
int pcs_web_api_dlprog(int *dlnum, progress_t *record);
long long  pcs_web_api_getdownfile_size(char *path, char *totoal);
int pcs_web_api_getlist(const char *sql, char ***pazResult, int *pnRow, int *pnColumn);
int pcs_web_api_getdata_record(rec_t *rectp, char *fmt, ...);

int pcs_web_api_exesql(char *fmt, ...);
int pcs_web_api_dlprogress_interp(char *path);
int pcs_web_api_update_deletedb(char *path);




#endif
