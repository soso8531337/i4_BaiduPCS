#ifndef _HTTP_H
#define _HTTP_H

#define PCS_HTTP_GZIP_FILE			"/tmp/pcs_gzip.gz"
volatile int LOGOUTFLAG;

#define UPLOAD_LOGOUT_FLAG			1	 /*0001*/
#define DOWN_LOGOUT_FLAG			2	  /*0010*/
#define POLLSERV_LOGOUT_FLAG 		4        /*0100*/
#define INOTIFY_LOGOUT_FLAG			8	/*1000*/
#ifdef PCS_BACKUP
#define THREADS_LOGOUT_FLAG			(POLLSERV_LOGOUT_FLAG|DOWN_LOGOUT_FLAG | \
											INOTIFY_LOGOUT_FLAG |UPLOAD_LOGOUT_FLAG)
#else
#define THREADS_LOGOUT_FLAG			(POLLSERV_LOGOUT_FLAG|DOWN_LOGOUT_FLAG)
#endif

int cloud_send_base_err(int sock, char *value, int errnum);
int cloud_handle_web_request(char *buf, char *url, char *opt, int sockfd);

#endif
