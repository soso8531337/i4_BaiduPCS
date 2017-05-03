#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <fcntl.h>
#include <poll.h>
#include <syslog.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <zlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/un.h>
#include <net/if.h>
#include <sys/param.h>
#include <pthread.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <curl/curl.h>	
#include <getopt.h>
#include <libgen.h>
#include <linux/limits.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include "common.h"
#include "xml.h"
#include "http.h"
#include "sql.h"
#include "web_api.h"
#include <zlib.h>


#define httphead "HTTP/1.1 200 Ok\r\n" \
				"Server: i4season\r\n" \
				"Content-Type: application/xml\r\n" \
				"Access-Control-Allow-Origin:*\r\n" \
				"Content-Length: %d\r\n" \
				"Connection: keep-alive\r\n\r\n"

#define httphead_gzip "HTTP/1.1 200 Ok\r\n" \
				"Server: i4season\r\n" \
				"Content-Type: application/xml\r\n" \
				"Access-Control-Allow-Origin:*\r\n" \
				"Content-Length: %lld\r\n" \
				"Content-Encoding: gzip\r\n"\
				"Connection: keep-alive\r\n\r\n"				

#define THREAD_LOGOUT_MAXNUM		150
#define FREE(x) do { if ((x)) {free((x)); (x)=NULL;} } while(0)

typedef struct _oper_node{
        struct _oper_node *next;
        char *name;
}oper_node;

static int Login(char *buffer, char *url, char *opt, int sockfd);
static int GetInfo(char *buffer, char *url, char *opt, int sockfd);
static int DeveAccount(char *buffer, char *url, char *opt, int sockfd);
static int AccessToken(char *buffer, char *url, char *opt, int sockfd);
static int LogOut(char *buffer, char *url, char *opt, int sockfd);
static int Sync(char *buffer, char *url, char *opt, int sockfd);
static int Link(char *buffer, char *url, char *opt, int sockfd);
static int GetStatus(char *buffer, char *url, char *opt, int sockfd);
static int GetPath(char *buffer, char *url, char *opt, int sockfd);
static int GetToken(char *buffer, char *url, char *opt, int sockfd);
static int Speed(char *buffer, char *url, char *opt, int sockfd);
static int Prog(char *buffer, char *url, char *opt, int sockfd);
static int DLProg(char *buffer, char *url, char *opt, int sockfd);
static int Getlist(char *buffer, char *url, char *opt, int sockfd);
static int Getlist2(char *buffer, char *url, char *opt, int sockfd);
static int Donelist(char *buffer, char *url, char *opt, int sockfd);
static int Pause(char *buffer, char *url, char *opt, int sockfd);
static int Allact(char *buffer, char *url, char *opt, int sockfd);
static int Delrecord(char *buffer, char *url, char *opt, int sockfd);
static int OperationRecord(char *buffer, char *url, char *opt, int sockfd);
static int DoneOperationRecord(char *buffer, char *url, char *opt, int sockfd);
static int Pcsreset(char *buffer, char *url, char *opt, int sockfd);
static int  Multi_down(char *buffer, char *url, char *opt, int sockfd);
static int Share_url(char *buffer, char *url, char *opt, int sockfd);
static int fRefresh(char *buffer, char *url, char *opt, int sockfd);



int cloud_send_base_err(int sock, char *value, const int errnum)
{
	char buf[4096] = {0};
	char buferr[128];
	char httpbuf[4096] = {0};
	int len;

	sprintf(buferr, "%d", errnum);
	
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	if(value){
		xml_add_elem(XML_ELEM_START, value, NULL, buf);
	}
	xml_add_elem(XML_LABEL, "errno", buferr, buf);
	if(value){
		xml_add_elem(XML_ELEM_END, value, NULL, buf);
	}	
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);
	
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sock, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
}

int send_func(char *header, char *opt, int sockfd, char *httpres, int reslen)
{
	char *ptr = NULL, ziplist[4096] = {0}, *httpbuf, *pcursor;
	char readbuf[4096] = {0};
	int cursor = 0, gziplen, tmplen, gfd, freelen;	
	gzFile gfp;	
	struct stat st;
		
	if(!header || !httpres){
		DPRINTF("HttpHeader or Httpres is NULL...\n");
		cloud_send_base_err(sockfd,  opt, EWEB_BASE_ERROR);
		return FAILURE;
	}
	/*Decode GZIP*/
	ptr = strstr(header, "\r\n\r\n");
	if(ptr == NULL){
		DPRINTF("HTTP Header is Too Big [More Than 4KB]...\n");
		goto send_web;
	}
	ptr = strstr(header, "Accept-Encoding:");
	if(!ptr){
		DPRINTF("No find Accept-Encoding \n");
		goto send_web;
	}
	ptr += strlen("Accept-Encoding:");
	while(*ptr != '\r' && *ptr != '\n' && cursor < 4095){
		ziplist[cursor] = *ptr;
		cursor++;
		ptr++;
	}
	if(strstr(ziplist, "gzip") == NULL &&
			strstr(ziplist, "GZIP") == NULL){
		goto send_web;
	}
	
	DPRINTF("Client Accept gzip [%s]\n", ziplist);

	gfp=gzopen(PCS_HTTP_GZIP_FILE,"w");
	if(gfp == Z_NULL){
		DPRINTF("Gzopen Failed...\n");
		goto send_web;
	}
	
	tmplen = gzwrite(gfp, httpres, reslen);
	if(tmplen != reslen){
		DPRINTF("Gzwirte wrong [%d-->%d]\n", reslen, tmplen);
		gzclose(gfp);
		goto send_web;
	}
	gzclose(gfp);
	gfd = open(PCS_HTTP_GZIP_FILE, O_RDONLY);
	if(gfd < 0 || lstat(PCS_HTTP_GZIP_FILE, &st)){
		DPRINTF("Open %s Failed..\n", PCS_HTTP_GZIP_FILE);
		close(gfd);
		goto send_web;
	}
	
	tmplen = 0;
	httpbuf = calloc(1, 4096);
	if(httpbuf == NULL){
		DPRINTF("Mallloc Memory Failed...\n");
		close(gfd);
		goto send_web;
	}
	gziplen = sprintf(httpbuf, httphead_gzip, st.st_size);
	pcursor = httpbuf+gziplen;
	freelen = 4095-gziplen;
	
	while(tmplen < st.st_size){
		memset(readbuf, 0, sizeof(readbuf));
		cursor = read(gfd, readbuf, freelen);	
		if(cursor < 0){
			if(errno == EINTR){	
				continue;
			}else{
				DPRINTF("Read %s Error....\n", PCS_HTTP_GZIP_FILE);
				close(gfd);
				free(httpbuf);
				goto send_web;
			}
		}else if(cursor == 0){
			DPRINTF("Read %s Error [Get EOF]....\n", PCS_HTTP_GZIP_FILE);
			close(gfd);
			free(httpbuf);
			goto send_web;
		}
		freelen -= cursor;
		tmplen += cursor;
		memcpy(pcursor, readbuf, cursor);
		pcursor += cursor;		
		if(freelen == 0 || tmplen == st.st_size){
			/*Send to Web*/
			if(send(sockfd, httpbuf, pcursor-httpbuf, 0) == -1){
				DPRINTF("send error! errmsg is: %s\n", strerror(errno));
				close(gfd);
				free(httpbuf);
				goto send_web;
			}
			memset(httpbuf, 0, 4096);
			pcursor= httpbuf;
			freelen = 4095;
		}
	}
	close(gfd);
	free(httpbuf);

	return SUCCESS;
	
send_web:

	httpbuf = calloc(1, 4096+reslen);
	if(httpbuf == NULL){
		DPRINTF("Mallloc Memory Failed...\n");
		return FAILURE;
	}
	tmplen = sprintf(httpbuf, httphead, reslen);
	sprintf(&httpbuf[tmplen], "%s", httpres);
	
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		free(httpbuf);
		return FAILURE;
	}
	free(httpbuf);
	return SUCCESS; 
}

