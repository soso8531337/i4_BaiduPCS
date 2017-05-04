#ifndef _COMMON_H
#define _COMMON_H

#include <sys/types.h>
#include <sys/socket.h>

#define SUCCESS         1
#define FAILURE         0

#define SOCKTIMEO		5

#define CLOUD_NOLOGIN	2015700
#define CLOUD_NOSYNC	2015701

/* error 2015000-2015099 */
#define ENOR_OUTMEM 		2015000
#define ENOR_DISKFULL		2015001
#define ENOR_NETBAD			2015002
#define ENOR_CLDDISKFULL 	2015003
#define ENOR_DISKIOERR		2015004
#define ENOR_NODISK		2015005

#define APP_KEY			"dxXmXglZKcLL03AGjicQ5ydc"	
#define APP_SECRET		"xVLENtmVMIW2pu2r3Fl3w66RaQirPaXh"
#define DB_HEAP_SIZE (1*1024*1024) /*1MB*/
#define PID_FILE "/var/run/baidupcs.pid"                 

typedef enum {
	ESTORAGE = 1,
	ENETWORK,
	EPCS,
	ESQL,
	ENOR,
	EOTHER,
}Errtype;

/* protocol error 2015100-2015199 */
/*
 * 2015100 noraml protocol error
 * 2015101 get Dropbox path failed
 * 2015102	get user info from server failed
 * 2015103	delete failed in cloud
 * 2015104	rename failed in cloud
 *
 */

/* SQL error 2015300-2015399 */
#define EDB_NOPEN 		2015300
#define EDB_SQLERR 		2015301
#define EDB_ACCDEY	 	2015302
#define EDB_PENERR		2015303
#define EDB_EMPTY 		2015304
#define EDB_ABORT		2015305
#define EDB_DBBUSY 		2015306
#define EDB_NOMEM 		2015307
#define EDB_DBDMAGE 	2015308

/* Curl error 2015200-2015299 */
#define ECURL_ABOR_CB	2015200
#define ECURL_INIT		2015201
#define ECURL_TRYOUT 	2015202
#define ECURL_COULDNT_RESOLVE_HOST 	2015203
#define ECURL_COULDNT_CONNECT 	2015204
#define ECURL_PARTIAL_FILE 	2015205
#define ECURL_SSL_CONNECT_ERROR 	2015206
#define ECURL_WRITE_ERROR 	2015207

/* Json error 2015400-2015499 */
#define EJSON_NULL 		2015400

/* normal error 2015500 2015599 */
#define ENOR_OK 			2015500
#define ENOR_ARGS		2015501

/*Unknow Error code */
#define  UNKNOW_ERR		2016000 

/*Web socket error*/
#define EWEB_BASE_ERROR	2017000
#define EWEB_GETINFO_INIT_ERROR	2017001
#define EWEB_GETINFO_ERROR	2017002
#define EWEB_MKDIR_ERROR	2017003
#define EWEB_LOGOUT_ERROR	2017004
#define EWEB_SETSYNC_ERROR	2017005
#define EWEB_NO_USER_LOGIN	2017006
#define EWEB_GETLIST_ERROR	2017007
#define EWEB_PROG_ERROR	2017008
#define EWEB_PAUSE_ERROR		2017009
#define EWEB_ALLACT_ERROR		2017009
#define EWEB_DONELIST_ERROR	2017010
#define EWEB_DELRECORD_ERROR	2017011
#define EWEB_DONEOPT_ERROR	2017012
#define EWEB_OPTREC_ERROR	2017013
#define EWEB_SPEED_ERROR	2017014
#define EWEB_SETMULTI_ERROR	2017015
#define EWEB_SHARE_URL_ERROR	2017016
#define EWEB_PCS_RESET_ERROR	2017017
#define EWEB_PCS_REFRESH_FREQUENT		2017018
#define EWEB_SHARE_URL_FILE_EXIST	2017019

#define EWEB_LOGIN_BADUSER	2017051
#define EWEB_LOGIN_BADPASSWD	2017052
#define EWEB_LOGIN_BADNETWORK	2017053
#define EWEB_LOGIN_BADVERIFY	2017054
#define EWEB_LOGIN_FAIL	2017055


#define DPRINTF(fmt, arg...) do {log_debug(stderr, __FILE__, __FUNCTION__ ,  __LINE__, fmt, ##arg); } while (0)

int tcp_init(unsigned short port);
int tcp_accept(int sfd);
int ipc_server_init(char *path);

int ipc_pipe_init(char *path);
int check_dev(char *dev);
int check_partrdonly(char *path);
int rslash(char *src);
char *str_memstr(void *s, int slen, void *mstr, int mlen);
int socket_read(int fd, char *base, int len, const char *stop, int timeout);
int socket_write(int fd, const char *buf, int len, int timeout);
int connect_nonblock(int sockfd, struct sockaddr *addr, int slen, int nsec);

int get_value_from_url(char *url, const char *key, char *value);

void str_decode_url(const char *src, int src_len, char *dst, int dst_len);
void str_encode_url(const char *src, int src_len, char **dst);

int dm_daemon(const char *fname, const char *workdir);

int already_running(char *pidfile);
int handler_sig();
int copyFile(const char *sourceFileNameWithPath, const char *targetFileNameWithPath);  






#endif

