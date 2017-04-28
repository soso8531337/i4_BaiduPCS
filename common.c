#define _GNU_SOURCE
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


#include <mntent.h>
#include <sqlite3.h>

#include "common.h"
#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)                 

void log_debug(FILE *fp, char *fname, const char *func, int lineno, char *fmt, ...)
{
	va_list ap;
	pid_t pid;
	
	if (fp == NULL)
		fp=stderr;

	pid = getpid();
		
	time_t t;
	struct tm *tm, tmptm={0};
	t = time(NULL);
	localtime_r(&t, &tmptm);
	tm=&tmptm;
	fprintf(fp, "[%04d/%02d/%02d %02d:%02d:%02d] ",
			tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	
	fprintf(fp, "[pid:%d] ", pid);
	
	fprintf(fp, "(%s:%s():%d) ", fname, func, lineno);

	va_start(ap, fmt);
	if (vfprintf(fp, fmt, ap) == -1)
	{
		va_end(ap);
		return;
	}
	va_end(ap);

	fflush(fp);

	return;
}

int tcp_init(unsigned short port)
{
	int s;
	int i = 1;
	struct sockaddr_in listenname;

	s = socket(PF_INET, SOCK_STREAM, 0);
	if (s < 0)
	{
		DPRINTF("socket error!\n");
		return -1;
	}

	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) < 0)
		DPRINTF("setsockopt error\n");
	memset(&listenname, 0, sizeof(struct sockaddr_in));
	listenname.sin_family = AF_INET;
	listenname.sin_port = htons(port);
	listenname.sin_addr.s_addr =  htonl(INADDR_ANY);//inet_addr(IP)

	if (bind(s, (struct sockaddr *)&listenname, sizeof(struct sockaddr_in)) < 0)
	{
		DPRINTF("bind(http), errmsg is: %s.\n", strerror(errno));
		close(s);
		return -1;
	}

	if (listen(s, 6) < 0)
	{
		DPRINTF("listen(http), errmsg is: %s.\n", strerror(errno));
		close(s);
		return -1;
	}

	return s;
}

int tcp_accept(int sfd)   
{
	struct sockaddr_in clientaddr;
	memset(&clientaddr, 0, sizeof(struct sockaddr));
	int addrlen = sizeof(struct sockaddr);
	struct timeval tv;
	int flag = 1;

	tv.tv_sec = SOCKTIMEO;
	tv.tv_usec = 0;

	int cfd = accept(sfd, (struct sockaddr*)&clientaddr, &addrlen);
	if(cfd == -1)
	{
	    DPRINTF("accept, errmsg is: %s.\n", strerror(errno));
	    return FAILURE;
	}
	/* set send and recv timeout to 5 second */	
	if (setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) 
		DPRINTF("setsockopt error\n");
	if (setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) 
		DPRINTF("setsockopt error\n");
	setsockopt(cfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flag, sizeof(flag));

    	DPRINTF("%s %d success connect...\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
	return cfd;
}

static void sig_hander(int signo)
{
    printf("recv signal, signal number = %d\n", signo);
}

int ipc_server_init(char *path)
{
    int len = 0, sock = 0;
    sigset_t sig;
    struct sockaddr_un addr;

    if ((!path))
        return -1;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        DPRINTF("create socket fail, errno:%d, %s\n", errno, strerror(errno));
        return -1;
    }

    unlink(path);
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);
    len = sizeof(addr.sun_family) + strlen(addr.sun_path);

    if (bind(sock, (struct sockaddr *)&addr, len) < 0) {
        DPRINTF("bind socket fail, errno:%d, %s\n", errno, strerror(errno));
        close(sock);
        return -1;
    }

    if (listen(sock, 5) < 0) {
        DPRINTF("listen on socket fail, errno:%d, %s\n", errno, strerror(errno));
        close(sock);
        return -1;
    }

    sigemptyset(&sig);
    /*sigaddset(&sig, SIGALRM);*/
    sigaddset(&sig, SIGABRT);
    sigaddset(&sig, SIGPIPE);
    sigaddset(&sig, SIGQUIT);
    /*sigaddset(&sig, SIGTERM);*/
    sigaddset(&sig, SIGUSR1);
    sigaddset(&sig, SIGUSR2);
    sigprocmask(SIG_BLOCK, &sig, NULL);
    signal(SIGBUS, sig_hander);
    signal(SIGHUP, sig_hander);
    signal(SIGILL, sig_hander);

    DPRINTF("ipc server success, path:%s, sock:%d\n", path, sock);

    return sock;
}