/*
*URL IS ENCODE
*/
int cloud_handle_web_request(char *buf, char *url, char *opt, int sockfd)
{	
	typedef int (*web_func_callback)(char *, char *, char *, int);
	typedef struct _cloud_web_func{
		char *opt;
		web_func_callback handle_func;
	}cloud_web_func;
	cloud_web_func *cur_func = NULL;
	const cloud_web_func pcs_web_handle_list[]={
		{"Login", 				Login},
		{"GetInfo", 			GetInfo},
		{"DeveAccount", 		DeveAccount},
		{"AccessToken", 		AccessToken},
		{"LogOut", 			LogOut},
		{"Sync", 				Sync},
		{"Link", 				Link},
		{"GetStatus", 		GetStatus},
		{"GetPath", 			GetPath},
		{"GetToken", 		GetToken},
		{"Speed", 			Speed},
		{"Prog", 				Prog},
		{"DLProg", 			DLProg},
		{"Getlist", 			Getlist},
		{"Getlist2", 			Getlist2},
		{"Donelist", 			Donelist},
		{"Pause", 			Pause},
		{"Allact", 			Allact},
		{"Delrecord",			Delrecord},
		{"Operation", 		OperationRecord},
		{"Doneoperation", 	DoneOperationRecord},
		{"reset", 			Pcsreset},
		{"Multi_down", 		Multi_down},
		{"Share_url", 		Share_url},			
		{"Refresh", 			fRefresh},		
		{NULL, 				NULL}
	};
	
	if(!buf || !url || !opt){
		DPRINTF("URL or OPT is empty\n");
		cloud_send_base_err(sockfd,  opt, EWEB_BASE_ERROR);
		return FAILURE;
	}
	/*Confirm base judge*/
	cur_func = (cloud_web_func*)&pcs_web_handle_list[0];
	while(cur_func->handle_func && cur_func->opt){
		if(strcmp(opt, cur_func->opt) == 0){
			DPRINTF("Find HTTP Handle Funciton [%s]\n", cur_func->opt);
			cur_func->handle_func(buf, url, opt, sockfd);
			break;
		}
		cur_func++;
	}
	if(cur_func->handle_func == NULL || cur_func->opt == NULL){
		DPRINTF("Unknow OPT--->%s\n", opt);
		cloud_send_base_err(sockfd,  opt, EWEB_BASE_ERROR);
	}

	return SUCCESS;
}

static int 
Login(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0}, tmpbuf[1024] = {0};
	int  ret = 0;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif
	char state[64] = {0}, verifycode[1024] = {0};
	char int2buf[64] = {0};

	ret = get_value_from_url(url, "state", state);	
	if (ret == FAILURE){
		DPRINTF("Can Not Find [on] IN [%s]\n", url);
		cloud_send_base_err(sockfd,  "Login", EWEB_BASE_ERROR);
		return SUCCESS;
	}
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Login", NULL, buf);
	
	if(!strcmp(state, "login")){
		DPRINTF("Login Protocol..\n");
		char username[1024] = {0}, password[1024] = {0};
		ret = get_value_from_url(url, "username", username);	
		if (ret == FAILURE || strlen(username) == 0){
			DPRINTF("Can Not Find [username] IN [%s]\n", url);
			cloud_send_base_err(sockfd,  "Login", EWEB_BASE_ERROR);
			return SUCCESS;
		}		
		str_decode_url(username, strlen(username), username, strlen(username) + 1);
		ret = get_value_from_url(url, "password", password);	
		if (ret == FAILURE || strlen(password) == 0){
			DPRINTF("Can Not Find [password] IN [%s]\n", url);
			cloud_send_base_err(sockfd,  "Login", EWEB_BASE_ERROR);
			return SUCCESS;
		}
		str_decode_url(password, strlen(password), password, strlen(password) + 1);
		ret = pcs_web_api_login(username, password, verifycode);
		if(ret == 1){
			DPRINTF("Need Verfiycode....\n");
			xml_add_elem(XML_LABEL, "verify", "1", buf);
			xml_add_elem(XML_LABEL, "verifycode", verifycode, buf);
			ret = 0;
		}else{
			xml_add_elem(XML_LABEL, "verify", "0", buf);
			xml_add_elem(XML_LABEL, "verifycode", NULL, buf);
		}
		sprintf(int2buf, "%d", ret>= 0?ret:EWEB_LOGIN_FAIL);		
		xml_add_elem(XML_LABEL, "errno", int2buf, buf);
	}else if(!strcmp(state, "verify")){
		ret = get_value_from_url(url, "verifycode", verifycode);	
		if (ret == FAILURE){
			DPRINTF("Can Not Find [verifycode] IN [%s]\n", url);
			cloud_send_base_err(sockfd,  "Login", EWEB_BASE_ERROR);
			return SUCCESS;
		}
		str_decode_url(verifycode, strlen(verifycode), verifycode, strlen(verifycode) + 1);
		DPRINTF("User Input VerifyCode is %s\n", verifycode);
		ret = pcs_web_api_verifycode(verifycode, strlen(verifycode));
		if(ret == 1){
			xml_add_elem(XML_LABEL, "verify", "1", buf);
		}else{
			xml_add_elem(XML_LABEL, "verify", "0", buf);
		}
		sprintf(int2buf, "%d", ret>= 0?ret:EWEB_LOGIN_FAIL);		
		xml_add_elem(XML_LABEL, "errno", int2buf, buf);
	}else{
		cloud_send_base_err(sockfd,  "Login", EWEB_BASE_ERROR);
		return SUCCESS;
	}

	xml_add_elem(XML_ELEM_END, "Login", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);	

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
#endif
}


static int 
GetInfo(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0}, tmpbuf[1024] = {0};
	int  ret = 0, islogin, issync;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	
#define INIT_MAXNUM			10
	char username[256] = {0}, used[64] = {0}, quato[64] = {0};
	char localpath[4096] = {0};
	
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "GetInfo", NULL, buf);

	ret = pcs_web_api_link(&islogin, &issync);
	if(ret < 0 || islogin == 0){
		DPRINTF("Not Login.....\n");
		cloud_send_base_err(sockfd,  "GetInfo", EWEB_NO_USER_LOGIN);
		return FAILURE;
	}
	if(pcs_web_api_getinfo(username, quato, used) < 0){
		cloud_send_base_err(sockfd,  "GetInfo", EWEB_GETINFO_ERROR);
		return FAILURE;
	}
	pcs_web_api_location(localpath);


	xml_add_elem(XML_LABEL, "uname", username, buf);
	xml_add_elem(XML_LABEL, "quota", quato, buf);
	xml_add_elem(XML_LABEL, "used", used, buf);
	xml_add_elem(XML_LABEL, "dl_speed", "0", buf);	
	xml_add_elem(XML_LABEL, "up_speed", "0", buf);
	xml_add_elem(XML_LABEL, "path", localpath, buf);
	sprintf(tmpbuf, "%d",issync);
	xml_add_elem(XML_LABEL, "sync", tmpbuf, buf);
	xml_add_elem(XML_LABEL, "multi_down", "1", buf);
	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "GetInfo", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);	

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
#endif
}

