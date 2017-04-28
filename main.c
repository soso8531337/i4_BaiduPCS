#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <locale.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include <openssl/aes.h>
#include <openssl/md5.h>
#include <sqlite3.h>
#include <unistd.h>
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

#include "common.h"
#include "web_api.h"
#define PCS_PORT	8400


int 
main(int argc, char *argv[])
{
	fd_set readset;	/* for select() */
	int sfd = -1, cfd = -1;
	char buf[4096] = {0};
	char HttpCommand[16];
	char HttpUrl[4096] = {0};
	char HttpVer[16] = {0};
	int i = 0;
	int daemonize = 1, ret, option;
	char value[256];
	char *start = NULL, *end = NULL, *p;
	struct timeval timeout;

	setlocale(LC_ALL, "");

	static const struct option options[] = {
		{ "daemon", 0, NULL, 'd' },
		{ "debug", 0, NULL, 'D' },
		{ "help", 0, NULL, 'h' },
		{ NULL, 0, NULL, 0 }	
	};

	curl_global_init(CURL_GLOBAL_DEFAULT);

	
	while (1)
	{
		if(argc == 1){
			daemonize = 1;
			break;
		}
		option = getopt_long(argc, argv, "dDh", options, NULL);
		if (option == -1)
			break;

		switch (option) {
			case 'd':
				daemonize = 1;
				goto debug_deamon;
			case 'D':
				daemonize = 0;
				goto debug_deamon;
			default:
				printf("baidupcs [--help] [--daemon] [--debug]\n");
				exit (1);
		}
		
	}

debug_deamon:
	
	if ((daemonize == 1) && (!dm_daemon(NULL, NULL))) {
		DPRINTF("Fail to create daemon\n");
		exit(1);
	}	

	if (already_running(PID_FILE)){
		DPRINTF("Another \"baidupcs\" is running! Exit!\n");
		exit(1);
	}

	handler_sig();
	/* advise heap to 1MB */
	sqlite3_soft_heap_limit(DB_HEAP_SIZE);

	if(pcs_web_api_init() < 0){
		DPRINTF("pcs_web_api_init error!\n");
	}
	while((sfd = tcp_init(PCS_PORT))< 0){
		DPRINTF("Cloud Web Server Init error!\n");
		sleep(3);
	}
	
	while (1)
	{
		FD_ZERO(&readset);	
		FD_SET(sfd, &readset);
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;		
		ret = select(sfd + 1, &readset, NULL, NULL, &timeout);
		if(ret < 0){
			DPRINTF("select error!\n");
			continue;
		}else if (ret == 0){
// 			DPRINTF("select timeout!");
			continue;
		}
        
		if (!FD_ISSET(sfd, &readset)){
			continue;
		}	
		if ((cfd = tcp_accept(sfd)) == FAILURE){
			DPRINTF("tcp_accept error!\n");
			continue;
		}

		memset(buf, 0, sizeof(buf));
		ret = recv(cfd, buf, sizeof(buf)-1, 0); //recv size must be buf_size-1, if not strlen may be incorrect 20150623
		if( ret == -1 || ret == 0){
			DPRINTF("recv error or 0!\n");/* sometimes error here */
			close(cfd);
			continue;			
		}
		/*
		 * process http header, get http command, 
		 * http url, http version
		 */
		memset(HttpCommand, 0, sizeof(HttpCommand));
		memset(HttpUrl, 0, sizeof(HttpUrl));
		memset(HttpVer, 0, sizeof(HttpVer));
		p = buf;
		for(i = 0; i<15 && *p != ' ' && *p != '\r'; i++)
			HttpCommand[i] = *(p++);
		HttpCommand[i] = '\0';
		DPRINTF("HttpCommand=%s\n", HttpCommand);
		while(*p==' ')
			p++;
		if(strncmp(p, "http://", 7) == 0){
			p = p+7;
			while(*p!='/')
				p++;
		}
		for(i = 0; i<4095 && *p != ' ' && *p != '\r'; i++)
			HttpUrl[i] = *(p++);
		HttpUrl[i] = '\0';
//		str_decode_url(HttpUrl, strlen(HttpUrl), HttpUrl, strlen(HttpUrl) + 1);
		DPRINTF("HttpUrl=%s\n", HttpUrl);
				
		while(*p==' ')
			p++;
	
		for(i = 0; i<15 && *p != '\r'; i++)
			HttpVer[i] = *(p++);
		HttpVer[i] = '\0';
		DPRINTF("HttpVer=%s\n", HttpVer);
		
		/*
		 * process http url, get opt=, and some args,
		 * call the function
		 */
		if (strstr(HttpUrl, "baidupcs.csp") == NULL ||
			(start = strstr(HttpUrl, "opt=")) == NULL){
			DPRINTF("Not found opt=.\n");
			cloud_send_base_err(cfd, NULL, EWEB_BASE_ERROR);
			close(cfd);
			continue;
		}
		start += strlen("opt=");
		while(*start == ' '){
			start++;
		}
		/*memset value*/
		memset(value, 0, sizeof(value));
		end = value;
		i = 0;
		while(*start != '\0' &&*start != '&' && i < 256){
			*end = *start; 
			end++;
			start++;
			i++;
		}
		DPRINTF("Http request opt=%s\n", value);
		cloud_handle_web_request(buf, HttpUrl, value, cfd);
		close(cfd);
	}

	if (!ssl_thread_cleanup()){
		DPRINTF("ssl thread cleanup FAILURE.\n");
	}
	
 	close(sfd);	

	return 0;
}