int ipc_pipe_init(char *path)
{
	int fd = -1;

	if(!path){
		return -1;
	}

	unlink(path);

	if(mkfifo(path, 0666) < 0){
        DPRINTF("mkfifo fail, errno:%d, %s\n", errno, strerror(errno));
		return -1;
	}
	if((fd = open(path, O_RDWR)) < 0){
        DPRINTF("mkfifo open failed, errno:%d, %s\n", errno, strerror(errno));
		return -1;
	}

    DPRINTF("ipc fifo success, path:%s, fd:%d\n", path, fd);

    return fd;
}


int check_dev(char *dev)
{
	FILE *pfp = NULL;
	char line[512] = {0};	
	int ma, mi, sz, flag = 0;
	char ptname[100];
	char devname[256] = {0}, *ptr=NULL;

	ptr = strrchr(dev, '/');
	if(ptr != NULL){
		strcpy(devname, ++ptr);
	}else{
		strcpy(devname, dev);
	}
	
	pfp = fopen("/proc/partitions", "r");
	if(pfp == NULL){
		DPRINTF("Fopen /proc/partitions failed\n");
		return FAILURE;
	}

	while(fgets(line, 512, pfp)){
		if (sscanf(line, " %d %d %d %[^\n ]",
				&ma, &mi, &sz, ptname) != 4){
			continue;
		}	
		if(strstr(devname, ptname)==NULL){
			continue;
		}
		flag = 1;
		break;
	}
	fclose(pfp);

	return (flag == 1?SUCCESS:FAILURE);
}

int check_partrdonly(char *path)
{
	char filename[4096] = {0};
	int fd;
	
	if(path == NULL){
		return -1;
	}

	sprintf(filename, "%s/.readonly.tst", path);
	fd = open(filename, O_CREAT|O_TRUNC|O_RDWR, 0755);
	if(fd < 0 && errno == EROFS){
		printf("[%s:%d][%s] Read Only...\n", __func__, __LINE__, path);
		close(fd);
		return 1;
	}
	close(fd);
	remove(filename);
	
	return 0;
}

int
rslash(char *src)
{
	char * start = NULL, *pflag = NULL;

	if(!src){
		return FAILURE;
	}
	while ((start = strstr(src, "//"))){
		pflag = start + 1;	
		while (*pflag != '\0'){
			*(start++) = *(pflag++);
		}
		*start = '\0';
	}
	return SUCCESS;
}

char *str_memstr(void *s, int slen, void *mstr, int mlen)
{
	unsigned char *start = (unsigned char *)s;
	unsigned char *dst = (unsigned char *)mstr;
	
	if ((s == NULL) || (mstr == NULL) || (slen < 0) || (mlen < 0)){
		return NULL;
	}
	while (start < ((unsigned char *)s + slen)) {
		if (*start == *((unsigned char *)dst)) {
			if (memcmp(start, mstr, mlen) == 0) {
				return (char *)start;
			}
		}
		start++;
	}

	return NULL;
}

int socket_read(int fd, char *base, int len, const char *stop, int timeout)
{
	int n = len;
	char *buf = base;
	if (base == NULL) {
		return -1;
        }
	while (n > 0) {
		int tret;
		fd_set readfd, exceptfd;
		struct timeval tv;

		FD_ZERO(&readfd);
		FD_ZERO(&exceptfd);
		FD_SET(fd, &readfd);
		FD_SET(fd, &exceptfd);

		if (timeout == -1) {
		//	DPRINTF("select blocked...\n");
			tret = select(fd+1, &readfd, 0, &exceptfd, NULL);
		} else {
			tv.tv_sec = timeout; /* second */
			tv.tv_usec = 0;      /* us     */
			tret = select(fd+1, &readfd, 0, &exceptfd, &tv);
		}
		if (tret == 0) { /* Timeout */
		//	DPRINTF("Timeout!!!!!!!!!!!...\n");
			return -3;
		} else if (tret == -1) { /* Error */
			if (errno == EINTR){
				continue;
			}else {
			//	DPRINTF("select failed\n");
				return -1;
			}
		} /* else: receive data or except handle */
		if (FD_ISSET(fd, &exceptfd)) {
		//	DPRINTF("select exceptfd failed\n");
			return -1;
		}
		
		if (FD_ISSET(fd, &readfd)){
			tret = recv(fd, buf, n, 0);
			if (tret < 0) {
				if ((errno == EINTR) || (errno == EWOULDBLOCK) 
				    || (errno == EAGAIN)) {
					continue;
				} else {
				///	DPRINTF("recv failed\n");
					return -1;
				}
			} else if (tret == 0) {  /* the peer haved shutdown, terminate */
			//	DPRINTF("Shutdown connect...\n");
				return -2;
			} else { /* Prepare to the next reading */
				n -= tret;
				buf += tret;
			}

			/* Check stop flag         */
			if (stop != NULL) {
				*buf = '\0';
				/* Got a stop flag */
				if (str_memstr(base, buf - base, (void *)stop,
					       strlen(stop)) != NULL) 
					break;
			}
		}
	}
	return len - n;
}