static int 
DeveAccount(char *buffer, char *url, char *opt, int sockfd)
{	
	char buf[4096] = {0};
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	
	
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "DeveAccount", NULL, buf);
	xml_add_elem(XML_LABEL, "app_key", APP_KEY, buf);
	xml_add_elem(XML_LABEL, "app_secret", APP_SECRET, buf);
	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "DeveAccount", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);	

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
#endif	
}

static int 
AccessToken(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0};
	int  ret;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	

	/*Send to web*/
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "AccessToken", NULL, buf);
	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "AccessToken", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);	

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}
	return SUCCESS;
#endif

base_err:
	
	cloud_send_base_err(sockfd,  "AccessToken", EWEB_BASE_ERROR);
	
	return FAILURE;
}

static int 
LogOut(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0};
	int err = 0;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	

	if (NULL == url){
		DPRINTF("httpurl is NULL.");
		return FAILURE;
	}

	err = pcs_web_api_logout();
	/*Send to web*/
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "LogOut", NULL, buf);
	if(!err){
		xml_add_elem(XML_LABEL, "errno", "0", buf);
	}else{
		char errstr[128];
		sprintf(errstr, "%d", EWEB_LOGOUT_ERROR);
		xml_add_elem(XML_LABEL, "errno", errstr, buf);
	}
	xml_add_elem(XML_ELEM_END, "LogOut", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);	

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}
	
	return SUCCESS;
#endif	
}

static int 
Sync(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0};
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	

	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Sync", NULL, buf);	
	if(pcs_web_api_setsync() <0){
		char errstr[256];
		sprintf(errstr, "%d", EWEB_SETSYNC_ERROR);
		xml_add_elem(XML_LABEL, "errno", errstr, buf);
	}else{
		xml_add_elem(XML_LABEL, "errno", "0", buf);
	}
	xml_add_elem(XML_ELEM_END, "Sync", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
#endif	
}

static int 
Multi_down(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0};
	char multi_on[128] = {0};
	int ret;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	

	ret = get_value_from_url(url, "on", multi_on);	
	if (ret == FAILURE){
		DPRINTF("Can Not Find [on] IN [%s]\n", url);
		cloud_send_base_err(sockfd,  "Multi_down", EWEB_BASE_ERROR);
		return SUCCESS;
	}	
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Multi_down", NULL, buf);

	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "Multi_down", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
#endif	
}

static int 
Share_url(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0};
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	

	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Share_url", NULL, buf);

	xml_add_elem(XML_LABEL, "cloud_path", "shit", buf);
	xml_add_elem(XML_LABEL, "local_path", "shit", buf);
	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "Share_url", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
#endif	
}

static int 
Link(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0};
	int sync = 0, login = 0;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	

	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Link", NULL, buf);

	pcs_web_api_link(&login, &sync);
	if(login == 0){
		xml_add_elem(XML_LABEL, "link", "0", buf);
	}else{
		xml_add_elem(XML_LABEL, "link", "1", buf);
	}
	if(sync == 0){
		xml_add_elem(XML_LABEL, "sync", "0", buf);
	}else{
		xml_add_elem(XML_LABEL, "sync", "1", buf);
	}
	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "Link", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
#endif	
}

static int 
GetStatus(char *buffer, char *url, char *opt, int sockfd)
{
	return SUCCESS;
}


static int 
GetPath(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0};
	char diskpath[PATH_MAX] = {0};
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	

	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "GetPath", NULL, buf);
	pcs_web_api_location(diskpath);
	xml_add_elem(XML_LABEL, "path", diskpath, buf);
	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "GetPath", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
#endif
}

static int 
GetToken(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0};
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif
	
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "GetToken", NULL, buf);
	xml_add_elem(XML_LABEL, "access_token", "1234567890", buf);
	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "GetToken", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);	

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}
	
	return SUCCESS;
#endif	
}

static int 
Speed(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0};
	int ret;
	char upspeed[256] = {0}, dlspeed[256] = {0};
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	

	ret = get_value_from_url(url, "dl_speed", dlspeed);
	if (ret == FAILURE){
		DPRINTF("Can Not Find [dl_speed] IN [%s]\n", url);
		cloud_send_base_err(sockfd,  "Speed", EWEB_SPEED_ERROR);
		return FAILURE;
	}
	
	ret = get_value_from_url(url, "up_speed", upspeed);
	if (ret == FAILURE){
		DPRINTF("Can Not Find [up_speed] IN [%s]\n", url);
		cloud_send_base_err(sockfd,  "Speed", EWEB_SPEED_ERROR);
		return FAILURE;
	}
	
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Speed", NULL, buf);	

	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "Speed", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
#endif	
}

static int 
Prog(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0}, tmpbuf[4096] = {0};
	int ret, dnum = 0;
	progress_t recing;
	char *encodbuf = NULL;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif		
	
	if (NULL == url){
		DPRINTF("httpurl is NULL.");
		return FAILURE;
	}
	
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Prog", NULL, buf);

	memset(&recing, 0, sizeof(recing));
	if(pcs_web_api_dlprog(&dnum, &recing) < 0){
		dnum = 0;
	}
	xml_add_elem(XML_LABEL, "up_num", "0", buf);
	sprintf(tmpbuf, "%d", dnum>0?dnum:0);
	xml_add_elem(XML_LABEL, "dl_num", tmpbuf, buf);
	/*Upload information*/
	xml_add_elem(XML_ELEM_START, "up", NULL, buf);
	xml_add_elem(XML_LABEL, "method", "0", buf);		
	xml_add_elem(XML_LABEL, "status", "0", buf);		
	xml_add_elem(XML_LABEL, "path", NULL, buf);
	xml_add_elem(XML_LABEL, "uptotal", "0", buf);
	xml_add_elem(XML_LABEL, "upnow", "0", buf); 	
	xml_add_elem(XML_ELEM_END, "up", NULL, buf);
	/*Download Information*/
	xml_add_elem(XML_ELEM_START, "dl", NULL, buf);

	sprintf(tmpbuf, "%d", recing.method);
	xml_add_elem(XML_LABEL, "method", tmpbuf, buf);
	sprintf(tmpbuf, "%d", recing.status);
	xml_add_elem(XML_LABEL, "status", tmpbuf, buf); 
	str_encode_url(recing.record.path, strlen(recing.record.path), &encodbuf);
	xml_add_elem(XML_LABEL, "path", encodbuf, buf);
	FREE(encodbuf);
	if(!strlen(recing.record.size)
		||!atoll(recing.record.size)){
		xml_add_elem(XML_LABEL, "dltotal", "0", buf);
	}else{
		xml_add_elem(XML_LABEL, "dltotal", recing.record.size, buf);
	}
	sprintf(tmpbuf, "%.0f", recing.now);
	xml_add_elem(XML_LABEL, "dlnow", tmpbuf, buf);
	xml_add_elem(XML_ELEM_END, "dl", NULL, buf);
	
	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "Prog", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else	
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}	
	return SUCCESS;
#endif	
}

static int 
DLProg(char *buffer, char *url, char *opt, int sockfd)
{
	return SUCCESS;
}

static int 
Getlist(char *buffer, char *url, char *opt, int sockfd)
{
	char start[16] = {0}, count[16] = {0}, oplist[16] = {0};
	int ret, memlen, cnum, ning = 0, memcnt, cstart, nnum = 0;
	char *buf = NULL, *encodbuf, *sql = NULL;
	progress_t recing;
	char tmpbuf[128];
	char **result;
	int rows = 0, cols = 0, cur = 0, status, uptalbe=0;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char *httpbuf = NULL;
#endif		
	
	if (NULL == url){
		DPRINTF("httpurl is NULL.");
		return FAILURE;
	}

	str_decode_url(url, strlen(url), url, strlen(url) + 1);
	ret = get_value_from_url(url, "start", start);
	if (ret == FAILURE){
		DPRINTF("Can Not Find [start] IN [%s]\n", url);
		goto base_err;
	}
	ret = get_value_from_url(url, "count", count);
	if (ret == FAILURE){
		DPRINTF("Can Not Find [count] IN [%s]\n", url);
		goto base_err;
	}
	ret = get_value_from_url(url, "list", oplist);
	if (ret == FAILURE){
		DPRINTF("Use Default List DOWN Table\n");	
	}else if(strcmp(oplist, "uptable") == 0){
		DPRINTF("Get UpTable File List..\n");
		uptalbe = 1;
	}else if(strcmp(oplist, "downtable") == 0){
		uptalbe = 0;
	}
	
	cnum = atoi(count);
	cstart = atoi(start);
	buf = calloc(1, 4096);
	if(buf == NULL){
		DPRINTF("Calloc Memory Failed....\n");
		goto base_err;
	}
	memcnt =1;
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Getlist", NULL, buf);
	
	memset(&recing, 0, sizeof(progress_t));
	if(uptalbe == 1){
		memset(&recing, 0, sizeof(recing));
	}else{
		int dnum = 0;
		if(pcs_web_api_dlprog(&dnum, &recing) < 0){
			dnum = 0;
			memset(&recing, 0, sizeof(recing));			
			ning = 0;
		}else{
			ning = 1;
		}
	}

	xml_add_elem(XML_ELEM_START, "list", NULL, buf);
	str_encode_url(recing.record.path, strlen(recing.record.path), &encodbuf);
	xml_add_elem(XML_LABEL, "path", encodbuf, buf);
	FREE(encodbuf);
	if(!strlen(recing.record.size)
		||!atoll(recing.record.size)){	
		xml_add_elem(XML_LABEL, "size", "0", buf);
	}else{
		xml_add_elem(XML_LABEL, "size", recing.record.size, buf);
	}
	sprintf(tmpbuf, "%.0f", recing.now);
	if(uptalbe == 1){
		xml_add_elem(XML_LABEL, "up_now", tmpbuf, buf);
	}else{
		xml_add_elem(XML_LABEL, "dl_now", tmpbuf, buf);
	}
	xml_add_elem(XML_LABEL, "status", "ING", buf);
	xml_add_elem(XML_ELEM_END, "list", NULL, buf);
	
	if(ning == 1){
		sql = sqlite3_mprintf("SELECT PATH, SIZE, STATUS FROM '%q' " 
			" WHERE DIR = '%d' AND STATUS != %d LIMIT %d OFFSET %d", 
				uptalbe == 1?"UP":"DOWN", 0, CLOUD_ING, 
				cnum > 50?50:cnum, cstart == 0?cstart: (cstart-1));
	}else{
		sql = sqlite3_mprintf("SELECT PATH, SIZE, STATUS FROM '%q' " 
			" WHERE DIR = '%d'	LIMIT %d OFFSET %d", 
				uptalbe == 1?"UP":"DOWN", 0, 
			cnum > 50?50:cnum, cstart == 0?cstart: (cstart-1)); 
	}

	ret = pcs_web_api_getlist(sql, &result, &rows, &cols);
	if(uptalbe == 0 && ret == 0 && rows){
		for ( cur = cols; cur<= (rows ) * cols; cur += cols){
			status = atoi(result[cur + 2]);
			if (status == CLOUD_RELOAD ||
					status == CLOUD_MYDELETE){
				continue;
			}
			/*Check Memory*/
			memlen = strlen(buf);
			if( memcnt*4096 - memlen < 4096){
				DPRINTF("Realloc Memory To %d*4096B..\n", ++memcnt);
				if (!(buf = realloc(buf, memcnt* 4096 ))){
						DPRINTF("Realloc Memory Failed....\n");
						FREE(buf);
						sqlite3_free_table(result); 		
						sqlite3_free(sql);
						cloud_send_base_err(sockfd,  "Getlist", EWEB_GETLIST_ERROR);	
						return FAILURE;
				}			
			}
			xml_add_elem(XML_ELEM_START, "list", NULL, buf);
			str_encode_url(result[cur], strlen(result[cur]), &encodbuf);
			xml_add_elem(XML_LABEL, "path", encodbuf, buf);
			FREE(encodbuf);
			xml_add_elem(XML_LABEL, "size", result[cur+1], buf);
			memset(tmpbuf, 0, sizeof(tmpbuf));
			if(uptalbe == 1){
				xml_add_elem(XML_LABEL, "up_now", "0", buf);
			}else{
				sprintf(tmpbuf, "%lld", pcs_web_api_getdownfile_size(result[cur], result[cur+1]));
				xml_add_elem(XML_LABEL, "dl_now", tmpbuf, buf);
			}
			switch(status){
				case 0: 
					xml_add_elem(XML_LABEL, "status", "WAIT", buf);
					break;
				case 1: 
					xml_add_elem(XML_LABEL, "status", "ING", buf);
					break;
				case 2: 
					xml_add_elem(XML_LABEL, "status", "PAUSE", buf);
					break;
				case 4: 
					xml_add_elem(XML_LABEL, "status", "FAILED", buf);
					break;
				default:
					DPRINTF("UnRegnize Status [%d]....\n", status);
					xml_add_elem(XML_LABEL, "status", "WAIT", buf);
					break;
			}
			xml_add_elem(XML_ELEM_END, "list", NULL, buf);
			if(uptalbe == 0){
				nnum++;
			}
		}
	}
	if(ret == 0){
		sqlite3_free_table(result);
	}
	sqlite3_free(sql);
	
	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "Getlist", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		FREE(buf);
		return FAILURE;
	}
	FREE(buf);
	return SUCCESS;
#else
	httpbuf = calloc(1, strlen(buf)+4096);
	if(httpbuf == NULL){
		DPRINTF("Memory calloc Failed....\n");
		FREE(buf);
		cloud_send_base_err(sockfd,  "Getlist", EWEB_GETLIST_ERROR);	
		return FAILURE; 	
	}
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("Send To Web --->%d Bytes\n", strlen(httpbuf));
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}
	FREE(buf);
	FREE(httpbuf);

	return SUCCESS;