int socket_write(int fd, const char *buf, int len, int timeout)
{
	int n = len;
        if (buf == NULL) {
		return -1;
        }

	while (n > 0) {
		int tret;
		fd_set writefd, exceptfd;
		struct timeval tv;

		FD_ZERO(&writefd);
		FD_ZERO(&exceptfd);
		FD_SET(fd, &writefd);
		FD_SET(fd, &exceptfd);

		if (timeout == -1) {
			tret = select(fd+1, 0, &writefd, &exceptfd, NULL);
		} else {
			tv.tv_sec = timeout; /* second */
			tv.tv_usec = 0;      /* us     */
			tret = select(fd+1, 0, &writefd, &exceptfd, &tv);
		}
		if (tret == 0) { /* Timeout */
			return -3;
		} else if (tret == -1) { /* Error */
			if (errno == EINTR)
				continue;
			else {
				return -1;
			}
		} /* else: send data or exception handler */
		if (FD_ISSET(fd, &exceptfd)) {
			return -1;
		}		

		tret = n < 0x2000 ? n : 0x2000;
		tret = send(fd, buf, tret, 0);
		if (tret == -1) {
			if (errno == EINTR){
				continue;
			}else if (errno == ECONNRESET) {
				return -2;
			} else {
				return -1;
			}
		}
		n -= tret;
		buf += tret;
	}

	return len;
}

int connect_nonblock(int sockfd, struct sockaddr *addr, int slen, int nsec)
{
	int error=0,  flag, res;
	fd_set rset, wset;
	struct timeval tv;
	int len;

	flag = fcntl(sockfd, F_GETFL, 0);
	if(flag < 0){
	//	DPRINTF("fcntl get failed\n");
		return -1;
	}
	if(fcntl(sockfd, F_SETFL, flag | O_NONBLOCK) < 0){
	//	DPRINTF("fcntl set failed\n");
		return -1;
	}
	res = connect(sockfd, addr, slen);
	if(res < 0){
		if(errno != EINPROGRESS){
			return -1;
		}
	}else if(res == 0){
		goto ok;
	}

	FD_ZERO(&rset);
	FD_SET(sockfd, &rset);
	wset = rset;
	tv.tv_sec = nsec;
	tv.tv_usec = 0;

	res = select(sockfd+1, &rset, &wset, NULL, nsec ? &tv:NULL);
	if(res < 0){
		return -1;
	}else if(res == 0){
		fprintf(stderr, "Nonblock connect timeout!!\n");
		return -1;
	}
	
	if(FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset)){
		len = sizeof(error);
		if(getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0){
			return -1;
		}
		if(error){
			return -1;
		}else{
			goto ok;
		}
	}else{
		return -1;
	}

	ok:
		if(fcntl(sockfd, F_SETFL, flag) < 0){
			return -1;
		}
		
		return 0;
}
/* 
 * get the value from a url according to the key
 * /dropbox.csp?fname=dropbox&opt=GetStatus&path=/data/UsbDisk1/Volume1/Dropbox&function=get
 * eg: key=fname, return dropbox
 * caller need free the return string.
 * */
int  
get_value_from_url(char *url, const char *key, char *value)
{
	char buf[4096] = {0}, tmpurl[4096] = {0};
	char *start = NULL, *dst = NULL;

	if (NULL == url || NULL == key || NULL == value){
		DPRINTF("url or key is NULL\n");
		return FAILURE;
	}
	value[0] = '\0';
	sprintf(buf, "&%s%s", key, "=");
	sprintf(tmpurl, "%s&", url);	
	if ((start = strstr(tmpurl, buf)) == NULL){
		memset(buf, 0, sizeof(buf));
		sprintf(buf, "?%s%s", key, "=");
		if((start = strstr(tmpurl, buf)) == NULL){
			DPRINTF("Not found \"%s\" in \"%s\"\n", buf, tmpurl);
			return FAILURE;
		}
	}
	start += strlen(buf);
	dst = value;
	while(start && *start != '\0' &&*start != '&'){
		*dst++ = *start++;
	}
	*dst = '\0';
	DPRINTF("%s=%s\n", key, value);
//	str_decode_url(value, strlen(value), value, strlen(value)+1);

	return SUCCESS;
}