#endif
	
base_err:
	
	cloud_send_base_err(sockfd,  "Getlist", EWEB_BASE_ERROR);
	
	return FAILURE;
}

static int 
Getlist2(char *buffer, char *url, char *opt, int sockfd)
{
	char start[PATH_MAX] = {0}, count[16] = {0}, diskpath[PATH_MAX]= {0};
	char oplist[16] = {0};
	int ret, memlen, cnum, ning = 0, memcnt;
	char *buf = NULL, *encodbuf, *sql = NULL;
	progress_t recing;
	char tmpbuf[128];
	char **result;	
	rec_t qrecord;
	int rows = 0, cols = 0, cur = 0, status, uptable = 0;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char *httpbuf = NULL;
#endif		
	
	if (NULL == url){
		DPRINTF("httpurl is NULL.");
		return FAILURE;
	}
	
	ret = get_value_from_url(url, "start", start);
	if (ret == FAILURE){
		DPRINTF("Can Not Find [start] IN [%s]\n", url);
		goto base_err;
	}	
	str_decode_url(start, strlen(start), start, strlen(start) + 1);
	ret = get_value_from_url(url, "count", count);
	if (ret == FAILURE){
		DPRINTF("Can Not Find [count] IN [%s]\n", url);
		goto base_err;
	}
	ret = get_value_from_url(url, "list", oplist);
	if (ret == FAILURE){
		DPRINTF("Use Default List DOWN Table\n");	
	}else if(strcmp(oplist, "uptable") == 0){
		DPRINTF("Get UpTable File List..\n");
		uptable = 1;
	}else if(strcmp(oplist, "downtable") == 0){
		uptable = 0;
	}
	cnum = atoi(count);
	buf = calloc(1, 4096);
	if(buf == NULL){
		DPRINTF("Calloc Memory Failed....\n");
		goto base_err;
	}
	memcnt =1;
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Getlist", NULL, buf);
	memset(&recing, 0, sizeof(progress_t));

	if(uptable == 0 &&sql_get_data_record(&qrecord, 
			"SELECT * FROM '%q' WHERE PATH='%q'", uptable == 1?"UP":"DOWN", 
			start) == 0 && qrecord.id == 0){			
		int dnum = 0;
		if(pcs_web_api_dlprog(&dnum, &recing) == 0){
			xml_add_elem(XML_ELEM_START, "list", NULL, buf);
			str_encode_url(recing.record.path, strlen(recing.record.path), &encodbuf);
			xml_add_elem(XML_LABEL, "path", encodbuf, buf);
			FREE(encodbuf);
			if(!strlen(recing.record.size)
				||!atoll(recing.record.size)){	
				xml_add_elem(XML_LABEL, "size", "0", buf);
			}else{
				xml_add_elem(XML_LABEL, "size", recing.record.size, buf);
			}			
			sprintf(tmpbuf, "%.0f", recing.now);
			if(uptable == 1){
				xml_add_elem(XML_LABEL, "up_now", tmpbuf, buf);
			}else{
				xml_add_elem(XML_LABEL, "dl_now", tmpbuf, buf);
			}
			xml_add_elem(XML_LABEL, "status", "ING", buf);
			xml_add_elem(XML_ELEM_END, "list", NULL, buf);
			ning = 1;
		}else{
			ning = 0;
		}
	}

	if(ning == 1){
		sql = sqlite3_mprintf("SELECT PATH, SIZE, STATUS FROM '%q' " 
			" WHERE DIR = '%d' AND STATUS != %d LIMIT %d", 
				uptable == 1?"UP":"DOWN", 0, 
				CLOUD_ING, cnum > 50?50:cnum);
	}else{
		sql = sqlite3_mprintf("SELECT PATH, SIZE, STATUS FROM '%q' " 
			" WHERE DIR = '%d'	AND ID > '%lld'  AND STATUS != %d  LIMIT %d ", 
				uptable == 1?"UP":"DOWN", 0, 
				qrecord.id, CLOUD_ING, cnum > 50?50:cnum);	
	}
	if(sql == NULL){
		DPRINTF("Sql MPRINTF Memory Failed\n");
		FREE(buf);
		cloud_send_base_err(sockfd,  "Getlist", EWEB_GETLIST_ERROR);	
		return FAILURE;
	}
	ret = pcs_web_api_getlist(sql, &result, &rows, &cols);
	if(uptable == 0 && ret == 0 && rows){
		for ( cur = cols; cur<= (rows ) * cols; cur += cols){
			status = atoi(result[cur + 2]);
			if (status == CLOUD_RELOAD ||
					status == CLOUD_MYDELETE){
				continue;
			}
			/*Check Memory*/
			memlen = strlen(buf);
			if( memcnt*4096 - memlen < 4096){
				DPRINTF("Realloc Memory To %d*4096B..\n", ++memcnt);
				if (!(buf = realloc(buf, memcnt* 4096 ))){
						DPRINTF("Realloc Memory Failed....\n");
						FREE(buf);
						sqlite3_free_table(result); 		
						sqlite3_free(sql);
						cloud_send_base_err(sockfd,  "Getlist", EWEB_GETLIST_ERROR);	
						return FAILURE;
				}			
			}
			xml_add_elem(XML_ELEM_START, "list", NULL, buf);
			str_encode_url(result[cur], strlen(result[cur]), &encodbuf);
			xml_add_elem(XML_LABEL, "path", encodbuf, buf);
			FREE(encodbuf);
			xml_add_elem(XML_LABEL, "size", result[cur+1], buf);
			memset(tmpbuf, 0, sizeof(tmpbuf));
			if(uptable == 1){
				xml_add_elem(XML_LABEL, "up_now", "0", buf);
			}else{
				sprintf(tmpbuf, "%lld", pcs_web_api_getdownfile_size(result[cur], result[cur+1]));
				xml_add_elem(XML_LABEL, "dl_now", tmpbuf, buf);
			}
			switch(status){
				case 0: 
					xml_add_elem(XML_LABEL, "status", "WAIT", buf);
					break;
				case 1: 
					xml_add_elem(XML_LABEL, "status", "ING", buf);
					break;
				case 2: 
					xml_add_elem(XML_LABEL, "status", "PAUSE", buf);
					break;
				case 4: 
					xml_add_elem(XML_LABEL, "status", "FAILED", buf);
					break;
				default:
					DPRINTF("UnRegnize Status [%d]....\n", status);
					xml_add_elem(XML_LABEL, "status", "WAIT", buf);
					break;
			}
			xml_add_elem(XML_ELEM_END, "list", NULL, buf);
		}
	}
	if(ret == SUCCESS){
		sqlite3_free_table(result);
	}
	sqlite3_free(sql);

	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "Getlist", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		FREE(buf);
		return FAILURE;
	}
	FREE(buf);
	return SUCCESS;
#else
	httpbuf = calloc(1, strlen(buf)+4096);
	if(httpbuf == NULL){
		DPRINTF("Memory calloc Failed....\n");
		FREE(buf);
		cloud_send_base_err(sockfd,  "Getlist", EWEB_GETLIST_ERROR);	
		return FAILURE; 	
	}
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("Send To Web --->%d Bytes\n", strlen(httpbuf));
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}
	FREE(buf);
	FREE(httpbuf);

	return SUCCESS;
#endif
	
base_err:
	
	cloud_send_base_err(sockfd,  opt, EWEB_BASE_ERROR);
	
	return FAILURE;
}

static int 
Donelist(char *buffer, char *url, char *opt, int sockfd)
{
	int ret, memlen, memcnt;
	char *buf = NULL, *encodbuf, *sql = NULL;
	char **result;
	int rows = 0, cols = 0, cur = 0, uptale =0;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char *httpbuf = NULL;
#endif
	char oplist[16]= {0};
	
	if (NULL == url){
		DPRINTF("httpurl is NULL.");
		return FAILURE;
	}

	ret = get_value_from_url(url, "list", oplist);
	if (ret == FAILURE){
		DPRINTF("Use Default List DOWN Table\n");	
	}else if(strcmp(oplist, "uptable") == 0){
		DPRINTF("Get UpTable File List..\n");
		uptale = 1;
	}else if(strcmp(oplist, "downtable") == 0){
		uptale = 0;
	}
	buf = calloc(1, 4096);
	if(buf == NULL){
		DPRINTF("Calloc Memory Failed....\n");
		goto base_err;
	}
	memcnt =1;
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Donelist", NULL, buf);
	

	sql = sqlite3_mprintf("SELECT PATH, SIZE, ACTION, LTIME, STATUS FROM DONE" 
		" WHERE ACTION = '%d'	ORDER BY ID DESC LIMIT %d", 
			uptale == 1?PCS_UP_ACTION : PCS_DOWN_ACTION, PCS_DONE_RECORD);	
	if(sql == NULL){
		DPRINTF("%s \n", sql==NULL?"Sql MPRINTF Memory Failed":"DB Not Open");
		FREE(buf);
		cloud_send_base_err(sockfd,  "Donelist", EWEB_DONELIST_ERROR);	
		return FAILURE;
	}
	ret = pcs_web_api_getlist(sql, &result, &rows, &cols);	
	if(ret == 0 && rows){
		for ( cur = cols; cur<= (rows ) * cols; cur += cols){
			/*Check Memory*/
			memlen = strlen(buf);
			if( memcnt*4096 - memlen < 4096){
				DPRINTF("Realloc Memory To %d*4096B..\n", ++memcnt);
				if (!(buf = realloc(buf, memcnt* 4096 ))){
						DPRINTF("Realloc Memory Failed....\n");
						FREE(buf);
						sqlite3_free_table(result); 		
						sqlite3_free(sql);
						cloud_send_base_err(sockfd,  "Donelist", EWEB_DONELIST_ERROR);	
						return FAILURE;
				}			
			}
			xml_add_elem(XML_ELEM_START, "list", NULL, buf);
			str_encode_url(result[cur], strlen(result[cur]), &encodbuf);
			xml_add_elem(XML_LABEL, "path", encodbuf, buf);
			FREE(encodbuf);
			xml_add_elem(XML_LABEL, "size", result[cur+1], buf);
			xml_add_elem(XML_LABEL, "action", result[cur+2] , buf);
			xml_add_elem(XML_LABEL, "ltime", result[cur+3], buf);
			xml_add_elem(XML_LABEL, "status", result[cur+4], buf);			
			xml_add_elem(XML_ELEM_END, "list", NULL, buf);
		}
	}
	if(ret == SUCCESS){
		sqlite3_free_table(result);
	}
	sqlite3_free(sql);

	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "Donelist", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		FREE(buf);
		return FAILURE;
	}
	FREE(buf);
	return SUCCESS;
#else
	httpbuf = calloc(1, strlen(buf)+4096);
	if(httpbuf == NULL){
		DPRINTF("Memory calloc Failed....\n");
		FREE(buf);
		cloud_send_base_err(sockfd,  "Donelist", EWEB_DONELIST_ERROR);	
		return FAILURE; 	
	}
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("Send To Web --->%d Bytes\n", strlen(httpbuf));
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}
	FREE(buf);
	FREE(httpbuf);

	return SUCCESS;
#endif
	
base_err:
	
	cloud_send_base_err(sockfd,  opt, EWEB_BASE_ERROR);
	
	return FAILURE;
}

static int 
Pause(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0}, path[4096] = {0}, action[256] = {0};
	int ret;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif		
	int havewait = 0, waitnum = 0;
	char tmpfile[PATH_MAX] = {0};
	progress_t recing;
	char *encodbuf = NULL;
	
	if (NULL == url){
		DPRINTF("httpurl is NULL.");
		return FAILURE;
	}
	ret = get_value_from_url(url, "path", path);
	if (ret == FAILURE){
		DPRINTF("Can Not Find [path] IN [%s]\n", url);
		goto base_err;
	}	
	str_decode_url(path, strlen(path), path, strlen(path) + 1);
	ret = get_value_from_url(url, "act", action);
	if (ret == FAILURE){
		DPRINTF("Can Not Find [count] IN [%s]\n", url);
		goto base_err;
	}
	DPRINTF("Down Operation--->[%s]==>[%s]\n", path, action);

	if(strcmp(action, "start") == 0){
		/*Set The record To Start*/
		pcs_web_api_exesql("UPDATE DOWN SET	STATUS='%d' "
				" WHERE DIR=%d	AND PATH='%q'", CLOUD_WAITING, 0, path);		
	}else if(strcmp(action, "pause") == 0){
		if(pcs_web_api_dlprogress_interp(path) == 2){
			DPRINTF("Pause ING Record [%s]...\n", path);
			havewait = 1;
		}
		/*Set The record To PAUSE*/
		pcs_web_api_exesql("UPDATE DOWN SET	STATUS='%d' "
				" WHERE DIR=%d	AND PATH='%q'", CLOUD_SUSPEND, 0, path);
	}else{
		DPRINTF("Unknow Action=%s....\n", action);
		goto base_err;
	}

	while(havewait == 1 && waitnum < 100){
		int num;
		progress_t prg;
		memset(&prg, 0, sizeof(prg));
		pcs_web_api_dlprog(&num, &prg);
		if(strcmp(tmpfile, prg.record.path) != 0){
			DPRINTF("Wait DownLoad Thread Handle Finish....\n");
			break;
		}
		usleep(100000);
		waitnum++;
	}

exe_ok:
	
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Pause", NULL, buf);
	str_encode_url(path, strlen(path), &encodbuf);
	xml_add_elem(XML_LABEL, "path", encodbuf, buf);
	FREE(encodbuf);
	xml_add_elem(XML_LABEL, "status", action, buf);
	if(waitnum == 100){
		char tmpbuf[1024];
		sprintf(tmpbuf, "%d", EWEB_PAUSE_ERROR);
		xml_add_elem(XML_LABEL, "errno", tmpbuf, buf);		
	}else{
		xml_add_elem(XML_LABEL, "errno", "0", buf);
	}
	xml_add_elem(XML_ELEM_END, "Pause", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);
	
#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}	
	return SUCCESS;
#else	
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}	
	return SUCCESS;
#endif

base_err:
	
	cloud_send_base_err(sockfd,  opt, EWEB_BASE_ERROR);	
	return FAILURE;
}