/* Purpose: Decode a url code string, for example '%','+' etc.
 * Note   : Enable the same string(share space) for source string
 *          and destination string
 * In     : *src: the source string
 *          src_len: the source string length
 *          *dst: the destination sting
 *          dst_len: the destination string length
 * Return : void
 */
void str_decode_url(const char *src, int src_len, char *dst, int dst_len)
{
	int  i, j, a, b;
	if(src == NULL){
		DPRINTF("src is null.\n");
		return ;
	}
#define HEXTOI(x)  (isdigit(x) ? x - '0' : x - 'W')

        for (i = j = 0; i < src_len && j < dst_len - 1; i++, j++)
                switch (src[i]) {
                case '%':
                        if (isxdigit(((unsigned char *) src)[i + 1]) &&
                            isxdigit(((unsigned char *) src)[i + 2])) {
                                a = tolower(((unsigned char *)src)[i + 1]);
                                b = tolower(((unsigned char *)src)[i + 2]);
                                dst[j] = (HEXTOI(a) << 4) | HEXTOI(b);
                                i += 2;
                        } else {
                                dst[j] = '%';
                        }
                        break;
                case '+':
                        dst[j] = ' ';
                        break;
                default:
                        dst[j] = src[i];
                        break;
                }

        dst[j] = '\0';  /* Null-terminate the destination */
}

void str_encode_url(const char *src, int src_len, char **dst)
{
	char encoded[4096];
	unsigned char c;
	int  i,j = 0;

	if(src == NULL)
		return;
	memset(encoded, 0, 4096);
	for(i=0; i < src_len; i++){
		c = src[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') ||c == '.' || c == '-' || c == '_' || c == '/'){
			// allowed characters in a url that have non special meaning
			encoded[j] = c;
			j++;
			continue;
		}
		j += sprintf(&encoded[j], "%%%x", c);
	}

	*dst = strdup(encoded);
}
/* Purpose   : Create a daemon process
 * Prameters : *fname: the pid file path and name
 *             *workdir: the working directory
 * Return    : FAILURE: 0
 *             SUCCESS: 1
 */
int dm_daemon(const char *fname, const char *workdir)
{
	int pid, i;

// 	if ((fname != NULL) &&  dm_chk_running(fname)) {
// 		fprintf(stderr, "The daemon is running known by %s\n",
// 			fname);
// 		exit(1);
// 	}
	
	switch(fork())
	{
		/* fork error */
	case -1:
		exit(1);
	
		/* child process */
	case 0:
		/* obtain a new process group */
		if((pid = setsid()) < 0) {
			exit(1);
		}

		/* close all descriptors */
		for (i = getdtablesize(); i >= 0; --i) 
			close(i);

		i = open("/dev/null", O_RDWR); /* open stdin */
		dup(i); /* stdout */
		dup(i); /* stderr */

		umask(000);

		//umask(027);

// 		if (workdir)
// 			chdir(workdir); /* chdir to /tmp ? */

// 		if ((fname != NULL) && !dm_write_pidfile(fname)) {
// 			exit(1);
// 		}
		return SUCCESS;

		/* parent process */
	default:
		exit(0);
	}

	return FAILURE;
}

int lockfile(int fd)
{
	struct flock f1;

	f1.l_type = F_WRLCK;
	f1.l_start = 0;
	f1.l_whence = SEEK_SET;
	f1.l_len = 0;

	return (fcntl(fd, F_SETLK, &f1));

}

int already_running(char *pidfile)
{
	int fd;
	char buf[16];

	fd = open(pidfile, O_RDWR|O_CREAT, LOCKMODE);
	if(fd < 0)
	{
		DPRINTF("Can not open %s:%s\n", pidfile, strerror(errno));
		return -1;

	}

	if(lockfile(fd) < 0)
	{
		if(errno == EACCES || errno == EAGAIN)
		{
			close(fd);
			return 1;

		}
		DPRINTF("Can not lock %s:%s\n", pidfile, strerror(errno));
		return -1;

	}
	ftruncate(fd, 0);
	sprintf(buf, "%ld", (long)getpid());
	write(fd, buf, strlen(buf)+1);
	return 0;

}
int
handler_sig()
{
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	//act.sa_handler = io_sig_handler;
	act.sa_handler = SIG_IGN;
	sigfillset(&act.sa_mask);
	if ((sigaction(SIGCHLD, &act, NULL) == -1) ||
//		(sigaction(SIGTERM, &act, NULL) == -1) ||
		(sigaction(SIGINT, &act, NULL) == -1) ||
		(sigaction(SIGSEGV, &act, NULL) == -1)) {
		DPRINTF("Fail to sigaction");
		
	}

	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) == -1) {
		
		DPRINTF("Fail to signal(SIGPIPE)");
		
	}
	return SUCCESS;

}