static int 
Allact(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0}, action[256] = {0};
	int ret;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif		
	
	if (NULL == url){
		DPRINTF("httpurl is NULL.");
		return FAILURE;
	}
	ret = get_value_from_url(url, "act", action);
	if (ret == FAILURE){
		DPRINTF("Can Not Find [action] IN [%s]\n", url);
		goto base_err;
	}
	
	DPRINTF("%s All Record...\n", action);
	if(strcmp(action, "start") == 0){
		ret = pcs_web_api_exesql("UPDATE DOWN SET  STATUS='%d' "
				" WHERE  STATUS='%d' OR STATUS='%d'", 
				CLOUD_WAITING, CLOUD_SUSPEND, CLOUD_FAILED);
	}else if(strcmp(action, "pause") == 0){
		pcs_web_api_dlprogress_interp(NULL);
		ret = pcs_web_api_exesql("UPDATE DOWN SET  STATUS='%d' "
			" WHERE  STATUS='%d' OR STATUS='%d' OR STATUS='%d'", 
			CLOUD_SUSPEND, CLOUD_WAITING, CLOUD_FAILED, CLOUD_ING); 
	}else{
		DPRINTF("UNknow Allacion OPT [%s]....\n", action);
		goto base_err;
	}	

	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Allact", NULL, buf);
	xml_add_elem(XML_LABEL, "status", action, buf);
	if(ret == 0){
		xml_add_elem(XML_LABEL, "errno", "0", buf);
	}else{	
		char tmpbuf[1024] = {0};
		sprintf(tmpbuf, "%d", EWEB_ALLACT_ERROR);
		xml_add_elem(XML_LABEL, "errno", tmpbuf, buf);
	}
	
	xml_add_elem(XML_ELEM_END, "Allact", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}	
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}	
	return SUCCESS; 
#endif

base_err:
	
	cloud_send_base_err(sockfd,  opt, EWEB_BASE_ERROR);

	return FAILURE;
}

static int 
Delrecord(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0}, action[256] = {0}, path[PATH_MAX] = {0};
	int ret;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif		
	
	if (NULL == url){
		DPRINTF("httpurl is NULL.");
		return FAILURE;
	}
	ret = get_value_from_url(url, "act", action);
	if (ret == FAILURE){
		DPRINTF("Can Not Find [action] IN [%s]\n", url);
		goto base_err;
	}
	
	DPRINTF("%s Delete Record...\n", action);
	if(strcmp(action, "all") == 0){
		ret = pcs_web_api_exesql("DELETE FROM DONE");
	}else if(strcmp(action, "single") == 0){
		ret = get_value_from_url(url, "path", path);
		if (ret == FAILURE){
			DPRINTF("Can Not Find [path] IN [%s]\n", url);
			goto base_err;
		}		
		str_decode_url(path, strlen(path), path, strlen(path) + 1);
		ret = pcs_web_api_exesql("DELETE FROM DONE WHERE PATH='%q'", path); 
	}else{
		DPRINTF("UNknow Delrecord OPT [%s]....\n", action);
		goto base_err;
	}	

	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Delrecord", NULL, buf);
	if(ret == 0){
		xml_add_elem(XML_LABEL, "errno", "0", buf);
	}else{	
		char tmpbuf[1024] = {0};
		sprintf(tmpbuf, "%d", EWEB_DELRECORD_ERROR);
		xml_add_elem(XML_LABEL, "errno", tmpbuf, buf);
	}	
	xml_add_elem(XML_ELEM_END, "Delrecord", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}	
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}	
	return SUCCESS; 
#endif

base_err:
	
	cloud_send_base_err(sockfd,  opt, EWEB_BASE_ERROR);

	return FAILURE;
}

static void free_oper_node(oper_node *list)
{
	oper_node *cur, *tmp;

	if(list == NULL)
		return;
	cur = list;
	while(cur){
		tmp = cur;
		cur = cur->next;
		if(tmp->name){
			free(tmp->name);
			tmp->name = NULL;
		}	
		free(tmp);
		tmp = NULL;
	}
}

static oper_node *Hanlde_post_content_for_operation(int cfd, char *buf, int ret)
{
	char *p,*pp, *httpres = NULL, *curp = NULL;
	int contlen, headlen, already, filenum;
	int freelen, rc, tmplen = 0;
	oper_node *oper_list = NULL, *node;
#define MALLOC_LEN 4096	

	if(!buf){
		return NULL;
	}
	p = strstr(buf, "Content-Length:");
	if(!p){
		DPRINTF("No find Content-Length \n");
		return NULL;
	}
	p += strlen("Content-Length:");
	contlen = atoi(p);	
	
	p = strstr(buf, "\r\n\r\n");
	if(!p){
		DPRINTF("No find http requst ending\n");
		return NULL;
	}
	p += strlen("\r\n\r\n");
	headlen = p -buf;	
	already = ret - headlen;
		
	httpres = calloc(1, MALLOC_LEN);
	if(!httpres){
		DPRINTF("calloc failed\n");
		return NULL;
	}
	curp = httpres;
	memcpy(curp, p, already);
	curp += already;
	freelen = MALLOC_LEN-already;
	DPRINTF("already=%d  contlen=%d\n", already, contlen);
	while(already < contlen){
		rc = recv(cfd, curp, freelen, 0);
                if( rc == -1 || rc == 0)
                {
                        DPRINTF("recv error or 0!\n");/* sometimes error here */
			FREE(httpres);
			return NULL;
                }
		DPRINTF("Read %d bytes\n", rc);		
		already += rc;
		freelen -= rc;
		if(freelen == 0){
			httpres = realloc(httpres, already+MALLOC_LEN);		
			freelen = MALLOC_LEN;
			curp = httpres+already;
		}else{
                        curp += rc; //add by zhangwei 20150623
                }
	}	
	DPRINTF("post data read finish already=%d  contlen=%d\n", already, contlen);
	/*read finish, decord http request*/
	if(strncmp(httpres, "number=", strlen("number="))){
		DPRINTF("HTTP POST request format wrong\n");
		FREE(httpres);
		return NULL;
	}
	p = httpres + strlen("number=");
	filenum = atoi(p);
	while(p && (p-httpres) <= already){
		if(*p == '\r' && *(p+1) == '\n'){
			break;
		}
		p++;
	}		
	if(p-httpres >= already){
		DPRINTF("HTTP POST decode wrong\n");
		FREE(httpres);
		return NULL;
	}
	p += strlen("\r\n");
	curp = p;
	while(curp && filenum){
		if(curp-httpres >= contlen){
			DPRINTF("Read finish must stop  filenum=%d\n", filenum);
			break;
		}
		if(strncmp(curp, "path=", 5) || 
			(pp = strstr(curp, "\r\n")) == NULL){
			DPRINTF("Path is wrong format [%s]\n", curp);
			FREE(httpres);
			free_oper_node(oper_list);
			return NULL;
		}
		curp +=strlen("path=");
		node = calloc(1, sizeof(oper_node));	
		if(node == NULL){
			FREE(httpres);
			free_oper_node(oper_list);
			return NULL;
		}
		node->name = calloc(1, pp-curp+1);
		if(node->name == NULL){
			FREE(node);
			FREE(httpres);
			free_oper_node(oper_list);
			return NULL;
		}
		node->next = NULL;
		memcpy(node->name, curp, pp-curp);
		str_decode_url(node->name, pp-curp, node->name, pp-curp + 1);
		if(oper_list == NULL){
			oper_list = node;
		}else{
			node->next = oper_list;
			oper_list = node;
		}
		//DPRINTF("Insert Node [%s] successful\n", node->name);
		curp = pp +strlen("\r\n");	
		filenum--;
		tmplen++;
	}
	DPRINTF("Handle finish [handle %d node]...............\n", tmplen);
	return oper_list;	
}

static int 
OperationRecord(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0}, action[256] = {0}, lpath[PATH_MAX];
	char tmpbuf[PATH_MAX];
	int ret, optval = 0;
	oper_node *oper_list, *oper_cur;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	


	if (NULL == url || buffer == NULL){
		DPRINTF("httpurl is NULL.");
		return FAILURE;
	}
	ret = get_value_from_url(url, "action", action);	
	if (ret == FAILURE){
		DPRINTF("Can Not Find [action] IN [%s]\n", url);
		goto base_err;
	}
	if(strcmp(action, "start") == 0){
			optval = CLOUD_WAITING;
	}else if(strcmp(action, "pause") == 0){
			optval = CLOUD_SUSPEND;
	}else if(strcmp(action, "delete") == 0){
			optval = -1;
	}else{
		DPRINTF("Unkonw OPT [%s]\n", action);
		cloud_send_base_err(sockfd,  "Operation", EWEB_OPTREC_ERROR);
		return FAILURE;
	}
	
	DPRINTF("Operation %s Record...\n", action);
	oper_list = Hanlde_post_content_for_operation(sockfd, buffer, strlen(buffer));
	if(oper_list == NULL){
		DPRINTF("Get Operation Record Failed...\n");
		cloud_send_base_err(sockfd,  "Operation", EWEB_OPTREC_ERROR);
		return FAILURE;
	}
	oper_cur = oper_list;
	while(oper_cur){
		DPRINTF("Operation Execute %s [opt=%s]\n", oper_cur->name, action);
		if(oper_cur->name == NULL || strlen(oper_cur->name) == 0){
			oper_cur = oper_cur->next;
			continue;
		}
		if(optval >= CLOUD_WAITING){
			/*Update to Waiting....*/
			ret = pcs_web_api_exesql("UPDATE DOWN SET STATUS='%d' "
					" WHERE DIR=%d AND PATH='%q'", optval, 0, oper_cur->name);
			if(ret != 0){
				DPRINTF("UPDATE RECORD Status [%s] Failed\n",  oper_cur->name);
				break;
			}
		}else if(optval == -1){
			if(pcs_web_api_update_deletedb(oper_cur->name) != 0){
				DPRINTF("UPDATE DELTABLE Failed:[%s]\n",  oper_cur->name);
				break;
			}
		}else{
			DPRINTF("Unhandle optval=%d\n", optval);
		}
		oper_cur = oper_cur->next;
	}
	
	free_oper_node(oper_list);
	
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Operation", NULL, buf);	
	xml_add_elem(XML_LABEL, "action", action, buf);
	if(ret == FAILURE){
		sprintf(tmpbuf, "%d", EWEB_OPTREC_ERROR);
		xml_add_elem(XML_LABEL, "errno", tmpbuf, buf);
	}else{
		xml_add_elem(XML_LABEL, "errno", "0", buf);
	}
	xml_add_elem(XML_ELEM_END, "Operation", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
		if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
			return FAILURE;
		}	
		return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}	
	return SUCCESS; 
#endif

base_err:
	
	cloud_send_base_err(sockfd,  opt, EWEB_BASE_ERROR);

	return FAILURE;
}

static int 
DoneOperationRecord(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0}, action[256] = {0}, spath[PATH_MAX];
	int ret;
	int truedel = 0;
	oper_node *oper_list, *oper_cur;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	
	
	if (NULL == url || buffer == NULL){
		DPRINTF("httpurl is NULL.");
		return FAILURE;
	}

	ret = get_value_from_url(url, "action", action);	
	truedel = atoi(action);
	if (ret == FAILURE || truedel > 1){
		DPRINTF("Can Not Find [action=%d] IN [%s]\n", truedel, url);
		goto base_err;
	}
	
	DPRINTF("Delete %s Record...\n",truedel== 1?"ALL":"Record");
	oper_list = Hanlde_post_content_for_operation(sockfd, buffer, strlen(buffer));
	if(oper_list == NULL){
		DPRINTF("Get List Failed...\n");
		cloud_send_base_err(sockfd,  "Doneoperation", EWEB_DONEOPT_ERROR);
		return FAILURE;
	}
	oper_cur = oper_list;
	while(oper_cur){
		DPRINTF("Done Operation Execute %s [opt=%d]\n", oper_cur->name, truedel);
		pcs_web_api_exesql("DELETE FROM DONE WHERE PATH = '%q' ", oper_cur->name);
		if(truedel == 1 && access(oper_cur->name, F_OK) == 0){
				remove(oper_cur->name);
		}
		oper_cur = oper_cur->next;
	}
	
	free_oper_node(oper_list);
	
	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "Doneoperation", NULL, buf);
	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "Doneoperation", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}

	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}	
	return SUCCESS; 
#endif

base_err:
	
	cloud_send_base_err(sockfd,  opt, EWEB_BASE_ERROR);

	return FAILURE;
}

static int 
Pcsreset(char *buffer, char *url, char *opt, int sockfd)
{
	char buf[4096] = {0};
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	

	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "reset", NULL, buf);	

	xml_add_elem(XML_LABEL, "errno", "0", buf);
	xml_add_elem(XML_ELEM_END, "reset", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
#endif

}

static int 
fRefresh(char *buffer, char *url, char *opt, int sockfd)
{
	enum{
		REFLESH_BAIDU=1,
		REFLESH_RESOURCE
	};
	char buf[4096] = {0};	
	char errbuf[128] = {0}, item[256] = {0};
	int ret, kind = REFLESH_BAIDU;
#ifndef PCS_SUPPORT_GIZP
	int len;
	char httpbuf[4096] = {0};
#endif	

	
	ret = get_value_from_url(url, "item", item);
	if(ret == FAILURE || strcmp(item, "baidu") == 0){
		kind = REFLESH_BAIDU;
	}else if(strcmp(item, "resource") == 0){
		kind = REFLESH_RESOURCE;
	}

	xml_add_header(buf);
	xml_add_elem(XML_ELEM_START, "baidupcs", NULL, buf);
	xml_add_elem(XML_ELEM_START, "refresh", NULL, buf);

	sprintf(errbuf, "%d", 0);
	xml_add_elem(XML_LABEL, "errno", errbuf, buf);
	xml_add_elem(XML_ELEM_END, "refresh", NULL, buf);
	xml_add_elem(XML_ELEM_END, "baidupcs", NULL, buf);
	xml_add_end(buf);

#ifdef PCS_SUPPORT_GIZP
	if(send_func(buffer, opt, sockfd, buf, strlen(buf))){
		return FAILURE;
	}
	return SUCCESS;
#else
	len = sprintf(httpbuf, httphead, strlen(buf));
	sprintf(&httpbuf[len], "%s", buf);

	DPRINTF("httpbuf=\n%s\n", httpbuf);
	if(send(sockfd, httpbuf, strlen(httpbuf), 0) == -1){
		DPRINTF("send error! errmsg is: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
#endif

}


