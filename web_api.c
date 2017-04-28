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
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/vfs.h>
#include <sys/resource.h>
#include <mntent.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

#include <fcntl.h>

#include "pcs/pcs_mem.h"
#include "pcs/cJSON.h"
#include "pcs/pcs_utils.h"
#include "pcs/pcs.h"
#include "dir.h"
#include "utils.h"
#include "common.h"
#include "sql.h"
#include "web_api.h"

#define SORT_DIRECTION_ASC	0 /*正序*/
#define SORT_DIRECTION_DESC 1 /*倒序*/
#define PCS_HTTP_TRY			3
#define PCS_THREAD_NUM			4
#define PRINT_PAGE_SIZE			20		/*列出目录或列出比较结果时，分页大小*/
#define PCS_MULTI_DOWNNUM	4
#define PCS_DEFAULT_CONFIGDIR		"/etc/config/pcs"
#define PCS_REMOTE_DIR			"/SmartHDD"
#define PCS_LOCAL_DIR			"Baidu"
#define PCS_TMP_FILE		".bd"
#define PCS_DBDIR_POSTFIX			".vst/Baidu/"
#define PCS_IPC_VERIFY			"/tmp/ipc.verify"
#define PCS_IPC_LOGIN			"/tmp/ipc.login"
#define PCS_IPC_INPUT			"/tmp/ipc.input"
#define THREAD_STATE_MAGIC			(0x7bcd7bcd)

#define USAGE "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.87 Safari/537.36"
#define DB_VERSION		2017
#define PCS_MULTI_DOWNLIMIT	(10*1024*1024) /*the limit to user mulit-pthread download*/

#define CONNECTTIMEOUT				10

#define CONFIG_STRLEN		256
#define PCS_DONE_RECORD_MAX		150
#define I4SEASON_NETCHK_HOST		"www.simicloud.com"
#define I4SEASON_NETCHK_QUERY		"/media/httpbin/status/204"

enum{
	LOGIN_OK,
	LOGIN_VERIFY=1,
	LOGIN_BADPASSWD = EWEB_LOGIN_BADPASSWD,
	LOGIN_BADUSER = EWEB_LOGIN_BADUSER,
	LOGIN_BADVERIFY = EWEB_LOGIN_BADVERIFY,
	LOGIN_BADNETWORK = EWEB_LOGIN_BADNETWORK,
	LOGIN_FAILED = EWEB_LOGIN_FAIL,
};
enum{
	PCS_MULTI_STOP = 0,
	PCS_MULTI_RUN,
	PCS_MULTI_WAIT, /*Add by zhangwei 20150608, use for signal download*/
	PCS_RECODE_OK ,
	PCS_RECODE_ERR,
	PCS_RECODE_INT,
	PCS_RECODE_OK_NFOUND
};

typedef enum{
	DOWN_ACTION=888,
	UPLOAD_ACTION=999
}OP_ACTION;

typedef enum {
	PCS_DOWN_INIT = 0,
	PCS_DOWN_CRT_TMP,
	PCS_DOWN_ING
}DW_STATUS;


typedef struct _multi_segment{
	volatile int threadNum;
	volatile long long sp; /*start position*/
	volatile long long ep; /*end position*/
	volatile long long ds; /*download size*/
}segment;

#define SEG_ENCRY_SIZE		sizeof(segment)

typedef struct _multi_taskinfo{
	volatile int CondThread;
	volatile int StatusRun;
	volatile int reCode;
	char fileDown[2048];
	volatile long long downSzie;
	double curlDown;
	double nowDown;
	segment seginfo; 
}muti_taskinfo;


typedef struct _saveDownloadState{
	int32_t down_magic;
	int32_t segnum;
	segment seginfo[];
}saveDownloadState;

typedef struct _done_rec_t {
	int64_t id;
	char path[1024];  /* key */ 
	char size[64];
	int dir;	
	char md5[64];
	int action;
	char ltime[64];
	int opt;
	int status;
}done_rec_t;

typedef struct _metadata_t {
	/* for baidu pcs  */	
	char fs_id[64];
	char path[PATH_MAX];
	char ctime[64];
	char mtime[64];
	char md5[64];   /*TODO: it's a array json*/ 	
	char size[64];
	int isdir;
	int ifhassubdir;
	int isdelete;
} metadata_t, *metadata_tp;

typedef struct _webContext{
	char	contextfile[CONFIG_STRLEN]; /*上下文文件的路径*/
	char	cookiefile[CONFIG_STRLEN]; /*Cookie文件路径*/
	char	captchafile[CONFIG_STRLEN]; /*验证码图片路径*/
	char	workdir[CONFIG_STRLEN]; /*当前工作目录*/
	char 	localdir[CONFIG_STRLEN];	/*where save config in local*/
	int		syncon;	/*sync on or off*/
	int 	islogin;/*login or not*/

	Pcs 	pcs;
	int		list_page_size; /*执行list命令时，每页大小*/
	char	list_sort_name[CONFIG_STRLEN>>4]; /*执行list命令时，排序字段，可选值：name|time|size*/
	char	list_sort_direction[CONFIG_STRLEN>>4]; /*执行list命令时，排序字段，可选值：asc|desc*/
	int		timeout_retry;  /*是否启用超时后重试*/
	int		max_thread; /*指定最大线程数量*/
	int		max_speed_per_thread; /*指定单个线程的最多下载速度*/

	char	user_agent[CONFIG_STRLEN];	/*user agent*/

	int 	verify_fd; /*used for user input verify code*/	
	int 	login_fd; /*used for user input verify code*/
	int 	input_fd;	/*Used for user input*/
	sqlite3 *db;	/*download table*/
	char 	dbdir[CONFIG_STRLEN];	/*database path*/
	pthread_t tlogin;
	pthread_t tserver;
	pthread_t tdown;
	pthread_t tdwarr[PCS_MULTI_DOWNNUM];

	/*User cloud information*/
	char 	quota[CONFIG_STRLEN];
	char 	used[CONFIG_STRLEN];
	char 	username[CONFIG_STRLEN];
	
}webContext;

/*********************************************************************************/
pthread_mutex_t m_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t m_cond=PTHREAD_COND_INITIALIZER;

pthread_mutex_t m_down=PTHREAD_MUTEX_INITIALIZER;


static int networkStatus = 0;
static int storageStatus = 0;
static int downloadStart = 0;
muti_taskinfo mTask[PCS_MULTI_DOWNNUM];
static progress_t downprogress;

webContext contexWeb;


/*********************************************************************************/

static Pcs *create_pcs(webContext *context);
static void destroy_pcs(Pcs *pcs);

/*********************************************************************************/
int
cloud_set_errno(Errtype errtype, int oerrno)
{	
	pthread_mutex_lock(&m_mutex);
	if(errtype == ESTORAGE){
		storageStatus= oerrno;
	}else if(errtype == ENETWORK){
		networkStatus= oerrno;
	}

	pthread_mutex_unlock(&m_mutex);
	
	return SUCCESS;
}
/* reset error no to ok */

/*获取上下文存储文件路径*/
static int contextfile(char *name, int size)
{
	memset(name, 0, size);
	CreateDirectoryRecursive(PCS_DEFAULT_CONFIGDIR);
	snprintf(name, size-1, "%s/pcs.context", PCS_DEFAULT_CONFIGDIR);

	return 0;
}

/*返回COOKIE文件路径*/
static int cookiefile(char *name, int size)
{
	memset(name, 0, size);
	CreateDirectoryRecursive(PCS_DEFAULT_CONFIGDIR);
	snprintf(name, size-1, "%s/default.cookie", "/tmp");

	return 0;
}

/*返回验证码图片文件路径*/
static int captchafile(char *name, int size)
{
	memset(name, 0, size);
	CreateDirectoryRecursive(PCS_DEFAULT_CONFIGDIR);
	snprintf(name, size-1, "%s/captcha.gif", PCS_DEFAULT_CONFIGDIR);

	return 0;
}

int pcs_open_sockfd_gethostbyname(char *host,int port)
{
	int sockfd;
	struct sockaddr_in addr;
	struct hostent hent, *result;
	char buff[8192];
	int err;
	char **pptr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		return -1;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	
	if(gethostbyname_r(host, &hent, buff, 8192, &result, &err) != 0){		
		close(sockfd);		
		return -1;
	}
	for(pptr = result->h_addr_list; *pptr != NULL; pptr++){
		memcpy(&addr.sin_addr, *pptr, sizeof(addr.sin_addr));
		//DPRINTF("Connect to %s...\n", inet_ntop(AF_INET, &addr.sin_addr, ip, 32));
		if(connect_nonblock(sockfd, (struct sockaddr *)&addr, sizeof(addr), 5) == 0){
			return sockfd;
		}
	}
	close(sockfd);
	
	return -1;
}

int pcs_open_sockfd_getaddrinfo(char *host,int port)
{
	int sockfd, err;
	struct addrinfo hints, *result, *curr;
	char ip[32];
	char addr_port[5] = {0};

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		DPRINTF("socket failed\n");
		return -1;
	}
	sprintf(addr_port, "%d", port);	

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
		
	if((err = getaddrinfo(host, addr_port, &hints, &result)) != 0){
		close(sockfd);
		//DPRINTF("Getaddrinfo Error...[%d:%s]-->[%s]\n",
                   //     err, gai_strerror(err), strerror(errno));	
		return -1;
	}
        for(curr = result; curr !=NULL; curr = curr->ai_next){
                inet_ntop(AF_INET,  &(((struct sockaddr_in *)(curr->ai_addr))->sin_addr),
                ip, 16);
		if(connect_nonblock(sockfd, curr->ai_addr, curr->ai_addrlen, 5) == 0){
			freeaddrinfo(result);
			return sockfd;
		}
                
        }
	
	freeaddrinfo(result);
	close(sockfd);
	
	return -1;
}

int pcs_connect_server(void)
{
	int sockfd;
	char sndbuf[4096] = {0}, rcvbuf[4096] = {0}, *ptmp = NULL;	
	char *ntwork_Q= 
		"GET %s HTTP/1.1\r\n"
			  "HOST: %s\r\n"
			  "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*/;q=0.8\r\n"
			  "Accept-Language: zh-cn,zh;q=0.8,en-us;q=0.5,en;q=0.3\r\n"
			  "Connection: keep-alive\r\n\r\n";

	res_init();		
	sockfd = pcs_open_sockfd_getaddrinfo(I4SEASON_NETCHK_HOST,80);
	if(sockfd < 0){
		sockfd = pcs_open_sockfd_gethostbyname(I4SEASON_NETCHK_HOST,80);
		if(sockfd < 0){
			printf("open Socket Error:%s\n", strerror(errno));
			return FAILURE;
		}
	}
	
	snprintf(sndbuf, 4095, ntwork_Q, I4SEASON_NETCHK_QUERY, I4SEASON_NETCHK_HOST);
	if(socket_write(sockfd, sndbuf, strlen(sndbuf),  8) < 0){
		close(sockfd);		
		printf("Read Socket Error:%s\n", strerror(errno));
		return FAILURE;
	}
	
	if(socket_read(sockfd,rcvbuf,4095,"\r\n\r\n", 8) < 0){
		printf("Write Socket Error:%s\n", strerror(errno));
		close(sockfd);		
		return FAILURE;
	}
	/*Decode HTTP Response*/
	ptmp = strstr(rcvbuf, "HTTP/1.1");
	if(ptmp == NULL || atoi(ptmp+strlen("HTTP/1.1")) != 204){
		close(sockfd);		
		return FAILURE;
	}

	close(sockfd);
	return SUCCESS;
}

int pcs_multi_down_debug(void)
{
	int current = 0;
	DPRINTF("Multi-DownLoad INFO:\nDownFile:%s:\n", mTask[current].fileDown);
	for(current = 0; current < PCS_MULTI_DOWNNUM; current++){
		DPRINTF("************************Multi-Down*************************\n");
		if(mTask[current].seginfo.threadNum == 0){
			continue;
		}
		DPRINTF("TaskID:%d:\n", mTask[current].seginfo.threadNum);
		DPRINTF("TaskBegin:%lld\n", mTask[current].seginfo.sp);		
		DPRINTF("TaskEnd:%lld\n\n", mTask[current].seginfo.ep);		
		DPRINTF("TaskDown:%lld\n", mTask[current].seginfo.ds);
	}
	return SUCCESS;
}

int pcs_multi_down_setstart(void)
{
	int current;
	
	for(current = 0; current < PCS_MULTI_DOWNNUM; current++){
		if(mTask[current].seginfo.threadNum == 0){
			continue;
		}
		mTask[current].CondThread = PCS_MULTI_RUN;
	}
	return SUCCESS;
}

int pcs_multi_down_set_interupte(char *mdinfo, size_t size)
{
	int current, cursor, writelen;
	int fd, segnum = 0;
	saveDownloadState dwstate;
	segment seginfo[PCS_MULTI_DOWNNUM];
	
	if(!mdinfo){
		return FAILURE;
	}
	
	for(current = 0; current < PCS_MULTI_DOWNNUM; current++){
		if(mTask[current].seginfo.threadNum == 0){
			continue;
		}
		mTask[current].StatusRun = 1;
	}
	/*WaitDown Load Thread to Stop...*/
	cursor  = 0;
	while(cursor++ < 10){
		for(current = 0; current < PCS_MULTI_DOWNNUM; current++){
			if(mTask[current].CondThread == PCS_MULTI_RUN){
				break;
			}
		}
		if(current == PCS_MULTI_DOWNNUM){
			DPRINTF("All Multi-Down Thread Stop....\n");
			break;
		}
		usleep(500000);
	}
	if(cursor >=10){
		DPRINTF("Multi-Down Thread Not Stop Normal [Wait 30s]....\n");
	}
	/*Write to Multi-Down Info*/
	fd = open(mdinfo, O_WRONLY);
	if(fd < 0){
		DPRINTF("Create %s Failed...\n", mdinfo);
		if(errno == ENOSPC){			
			cloud_set_errno(ESTORAGE, ENOR_DISKFULL);
		}
		return FAILURE;
	}	
	lseek(fd, size, SEEK_SET);

	
	for(current = 0; current < PCS_MULTI_DOWNNUM; current++){
		if(mTask[current].seginfo.threadNum == 0){
			continue;
		}
		if(mTask[current].CondThread == PCS_MULTI_STOP &&
			(mTask[current].reCode == PCS_RECODE_OK ||
				mTask[current].reCode == PCS_RECODE_OK_NFOUND)){
			continue;
		}
		memcpy(&seginfo[current], &(mTask[current].seginfo), sizeof(segment));
		segnum ++;

	}
	dwstate.down_magic = THREAD_STATE_MAGIC;
	dwstate.segnum = segnum;
	write(fd, &dwstate, sizeof(dwstate));
	writelen = write(fd, seginfo, sizeof(segment) *segnum);
	if(writelen != sizeof(segment)*segnum){
		close(fd);
		if(errno == ENOSPC){			
			cloud_set_errno(ESTORAGE, ENOR_DISKFULL);
		}
		return FAILURE;
	}

	close(fd);

	return SUCCESS;
}

int pcs_multi_down_chk_condition(char *mdinfo, size_t size)
{
	int current;

	if(!mdinfo){
		return FAILURE;
	}
	
	for(current = 0; current < PCS_MULTI_DOWNNUM; current++){
		if(mTask[current].seginfo.threadNum == 0){
			continue;
		}
		if(mTask[current].CondThread== PCS_MULTI_STOP&&
				mTask[current].reCode == PCS_RECODE_ERR){
			pcs_multi_down_set_interupte(mdinfo, size);
			return FAILURE;
		}
		if(mTask[current].CondThread == PCS_MULTI_WAIT){
			mTask[current].StatusRun = 0;
			mTask[current].CondThread = PCS_MULTI_RUN;
		}
	}

	return SUCCESS;
}

int pcs_multi_down_update_progress(void)
{
	int current;
	long long downsize = 0;
	
	pthread_mutex_lock(&m_down);
	downsize = strtoll(downprogress.record.size, NULL, 10);
	
	for(current = 0; current < PCS_MULTI_DOWNNUM; current++){
		if(mTask[current].seginfo.threadNum == 0){
			continue;
		}
		downsize -= (mTask[current].downSzie-mTask[current].seginfo.ds);
	}
	downprogress.now = downsize;
	pthread_mutex_unlock(&m_down);
	
	return SUCCESS;
}

int pcs_multi_down_update_mdinfo(char *mdinfo, size_t size)
{
	int current, writelen, segnum = 0;
	int fd;
	saveDownloadState dwstate;
	segment seginfo[PCS_MULTI_DOWNNUM];
	
	if(!mdinfo){
		return FAILURE;
	}

	/*Write to Multi-Down Info*/
	fd = open(mdinfo, O_WRONLY);
	if(fd < 0){
		DPRINTF("Create %s Failed...\n", mdinfo);
		if(errno == ENOSPC){			
			cloud_set_errno(ESTORAGE, ENOR_DISKFULL);
		}
		return FAILURE;
	}
	
	lseek(fd, size, SEEK_SET);
	for(current = 0; current < PCS_MULTI_DOWNNUM; current++){
		if(mTask[current].seginfo.threadNum == 0){
			continue;
		}
		if(mTask[current].CondThread == PCS_MULTI_STOP &&
			(mTask[current].reCode == PCS_RECODE_OK ||
				mTask[current].reCode == PCS_RECODE_OK_NFOUND)){
			DPRINTF("CUID#%d Finish....[sp:%lld-->ep:%lld]\n", mTask[current].seginfo.threadNum,
					mTask[current].seginfo.sp, mTask[current].seginfo.ep);
			continue;
		}
		
		segnum ++;
		memcpy(&seginfo[current], &(mTask[current].seginfo), sizeof(segment));
	}

	dwstate.down_magic = THREAD_STATE_MAGIC;
	dwstate.segnum = segnum;
	write(fd, &dwstate, sizeof(dwstate));
	writelen = write(fd, seginfo, sizeof(segment) *segnum);
	if(writelen != sizeof(segment)*segnum){
		close(fd);
		if(errno == ENOSPC){			
			cloud_set_errno(ESTORAGE, ENOR_DISKFULL);
		}
		return FAILURE;
	}

	close(fd);

	return SUCCESS;
}

int pcs_multi_down_chk_finish(char *mdinfo, char *lpath, size_t size)
{
	int current;
	int finsh_flag =0;

	if(!mdinfo || !lpath){
		return FAILURE;
	}
	

	for(current = 0; current < PCS_MULTI_DOWNNUM; current++){
		if(mTask[current].seginfo.threadNum == 0){
			finsh_flag++;
			continue;
		}
		if(mTask[current].CondThread == PCS_MULTI_RUN ||
				mTask[current].CondThread == PCS_MULTI_WAIT){
			return 2;
		}
		if(mTask[current].reCode == PCS_RECODE_OK_NFOUND){
			DPRINTF("%s Not In CLoud Server\n", 
				lpath);
			return 3;
		}else if(mTask[current].reCode == PCS_RECODE_OK){
			finsh_flag++;
		}
	}

	if(finsh_flag == PCS_MULTI_DOWNNUM){
		DPRINTF("Down %s Finish...\n", lpath);
	}else{
		DPRINTF("Down Error..... Let Muliti-Down chk Condittion Handle it...\n");
		return 2;
	}
	if (truncate(mdinfo, size)) {
		DPRINTF("truncate Error...\n");
		return 2;
	}
	/*All Download Thread stop and return successful, we think it download finish*/	
	if ( lpath && access(lpath, F_OK) == 0){
		char oldpath[PATH_MAX] = {0}, *dot;
		int len = 0;

		dot = strrchr(lpath, '.');
		if(!dot){
			sprintf(oldpath, "%s(%ld)", lpath, time(NULL));
		}else{
			*dot = '\0';
			len = sprintf(oldpath, "%s(%ld)", lpath, time(NULL));
			*dot = '.';
			sprintf(&oldpath[len], "%s", dot);

		}
		DPRINTF("[DOWN] Rename %s to %s\n", lpath, oldpath);
		rename(mdinfo, oldpath);
	}
	
	rename(mdinfo, lpath);
	
	return SUCCESS;
}

int pcs_chk_downfile_size(rec_t * record)
{
	char lpath[PATH_MAX] = {0};
	struct stat st;
	int ret;
	off_t size = 0;
	
	if(!record){
		return FAILURE;
	}
	
	ret = pcs_convert_path_to_local(record->path, lpath);
	if (ret == FAILURE || lstat(lpath, &st) == -1){
		DPRINTF("[CHK_DOWN] %s=>%s error[%s]\n", 
				ret == FAILURE?"CONVERT PATH":"STAT FILE", lpath, strerror(errno));
		return FAILURE;
	}

	size = strtoll(record->size, NULL, 10);
	if(st.st_size != size){
		DPRINTF("[CHK_DOWN] Size is not match, DELETE[src == %lld  dst==%lld] Path=%s\n", 
				size, st.st_size, record->path);
		return FAILURE;
	}	

	return SUCCESS;
}

int cloud_check_condition(void)
{
	int ret = 0;
	pthread_mutex_lock(&m_mutex);
	if(contexWeb.islogin == 0 || 
			contexWeb.syncon == 0 || 
		networkStatus|| storageStatus){
		ret = -1;
	}
	pthread_mutex_unlock(&m_mutex);	

	return ret;
}

int
cloud_set_downprogress(OP_ACTION act, rec_t *recp)
{
	if (!recp){
		DPRINTF("recp is null\n");
		return FAILURE;
	}
	if(recp->dir == 1){
		return SUCCESS;
	}
	pthread_mutex_lock(&m_down);
	memset(&downprogress, 0, sizeof(progress_t));
	memcpy(&downprogress.record, recp, sizeof(rec_t));
	downprogress.action = act;
	downprogress.invaild= 0;
	pthread_mutex_unlock(&m_down);

	return SUCCESS;
}

int
cloud_clean_downprogress(void)
{
	pthread_mutex_lock(&m_down);
	memset(&downprogress, 0, sizeof(progress_t));
	pthread_mutex_unlock(&m_down);
	return SUCCESS;
}


int 
cloud_set_dlprogress_interp(char *path)
{
	int ret = 0;
	
	pthread_mutex_lock(&m_down);
	if(path == NULL && downprogress.record.dir == 0){
		DPRINTF("INTERUPT Create %s\n", downprogress.record.path);
		downprogress.interp = 1;
		ret = 1;
	}else if(path && strcmp(downprogress.record.path, path) == 0){
		DPRINTF("INTERUPT path==>%s\n", path);
		downprogress.interp = 1;
		ret = 2;
	}else{
		DPRINTF("INTERUPT Nothing...\n");
		ret = 0;
	}
	pthread_mutex_unlock(&m_down);

	return ret;
}

void
cloud_set_downprogress_status(DW_STATUS status)
{
	pthread_mutex_lock(&m_down);
	downprogress.status = status;
	pthread_mutex_unlock(&m_down);
}

int
cloud_get_downprogress(progress_t *progp)
{
	char lpath[PATH_MAX] = {0}, tmpath[PATH_MAX] = {0};
	struct stat statbuf;
	int ret;
	long long size;

	if (!progp){
		DPRINTF("progp is null\n");
		return FAILURE;
	}
	pthread_mutex_lock(&m_down);
	if(downprogress.invaild == 1){
		DPRINTF("Invaild DownProgress Information, INGNORE....\n");
		pthread_mutex_unlock(&m_down);
		return FAILURE;		
	}
	ret = pcs_convert_path_to_local(downprogress.record.path, lpath);
	if(ret == FAILURE){
		DPRINTF("Convert PATH [%s] error\n", downprogress.record.path);
		pthread_mutex_unlock(&m_down);
		return FAILURE;
	}
	
	size = strtoll(downprogress.record.size, NULL, 10);
	if(size <= PCS_MULTI_DOWNLIMIT){
		sprintf(tmpath, "%s%s", lpath, PCS_TMP_FILE);
		if (access(tmpath, F_OK) == 0){
			if(lstat(tmpath, &statbuf) == -1){
				DPRINTF("%s stat:%s\n", tmpath, strerror(errno));
				pthread_mutex_unlock(&m_down);
				return FAILURE;
			}
		}else if(access(lpath, F_OK) == 0){
			if(lstat(lpath, &statbuf) == -1){
				DPRINTF("%s stat:%s\n", lpath, strerror(errno));
				pthread_mutex_unlock(&m_down);
				return FAILURE;
			}		
		}else{
			statbuf.st_size= 0;
		}
		downprogress.now = statbuf.st_size;
	}
	downprogress.method = 1;
	memcpy(progp, &downprogress, sizeof(progress_t));
	pthread_mutex_unlock(&m_down);

	return SUCCESS;
}

int pcs_convert_path_to_local(const char *from, char *to)
{
	char diskpath[PATH_MAX] = {0};
	const char *cur;
	
	if(!from || !to){
		DPRINTF("Parameter is error\n");
		return FAILURE;
	}
	if(strncmp(from, PCS_REMOTE_DIR, strlen(PCS_REMOTE_DIR))){
		DPRINTF("From Path [%s] is not include APPS_PREFIX_SYNC\n", from);
		return FAILURE;
	}
	
	pthread_mutex_lock(&m_mutex);
	if(contexWeb.islogin == 0){
		pthread_mutex_unlock(&m_mutex);
		return FAILURE;
	}
	sprintf(diskpath, "%s/%s(%s)", contexWeb.localdir, PCS_LOCAL_DIR, pcs_sysUID(contexWeb.pcs));
	pthread_mutex_unlock(&m_mutex);

	if(strlen(diskpath) == 0){
		DPRINTF("Get diskpath error\n");
		return FAILURE;
	}
	cur = from+strlen(PCS_REMOTE_DIR);
	sprintf(to, "%s/%s", diskpath, cur);
	rslash(to);

	return SUCCESS;
}

static size_t 
write_callback_multi_down(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t retcode;
	int fd = *((int*)userdata);	

	retcode = write(fd, ptr, size);
	
	return retcode;
}

static int
trans_callback_multi_down(void *clientp, double dltotal, double dlnow,
		                      double ultotal, double ulnow)
{
	muti_taskinfo *task = (muti_taskinfo *)clientp;	
	int ret = 0;

	if (task->StatusRun == 1){
		DPRINTF("Multi-Down Callback [%s]-->INTERUPT by manual...\n", task->fileDown);
		return 1;
	}else if(task->StatusRun == 2){
		DPRINTF("Multi-Down Trun OFF....\n");
		return 1;
	}

	if(dltotal == 0){
	//	DPRINTF("Multi-Down Progress Callback Total Return 0, IGNORE....\n");
		return 0;
	}
	task->curlDown= dltotal;
	task->nowDown= dlnow;
	/*Compute the download size*/
	task->seginfo.ds= task->downSzie - (long long)(task->curlDown - task->nowDown);

	return 0; 
}

int sql_get_same_md5_record(rec_t *rectp, sqlite3 *db, char *fmt, ...)
{
	char *sql;
	char **result;
	int rows = 0, cols = 0, i = 0;
	int ret = 0, found = 0;
	va_list ap;
	char lpath[PATH_MAX];	
	struct stat st;

	if (!rectp){
		DPRINTF("rectp is null\n");
		return FAILURE;
	}
	if (!db){
		DPRINTF("db is null\n");
		cloud_set_errno(ESQL, EDB_NOPEN);
		return FAILURE;
	}

	va_start(ap, fmt);
	sql = sqlite3_vmprintf(fmt, ap);
	if( !sql ){
		cloud_set_errno(ENOR, ENOR_OUTMEM);
		va_end(ap);
		return FAILURE;
	}
	memset(rectp, 0, sizeof(rec_t));
	ret = sql_get_table(db, sql, &result, &rows, &cols);
	if(ret == FAILURE){
		sqlite3_free(sql);
		va_end(ap);
		return FAILURE;
	}
	if (rows > 1){
		DPRINTF("Found %d records in database table\n", rows);
	}
	if( rows  ){
		/* only get the first result */
		for ( i = 0; i < cols; ++i ){		
			if(rows > 1 && rectp->id && strcasecmp(result[i], "id") == 0){
				memset(lpath, 0, sizeof(lpath));
				if(pcs_convert_path_to_local(rectp->path, lpath) == FAILURE){
					sqlite3_free_table(result);
					sqlite3_free(sql);
					va_end(ap);	
					return FAILURE;
				}				
				if(lstat(lpath, &st) == 0 && 
						st.st_size == atoll(rectp->size)){
					DPRINTF("Found A SAME MD5 Record.....\n");
					found = 1;
					break;
				}
				
				memset(rectp, 0, sizeof(rec_t));
				rectp->id = strtoll(result[i + cols], NULL, 10);
			}
			if ( !result[i + cols]){
				continue;
			}
			if (strcasecmp(result[i], "id") == 0){
				rectp->id = strtoll(result[i + cols], NULL, 10); 
			}
			else if (strcasecmp(result[i], "path") == 0)
			{
				strncpy(rectp->path, result[i + cols], sizeof(rectp->path));
			}else if (strcasecmp(result[i], "tpath") == 0)
			{
				strncpy(rectp->to, result[i + cols], sizeof(rectp->to));
			}
			else if (strcasecmp(result[i], "size") == 0)
			{
				strncpy(rectp->size, result[i + cols], sizeof(rectp->size));
			}
			else if (strcasecmp(result[i], "dir") == 0)
			{
				rectp->dir = strtoll(result[i + cols], NULL, 10); 
			}
			else if (strcasecmp(result[i], "md5") == 0)
			{
				strncpy(rectp->md5, result[i + cols], sizeof(rectp->md5));
			}
			else if (strcasecmp(result[i], "pmd5") == 0)
			{
				strncpy(rectp->pmd5, result[i + cols], sizeof(rectp->pmd5));
			}
			else if (strcasecmp(result[i], "crc") == 0)
			{
				strncpy(rectp->crc, result[i + cols], sizeof(rectp->pmd5));
			}			
			else if (strcasecmp(result[i], "ctime") == 0)
			{
				strncpy(rectp->ctime, result[i + cols], sizeof(rectp->ctime));
			}
			else if (strcasecmp(result[i], "mtime") == 0)
			{
				strncpy(rectp->mtime, result[i + cols], sizeof(rectp->mtime));
			}
			else if (strcasecmp(result[i], "opt") == 0)
			{
				rectp->opt = strtoll(result[i + cols], NULL, 10); 
			}
			else if (strcasecmp(result[i], "status") == 0)
			{
				rectp->status = strtoll(result[i + cols], NULL, 10); 
			}
			else if (strcasecmp(result[i], "action") == 0 ||
					strcasecmp(result[i], "ltime") == 0)
			{
				printf("unused filed:%s\n", result[i]);
			}			
			else
			{
				DPRINTF("Unknow type\n");
				sqlite3_free_table(result);
				sqlite3_free(sql);
				va_end(ap);
				return FAILURE;
			}
		}
		if(found == 0){
			memset(lpath, 0, sizeof(lpath));
			if(pcs_convert_path_to_local(rectp->path, lpath) == FAILURE){
				sqlite3_free_table(result);
				sqlite3_free(sql);
				va_end(ap); 
				return FAILURE;
			}				
			if(lstat(lpath, &st) == 0 && 
					st.st_size == atoll(rectp->size)){
				DPRINTF("Found A SAME MD5 Record.....\n");
			}else{
				DPRINTF("No SAME MD5 Record Found\n");	
				memset(rectp, 0, sizeof(rec_t));
			}			
		}
	}
	sqlite3_free_table(result);
	sqlite3_free(sql);
	va_end(ap);
	return SUCCESS;
}


int
sql_get_data_record(rec_t *rectp, sqlite3 *db, char *fmt, ...)
{
	char *sql;
	char **result;
	int rows = 0, cols = 0, i = 0;
	int ret = 0;
	va_list ap;

	if (!rectp){
		DPRINTF("rectp is null\n");
		return FAILURE;
	}
	if (!db){
		DPRINTF("db is null\n");
		cloud_set_errno(ESQL, EDB_NOPEN);
		return FAILURE;
	}

	va_start(ap, fmt);

	sql = sqlite3_vmprintf(fmt, ap);
	if( !sql ){
		cloud_set_errno(ENOR, ENOR_OUTMEM);
		va_end(ap);
		return FAILURE;
	}
	memset(rectp, 0, sizeof(rec_t));
	ret = sql_get_table(db, sql, &result, &rows, &cols);
	if(ret == FAILURE){
		sqlite3_free(sql);
		va_end(ap);
		return FAILURE;
	}
	if (rows > 1){
		DPRINTF("Found %d records in database table\n", rows);
	}
	if( rows  ){
		/* only get the first result */
		for ( i = 0; i < cols; ++i ){		
			if(rows > 1 && rectp->id && strcasecmp(result[i], "id") == 0){
				DPRINTF("Mutil Record Listened, we break out...\n");
				break;
			}
			if ( !result[i + cols]){
				continue;
			}
			if (strcasecmp(result[i], "id") == 0){
				rectp->id = strtoll(result[i + cols], NULL, 10); 
			}
			else if (strcasecmp(result[i], "path") == 0)
			{
				strncpy(rectp->path, result[i + cols], sizeof(rectp->path));
			}else if (strcasecmp(result[i], "tpath") == 0)
			{
				strncpy(rectp->to, result[i + cols], sizeof(rectp->to));
			}
			else if (strcasecmp(result[i], "size") == 0)
			{
				strncpy(rectp->size, result[i + cols], sizeof(rectp->size));
			}
			else if (strcasecmp(result[i], "dir") == 0)
			{
				rectp->dir = strtoll(result[i + cols], NULL, 10); 
			}
			else if (strcasecmp(result[i], "md5") == 0)
			{
				strncpy(rectp->md5, result[i + cols], sizeof(rectp->md5));
			}
			else if (strcasecmp(result[i], "pmd5") == 0)
			{
				strncpy(rectp->pmd5, result[i + cols], sizeof(rectp->pmd5));
			}
			else if (strcasecmp(result[i], "crc") == 0)
			{
				strncpy(rectp->crc, result[i + cols], sizeof(rectp->pmd5));
			}			
			else if (strcasecmp(result[i], "ctime") == 0)
			{
				strncpy(rectp->ctime, result[i + cols], sizeof(rectp->ctime));
			}
			else if (strcasecmp(result[i], "mtime") == 0)
			{
				strncpy(rectp->mtime, result[i + cols], sizeof(rectp->mtime));
			}
			else if (strcasecmp(result[i], "opt") == 0)
			{
				rectp->opt = strtoll(result[i + cols], NULL, 10); 
			}
			else if (strcasecmp(result[i], "status") == 0)
			{
				rectp->status = strtoll(result[i + cols], NULL, 10); 
			}
			else
			{
				DPRINTF("Unknow type\n");
				sqlite3_free_table(result);
				sqlite3_free(sql);
				va_end(ap);
				return FAILURE;
			}
		}
	}
	sqlite3_free_table(result);
	sqlite3_free(sql);
	va_end(ap);
	return SUCCESS;
}

int
sql_get_done_record(done_rec_t *rectp, sqlite3 *db, char *fmt, ...)
{
	char *sql;
	char **result;
	int rows = 0, cols = 0, i = 0;
	int ret = 0;
	va_list ap;

	if (!rectp){
		DPRINTF("rectp is null\n");
		return FAILURE;
	}
	if (!db){
		DPRINTF("db is null\n");
		cloud_set_errno(ESQL, EDB_NOPEN);
		return FAILURE;
	}

	va_start(ap, fmt);

	sql = sqlite3_vmprintf(fmt, ap);
	if( !sql ){
		cloud_set_errno(ENOR, ENOR_OUTMEM);
		va_end(ap);
		return FAILURE;
	}
	memset(rectp, 0, sizeof(done_rec_t));
	ret = sql_get_table(db, sql, &result, &rows, &cols);
	if(ret == FAILURE){
		sqlite3_free(sql);
		va_end(ap);
		return FAILURE;
	}
	if (rows > 1){
		DPRINTF("Found %d records in database table\n", rows);
	}
	if( rows  ){
		/* only get the first result */
		for ( i = 0; i < cols; ++i ){		
			if(rows > 1 && rectp->id && strcasecmp(result[i], "id") == 0){
				DPRINTF("Mutil Record Listened, we break out...\n");
				break;
			}
			if ( !result[i + cols]){
				continue;
			}
			if (strcasecmp(result[i], "id") == 0){
				rectp->id = strtoll(result[i + cols], NULL, 10); 
			}
			else if (strcasecmp(result[i], "path") == 0)
			{
				strncpy(rectp->path, result[i + cols], sizeof(rectp->path));
			}
			else if (strcasecmp(result[i], "size") == 0)
			{
				strncpy(rectp->size, result[i + cols], sizeof(rectp->size));
			}
			else if (strcasecmp(result[i], "dir") == 0)
			{
				rectp->dir = strtoll(result[i + cols], NULL, 10); 
			}
			else if (strcasecmp(result[i], "md5") == 0)
			{		
				strncpy(rectp->md5, result[i + cols], sizeof(rectp->md5));
			}			
			else if (strcasecmp(result[i], "action") == 0)
			{
				rectp->action = strtoll(result[i + cols], NULL, 10); 
			}
			else if (strcasecmp(result[i], "ltime") == 0)
			{
				strncpy(rectp->ltime, result[i + cols], sizeof(rectp->ltime));
			}			
			else if (strcasecmp(result[i], "opt") == 0)
			{
				rectp->opt = strtoll(result[i + cols], NULL, 10); 
			}
			else if (strcasecmp(result[i], "status") == 0)
			{
				rectp->status = strtoll(result[i + cols], NULL, 10); 
			}
			else
			{
				DPRINTF("Unknow type\n");
				sqlite3_free_table(result);
				sqlite3_free(sql);
				va_end(ap);
				return FAILURE;
			}
		}
	}
	sqlite3_free_table(result);
	sqlite3_free(sql);
	va_end(ap);
	return SUCCESS;
}

int pcs_check_local_file(char *filename, size_t size)
{
	struct stat st;
	int ret;
	
	if(!filename || !size){
		return FAILURE;
	}
	if(access(filename, F_OK)){
		return FAILURE;
	}
	if (lstat(filename, &st) == -1){
		printf("lstat %s:%s\n", filename, strerror(errno));
		return FAILURE;
	}

	if(st.st_size != size){
		return FAILURE;
	}
	return SUCCESS;
}
int pcs_update_done_table(PCS_ACTION action, sqlite3 *db, int status, rec_t *record)
{
	char *sql = NULL;
	char **result;
	int64_t detailID = 0;
	int rows, cols, i;	
	int ret, recnum = 0;
	char lpath[PATH_MAX] = {0};
	done_rec_t qrecord;
	
	if(!record){
		return FAILURE;
	}
	if(record->dir == 1){
		return SUCCESS;
	}
	

	ret = pcs_convert_path_to_local(record->path, lpath);
	if(ret == FAILURE){
		DPRINTF("Convert PATH [%s] error\n", record->path);
		return FAILURE;
	}
	memset(&qrecord, 0, sizeof(done_rec_t));
	ret= sql_get_done_record(&qrecord, db, 
			"SELECT * FROM DONE WHERE  "
			"ACTION=%d AND PATH='%q' LIMIT 1", action, lpath);
	if(ret == FAILURE){
		DPRINTF("Get DONE Record error...\n");
		return FAILURE;
	}
	if(qrecord.id == 0){
		ret = sql_exec(db, "INSERT into DONE (PATH, SIZE, DIR, MD5, ACTION, LTIME, OPT, STATUS) "
				" values ('%q', '%q', '%d', '%q', '%d', '%ld', '%d','%d')"
					,lpath, record->size, record->dir, record->md5, 
					action, time(NULL), record->opt, status);
		if(ret == FAILURE){
			DPRINTF("INSERT DONE %s error\n", lpath);
			return FAILURE;
		}			
	}else{
		ret = sql_exec(db, "UPDATE DONE SET PATH='%q', SIZE='%q'"
				", DIR='%d', MD5='%q', ACTION='%d', LTIME='%ld', OPT='%d', STATUS='%d'"
				" WHERE ID=%lld", 
				lpath, record->size, record->dir, record->md5, action, 
				time(NULL), record->opt, status, qrecord.id);
		if(ret == FAILURE){
			DPRINTF("UPDATE DONE %s error\n", lpath);
			return FAILURE;
		}
	}
	/*Check DONE TABLE SIZE*/
	ret = sql_get_int_field(db, &recnum, 
			"SELECT COUNT(*) FROM DONE WHERE ACTION='%d'", action);
	if (ret == FAILURE){
		return FAILURE;
	}
	if(recnum < PCS_DONE_RECORD_MAX){
		return SUCCESS;
	}
	DPRINTF("Reduce DONE TABLE [%s] Records...\n", 
			action==PCS_DOWN_ACTION?"DOWN":"UPLOAD");
	sql = sqlite3_mprintf("SELECT ID FROM DONE WHERE ACTION='%d'" 
			" ORDER BY ID  LIMIT %d OFFSET '%d' " , 
				1, PCS_DONE_RECORD_MAX-PCS_DONE_RECORD);	
	if( (sql_get_table(db, sql, &result, &rows, &cols) == SQLITE_OK)){
		if (rows){
			for( i = cols; i <= (rows ) * cols; i += cols){
				detailID = strtoll(result[i], NULL, 10);
				sql_exec(db, "DELETE FROM DONE WHERE ID<%lld", detailID);
			}
		}
		sqlite3_free_table(result);
	}
	sqlite3_free(sql);
	DPRINTF("Reduce DONE TABLE [%s] Records Successful...\n", 
			action==PCS_DOWN_ACTION?"DOWN":"UPLOAD");

	return SUCCESS;	
}

int pcs_update_down_table(sqlite3 *db, metadata_t *meta)
{
	rec_t qrecord, delrec;
	int ret;
	
	if(!db || !meta){
		DPRINTF("Parameter is error\n");
		return FAILURE;
	}

	memset(&qrecord, 0, sizeof(rec_t));
	/*Lock down table*/
	pthread_mutex_lock(&m_down);
	/*First check delete table*/
	memset(&delrec, 0, sizeof(rec_t));
	sql_get_data_record(&delrec, db, 
		"SELECT * FROM DETABLE WHERE DIR=%d AND PATH = '%q'", 
			meta->isdir, meta->path);
	if(delrec.id){
		printf("User Delete %s Not insert DOWN\n", meta->path);
		pthread_mutex_unlock(&m_down);
		return SUCCESS;
	}
	
	ret= sql_get_data_record(&qrecord, db, 
		"SELECT * FROM DOWN WHERE DIR=%d AND PATH = '%q'", 
			meta->isdir, meta->path);
	if(ret == FAILURE){
		DPRINTF("QUERY DOWN [%s] error\n", meta->path);
		goto exe_err;	
	}
	if(qrecord.id == 0){
		DPRINTF("INSERT %s to DOWN Table...\n", meta->path);
		ret = sql_exec(db, "INSERT into DOWN (PATH, TPATH, SIZE, DIR, MD5, OPT, STATUS) "
				" values ('%q', NULL, '%q', '%d', '%q', '%d','%d')", 
				meta->path, meta->size, meta->isdir, meta->md5, 
				CLOUD_CREATE, CLOUD_WAITING);
		if(ret == FAILURE){
			DPRINTF("INSERT DWON [%s] error\n", meta->path);
			goto exe_err;
		}
	}else if(meta->isdir == 0&&strcmp(qrecord.md5, meta->md5)){
		ret = sql_exec(db, "UPDATE DOWN SET PATH='%q', TPATH='%q', SIZE='%q'"
				", DIR='%d', MD5='%q', OPT='%d', STATUS='%d'"
				" WHERE ID=%lld", meta->path, qrecord.to, 
				meta->size, meta->isdir, meta->md5	, 
				CLOUD_CREATE, qrecord.status, qrecord.id);
		if(ret == FAILURE){
			DPRINTF("UPDATE DWON [%s] error\n", meta->path);
			goto exe_err;
		}
	}
	
	pthread_mutex_unlock(&m_down);
	return SUCCESS;

exe_err:	
	pthread_mutex_unlock(&m_down);
	return FAILURE;
}


int pcs_update_dbtable(sqlite3 *db, metadata_t *meta)
{
	metadata_t cmeta;
	int ret;
	rec_t qrecord;
	char localfile[PATH_MAX] = {0};
	size_t size;
	
	if(!db || !meta){
		DPRINTF("Parameter is error\n");
		return FAILURE;
	}
	/*filter path*/
	memcpy(&cmeta, meta, sizeof(metadata_t));
	rslash(cmeta.path);
	pcs_convert_path_to_local(cmeta.path, localfile);	
	if(meta->isdir == 1){
		if(strlen(localfile)){
			CreateDirectoryRecursive(localfile);
		}
		printf("Pass Dir--------------->%s\n", meta->path);
		return SUCCESS;
	}
	
	size = strtoll(cmeta.size, NULL, 10);
	if(strlen(localfile) &&
			pcs_check_local_file(localfile, size) == SUCCESS){
		printf("Local Dir Have Download %s\n", cmeta.path);
		return SUCCESS;
	}
	ret = pcs_update_down_table(db, &cmeta);
	if(ret == FAILURE){
		DPRINTF("Function pcs_update_down_table excute[%s] error\n", cmeta.path);
	}

	return SUCCESS;
}


int get_download_task(sqlite3  *db, rec_t *record)
{
	rec_t qrecord;
	int ret;
	int pgreset = 0;
	
	if(!db || !record){
		DPRINTF("Parameter is error\n");
		return FAILURE;
	}

	memset(&qrecord, 0, sizeof(rec_t));
	/*Lock down table*/
	pthread_mutex_lock(&m_down);
	/*FILE*/
	ret= sql_get_data_record(&qrecord, db, 
		"SELECT * FROM DOWN WHERE DIR=0 AND STATUS=%d LIMIT 1", CLOUD_WAITING);
	if(ret == FAILURE){
		DPRINTF("Get DownLoad Task FILE error...\n");
		goto exe_error;
	}
	if(qrecord.id){
		goto exe_ok;
	}

	/*DOWNLOAD ERROR FILE*/
	ret= sql_get_data_record(&qrecord, db, 
		"SELECT * FROM DOWN WHERE  STATUS=%d LIMIT 1", CLOUD_FAILED);
	if(ret == FAILURE){
		DPRINTF("Get DownLoad Task FAILED error...\n");
		goto exe_error;
	}
	if(qrecord.id){
		goto exe_ok;
	}else{
		//DPRINTF("No Task Need To Download, Reset DownPRG...\n");
		pgreset = 1;
		goto exe_error;
	}

exe_ok:
	
	/*UPDATE Task Status*/
 	ret = sql_exec(db, "UPDATE DOWN SET STATUS='%d'  WHERE ID=%lld", 
 			 CLOUD_ING, qrecord.id);
	if(ret == FAILURE){
		DPRINTF("UPDATE DWON-->ING [%s] error\n", qrecord.path);
	}
	memcpy(record, &qrecord, sizeof(rec_t));
	pthread_mutex_unlock(&m_down);
	return SUCCESS;

exe_error:
	
	pthread_mutex_unlock(&m_down);
	if(pgreset == 1){
		cloud_clean_downprogress();
	}
	return FAILURE;
}

int set_download_task(sqlite3 *db, rec_t *record, int isdelete)
{
	rec_t qrecord;
	int ret = SUCCESS, status;
	char tmpath[PATH_MAX] = {0};
	
	if(!db || !record){
		DPRINTF("Parameter is error\n");
		return FAILURE;
	}

	memset(&qrecord, 0, sizeof(rec_t));
	/*Lock down table*/
	pthread_mutex_lock(&m_down);
	if(isdelete == SUCCESS || isdelete == 2){
		ret = sql_exec(db, "DELETE FROM DOWN WHERE ID=%lld", record->id);
		DPRINTF("[DOWN] Handle Finish Record..[%s][%s-->%s][OPT=%d]\n",
			 record->dir?"DIR":"FILE", record->path, record->to, 
			 	record->opt);
	}else{
		sql_get_data_record(&qrecord, db, 
			"SELECT * FROM DOWN WHERE ID=%lld", record->id);
		if(qrecord.status == CLOUD_RELOAD || qrecord.status == CLOUD_MYDELETE){
			char topath[PATH_MAX] = {0};
			ret = pcs_convert_path_to_local(record->path, topath);
			if(ret == FAILURE){
				DPRINTF("Convert To Local Error [%s]..\n", record->path);
			}
			sprintf(tmpath, "%s%s", topath, PCS_TMP_FILE);
			if(access(tmpath, F_OK) == 0){
				DPRINTF("REMOVE TMP %s...\n", tmpath);
				remove(tmpath);
			}
			if(qrecord.status == CLOUD_MYDELETE){
				ret = sql_exec(db, "DELETE FROM DOWN WHERE ID=%lld", record->id);
				DPRINTF("[DOWN] Handle MYDELETE Finish Record..[%s][%s-->%s][OPT=%d]\n",
						 record->dir?"DIR":"FILE", record->path, record->to, 
			 					record->opt);					
				pthread_mutex_unlock(&m_down);
				sql_exec(db, "DELETE FROM SERVER WHERE DIR=%d AND PATH='%q'", 
						record->dir, record->path);
				DPRINTF("UPDATE DONE [DOWNLOAD:%d:%s]...\n", 1, record->path);
				pcs_update_done_table(PCS_DOWN_ACTION, db, 1, record);
				return ret;
			}
		}
		if(qrecord.status == CLOUD_RELOAD){
			status = CLOUD_WAITING;
		}else if(qrecord.status == CLOUD_SUSPEND){
			status = CLOUD_SUSPEND;
		}else if(record->status == CLOUD_FAILED){
			status = CLOUD_FAILED;
		}else{
			status = CLOUD_WAITING;
		}
		ret = sql_exec(db, "UPDATE DOWN SET PATH='%q', TPATH='%q', SIZE='%q'"
				", DIR='%d', MD5='%q', OPT='%d', STATUS='%d'"
				" WHERE ID=%lld", record->path, record->to, 
					record->size, record->dir, record->md5, qrecord.opt, status, record->id);
		DPRINTF("[DOWN] Handle Error Record..[%s][%s-->%s][OPT=%d][STATUS=%d]\n",
			 record->dir?"DIR":"FILE", record->path, record->to, record->opt, record->status);	
	}
	
	pthread_mutex_unlock(&m_down);
	/*Update DONE Table*/
	if(isdelete == SUCCESS && record->dir == 0){
		int act;
		act = (isdelete==SUCCESS?0:1);
		DPRINTF("UPDATE DONE [DOWNLOAD:%d:%s]...\n", act, record->path);
		pcs_update_done_table(PCS_DOWN_ACTION, db, act, record);

	}

	return ret;
}

int pcs_multi_down_initinfo(char *path, long long size, char *mdinfo)
{
	int fd;
	long long persize, offset = 0;
	int current, wsize,segnum = 0, threadnum = 0;	
	saveDownloadState dwstate;
	
	if(!path || !size || !mdinfo){
		return FAILURE;
	}	
	memset(mTask, 0, sizeof(muti_taskinfo) *PCS_MULTI_DOWNNUM);

	if(size < PCS_MULTI_DOWNLIMIT){
		threadnum = 1;
		persize= size;
	}else{
		threadnum = PCS_MULTI_DOWNNUM;
		persize = size / PCS_MULTI_DOWNNUM;
	}
	for(current = 0; current < threadnum; current++){
		memcpy(mTask[current].fileDown, path, strlen(path));
		mTask[current].seginfo.threadNum = current+1;
		mTask[current].seginfo.sp = offset;		
		mTask[current].seginfo.ds = 0;
		if(current+1 == threadnum){			
			mTask[current].seginfo.ep = size-1;			
		}else{		
			mTask[current].seginfo.ep = offset+persize-1;
		}		
		mTask[current].downSzie= mTask[current].seginfo.ep-mTask[current].seginfo.sp+1;
		offset += persize;
		segnum++;
	}
	/*Update Info*/
	fd = open(mdinfo, O_WRONLY);
	if(fd < 0){
		DPRINTF("Open %s Failed...\n", mdinfo);		
		memset(mTask, 0, sizeof(muti_taskinfo) *PCS_MULTI_DOWNNUM);
		return FAILURE;
	}
	
	lseek(fd, size, SEEK_SET);
	dwstate.down_magic = THREAD_STATE_MAGIC;
	dwstate.segnum = segnum;
	write(fd, &dwstate, sizeof(dwstate));
	for(current = 0; current < PCS_MULTI_DOWNNUM; current++){
		if(mTask[current].seginfo.threadNum == 0){
			continue;
		}
		wsize = write(fd, &(mTask[current].seginfo), sizeof(segment));
		if(wsize != sizeof(segment)){
			DPRINTF("Write To %s Failed...\n", mdinfo);
			if(errno == ENOSPC){				
				cloud_set_errno(ESTORAGE, ENOR_DISKFULL);
			}
			memset(mTask, 0, sizeof(muti_taskinfo) *PCS_MULTI_DOWNNUM);
			close(fd);
			remove(mdinfo);
			return FAILURE;
		}
	}
	close(fd);
	
	return SUCCESS;
}

int pcs_multi_initinfo_from_file(char *down, char *mdinfo, size_t size)
{
	int fd;
	int current;
	saveDownloadState dwstate;
	segment *seginfo;

	if(!mdinfo || !size || !down){
		DPRINTF("Paramter is Error...\n");
		return FAILURE;
	}

	fd = open(mdinfo, O_RDONLY);
	if(fd < 0){
		DPRINTF("Open %s Failed...\n", mdinfo);
		return FAILURE;
	}

	lseek(fd, size, SEEK_SET);

	if(read(fd, &dwstate, sizeof(saveDownloadState)) != sizeof(saveDownloadState) ||
			dwstate.down_magic != THREAD_STATE_MAGIC){
		DPRINTF("Read %s Failed or Magic Error...\n", mdinfo);
		close(fd);
		remove(mdinfo);		
		return FAILURE;
	}
	
	seginfo = calloc(1, sizeof(segment) *dwstate.segnum);
	if(!seginfo){
		close(fd);
		return FAILURE;
	}
	if(read(fd, seginfo, sizeof(segment) *dwstate.segnum) 
			!= sizeof(segment) *dwstate.segnum){
		DPRINTF("Read  Segment error:%s..\n", mdinfo);
		close(fd);
		remove(mdinfo);
		free(seginfo);
		return FAILURE;
	}
	close(fd);
	
	memset(mTask, 0, sizeof(muti_taskinfo) *PCS_MULTI_DOWNNUM);
	for(current = 0; current < PCS_MULTI_DOWNNUM; current++){
		memcpy(mTask[current].fileDown, down, strlen(down));
		if(current < dwstate.segnum){
			memcpy(&(mTask[current].seginfo), &seginfo[current], sizeof(segment));
			mTask[current].downSzie = mTask[current].seginfo.ep-mTask[current].seginfo.sp+1;
		}
	}
	
	free(seginfo);

	return SUCCESS;
}

int pcs_create_tmp_down_file(char *path, long long size)
{
	int fd;
	char dirpath[PATH_MAX] = {0}, *pdir = NULL;

	/*we need to mkdir*/
	memcpy(dirpath, path, strlen(path));
	pdir = dirname(dirpath);	
	if (pdir && access(pdir, F_OK) != 0){		
		CreateDirectoryRecursive(pdir); 
	}

	fd = open(path, O_CREAT|O_WRONLY, 0755);
	if(fd < 0){
		DPRINTF("Create %s Failed...\n", path);
		if(errno == ENOSPC){
			return 3;
		}
		return FAILURE;
	}
	if(ftruncate64(fd, size) != 0){
		DPRINTF("ftruncate64 [%s] %lldBytes Failed...\n", path, size);
		close(fd);		
		remove(path);
		if(errno == ENOSPC){
			return 3;
		}
		return FAILURE;
	}
	
	close(fd);
	DPRINTF("Create Tmp Cache File Successful...[%s]\n", path);

	return SUCCESS;
}


int pcs_download_chk_same_md5(sqlite3 *db, rec_t * record)
{
	int ret;
	rec_t query;
	char lpath[PATH_MAX] = {0}, tmpfile[PATH_MAX] = {0};
	char dpath[PATH_MAX] = {0}, buffer[8192] = {0}, *ptr;
	struct stat st, dst;
	int tfd, sfd;
	long long cpsize, size;
	int readlen, writelen;
	
	if(!record){
		DPRINTF("Parameter is Error....\n");
		return FAILURE;
	}
	memset(&query, 0, sizeof(rec_t));
	ret = sql_get_same_md5_record(&query, db, "SELECT * FROM DONE  "
			"WHERE MD5='%q' AND DIR=%d", record->md5, record->dir);
	if(ret == FAILURE || query.id == 0){
		return FAILURE;
	}
	DPRINTF("We Found MD5 is Same [%s]<-->[%s], No need To Download again...\n", 
			query.path, record->path);
	
	if(pcs_convert_path_to_local(record->path, lpath) == FAILURE
		|| pcs_convert_path_to_local(query.path, dpath) == FAILURE){
		return FAILURE;
	}	
	if((sfd = open(dpath, O_RDONLY)) <0 || lstat(tmpfile, &dst)){
		DPRINTF("OPEN: [%s]  error. [errmsg: %s]\n", 
				dpath, strerror(errno));
		return FAILURE;
	}
	sprintf(tmpfile, "%s%s", lpath, PCS_TMP_FILE);
	if(lstat64(tmpfile, &st) == 0){
		DPRINTF("TMP FILE [%s] Exist...\n", tmpfile);	
		cpsize = st.st_size;
	}else{
		char dirpath[PATH_MAX] = {0}, *pdir = NULL;
		/*we need to mkdir*/
		memcpy(dirpath, lpath, strlen(lpath));
		pdir = dirname(dirpath);	
		if (pdir && access(pdir, F_OK) != 0){		
			CreateDirectoryRecursive(pdir);	
		}
		cpsize = 0;
	}
	if((tfd = open(tmpfile, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR)) <0){
		DPRINTF("OPEN: [%s] error. [errmsg: %s]\n", 
				tmpfile, strerror(errno));
		if(errno == ENOSPC){			
			return 2;
		}		
		return FAILURE;
	}
	if(st.st_size){
		lseek(tfd, cpsize, SEEK_SET);
		lseek(sfd, cpsize, SEEK_SET);
	}
	/*Copy the file*/
	size = dst.st_size;
	while(cpsize < size){
		if(cloud_check_condition() != 0){
			DPRINTF("Base Condition Change, Down INTERUPT....\n");
			close(tfd);
			close(sfd);
			return 2;
		}
		readlen = read(sfd, buffer, 8192);
		if(readlen < 0){
			if(errno == EAGAIN||
				errno == EINTR){
				continue;
			}
			close(tfd);
			close(sfd);
			return FAILURE;			
		}else if(readlen == 0){
			DPRINTF("Read Finish...\n");
			break;
		}
		ptr = buffer;
		while((writelen = write(tfd, ptr, readlen))){
			if(writelen == readlen){
				break;
			}else if(writelen>0){
				ptr+=writelen;
				readlen-=writelen;
			}else if(writelen == -1){
				if(errno == EINTR){
					continue;
				}
				close(tfd);
				close(sfd);
				if(errno == ENOSPC){					
					return 2;
				}
				return FAILURE;
			}
		}
		cpsize += readlen;
		memset(buffer, 0, sizeof(buffer));
		if(cpsize % PCS_MULTI_DOWNLIMIT == 0){
			pthread_mutex_lock(&m_down);
			downprogress.now = cpsize;
			pthread_mutex_unlock(&m_down);				
		}
	}	
	close(tfd);
	close(sfd);
	/*Move Name*/
	if ( lpath && access(lpath, F_OK) == 0 &&
		record->opt == CLOUD_CREATE){
		
		char oldpath[PATH_MAX] = {0}, *dot;
		int len;
		
		dot = strrchr(lpath, '.');
		if(!dot){
			sprintf(oldpath, "%s(%ld)", lpath, time(NULL));
		}else{
			*dot = '\0';
			len = sprintf(oldpath, "%s(%ld)", lpath, time(NULL));
			*dot = '.';
			sprintf(&oldpath[len], "%s", dot);

		}
		DPRINTF("[DOWN] Rename %s to %s\n", lpath, oldpath);
		rename(lpath, oldpath);
	}else if( lpath && access(lpath, F_OK) == 0 &&
			 record->opt == CLOUD_UPDATE){
		DPRINTF("[DOWN] UPDATE %s, So Remove It First....\n", lpath);
		remove(lpath);
	}
	rename(tmpfile, lpath);
	
	return SUCCESS;	
}

int pcs_multi_down_checkhdr(char *filename, size_t filesize)
{
	int fd;
	saveDownloadState dwstate;

	if(!filename){
		return FAILURE;
	}
	if((fd = open(filename, O_RDONLY)) < 0){
		DPRINTF("OPEN: [%s] error. [errmsg: %s]\n", 
				filename, strerror(errno));
		return FAILURE;
	}
	lseek(fd, filesize, SEEK_SET);

	read(fd, &dwstate, sizeof(saveDownloadState));
	close(fd);

	return ((dwstate.down_magic == THREAD_STATE_MAGIC)?SUCCESS:FAILURE);
}

int pcs_multi_download_file(rec_t * record)
{
	int ret, initdown = 0, second = 0, encry = 0;
	long long size = 0;
	char lpath[PATH_MAX] = {0};
	char *ptr = NULL, dtmp[PATH_MAX] = {0}, tmpath[PATH_MAX] = {0};

	if(!record){
		DPRINTF("Parameter is error\n");
		return FAILURE;
	}
	/*Get cahche path and mutil-down info*/
	if(pcs_convert_path_to_local(record->path, lpath) == FAILURE){
		return FAILURE;
	}
	ptr = strrchr(lpath, '/');
	if(ptr == NULL){
		DPRINTF("Can Not Find / in %s...\n", lpath);
		return FAILURE;
	}
	
	size = strtoll(record->size, NULL, 10);
	if(size == 0){
		printf("Size is 0[%s]\n", size, record->size);
		return SUCCESS;
	}
	if(access(lpath, F_OK) == 0){
		struct stat st;
		if(lstat(lpath, &st) == 0 ||
				st.st_size == size){
			DPRINTF("Local Exist %s\n", lpath);
			return SUCCESS;
		}
	}
	/*Get tmpdown file*/
	memset(dtmp, 0, sizeof(dtmp));
	sprintf(dtmp, "%s%s", lpath, PCS_TMP_FILE);

	ret = access(dtmp, F_OK);
	if(ret != 0){
		initdown = 1;
	}else if(ret == 0){
		struct stat st;
		if(lstat(dtmp, &st) != 0 ||
			st.st_size <= size || pcs_multi_down_checkhdr(dtmp, size) == FAILURE){
			DPRINTF("Cache File Is Not Match...[st.st_size=%lld size=%lld]\n", 
					st.st_size, size);
			initdown = 2;	
		}
	}else{
		initdown = 2;
	}

	if(initdown){
		DPRINTF("Create %lldBytes[%s]...\n", size, dtmp);
		cloud_set_downprogress_status(PCS_DOWN_CRT_TMP);
		ret = pcs_create_tmp_down_file(dtmp, size);
		if(ret == 3){
			DPRINTF("Space Not Enough....\n");			
			cloud_set_errno(ESTORAGE, ENOR_DISKFULL);
			return FAILURE;			
		}else if(ret == 2){
			return 2;
		}else if(ret == FAILURE){
			return FAILURE;
		}
		ret = pcs_multi_down_initinfo(record->path, size, dtmp);
		if(ret == FAILURE){
			DPRINTF("Get Multi-Down Info Failed...\n");
			return FAILURE;
		}
	}else{
		DPRINTF("Read Multi-Download Info\n");
		ret = pcs_multi_initinfo_from_file(record->path, dtmp, size);
		if(ret == FAILURE){
			DPRINTF("Get Multi-Down Info Failed...\n");
			return FAILURE;
		}		
	}

	pcs_multi_down_debug();
	/*Set The Multi-Down Thread to Work*/
	DPRINTF("Set Multi-DownLoad Start....\n");
	pcs_multi_down_setstart();	
	cloud_set_downprogress_status(PCS_DOWN_ING);
	while(1){
		if(downprogress.interp == 1||
				cloud_check_condition() != 0){
			DPRINTF("We Need To Interupted Multi-Down...\n");
			pcs_multi_down_set_interupte(dtmp, size);
			return 2;
		}
		/*Judge Thread Run Condition*/
		ret = pcs_multi_down_chk_condition(dtmp, size);
		if(ret == FAILURE){
			DPRINTF("Multi-Down %s Failed....\n", lpath);
			return FAILURE;
		}
		/*1s update downpgress info*/
		if(second % 2 == 0){
			pcs_multi_down_update_progress();
		}else if((second+1) % 20 == 0){
			/*10 second update multi-down info*/
//			DPRINTF("Update Multi-Down File Info.....\n");
			pcs_multi_down_update_mdinfo(dtmp, size);
		}
		ret = pcs_multi_down_chk_finish(dtmp, lpath, size);
		if(ret == 3){	
			pcs_multi_down_set_interupte(dtmp, size);
			remove(dtmp);
			return 3;
		}else if(ret == SUCCESS){
			DPRINTF("Multi-Down %s Finish %s...\n", record->path, 
					encry == 1?"Decryption File":"No Encryption");
			return SUCCESS;
		}else if(ret == FAILURE){
			DPRINTF("Multi-Down Stop...[%d]\n", ret);
			return ret;
		}
		second++;
		usleep(500000);
	}

	return FAILURE;
}


int  pcs_download_handle(sqlite3 *db, rec_t *record)
{
	char lpath[PATH_MAX];
	int ret = SUCCESS;
	int count = 0;

	if(!record){
		DPRINTF("Parameter is error\n");
		return FAILURE;
	}

	if(pcs_convert_path_to_local(record->path, lpath) == FAILURE){
		return FAILURE;
	}
	if(record->dir == 1){ 
		if(access(lpath, F_OK)){
			DPRINTF("Make DIR [%s]\n", lpath);
			CreateDirectoryRecursive(lpath);
		}
		return SUCCESS;
	}else if(record->dir == 0){
		/*Check if The File is exist the same record*/
		ret = pcs_download_chk_same_md5(db, record);
		if(ret == SUCCESS){
			DPRINTF("Copy [%s] From Local Finish....\n", record->path);
			return SUCCESS;
		}else if(ret == 2){
			DPRINTF("Copy Interupted....\n");
			return FAILURE;
		}
		
		while(count++ < 10){
			if(cloud_check_condition() != 0){
				DPRINTF("Base Condition Change, Down INTERUPT....\n");
				return FAILURE;
			}
			ret = pcs_multi_download_file(record);
			if(ret == FAILURE){
				DPRINTF("DOWNLOAD [%s] [try %d]error....\n", record->path, count);
			}else if(ret == 2){
				DPRINTF("Down STOP, INTERUPT by Callback...\n");
				return FAILURE;
			}else if(ret == 3){			
				DPRINTF("[%s] Not Exist In Server, We Think Down Successful...\n", 
						record->path);
				return 2;
			}else{
				DPRINTF("CURL Successful [%s], Check File Size\n", record->path);
				if(pcs_chk_downfile_size(record) == SUCCESS){
					return SUCCESS;
				}
			}
			sleep(10);
		}
		if(count >= 10){
			DPRINTF("Set Task Status to FAILED[%s]\n", record->path);
			record->status = CLOUD_FAILED;
			return FAILURE;
		}
	}

	DPRINTF("Unknow OPERATION TYPE=%d [%s]\n", record->dir,
				record->path);
	return SUCCESS;
}

int pcs_get_localdir(char *dirname)
{
	struct mntent *mnt; 	
	FILE *fp = NULL;
	long long freesize = 0, maxsize = 0; 	
	struct statfs st;

	fp = setmntent("/proc/mounts", "r");
	if(fp == NULL){
		return -1;
	}
	while ((mnt = getmntent(fp))){
		if (strstr(mnt->mnt_dir, "Volume")){
			/*We need to check partition*/
			if(check_dev(mnt->mnt_fsname) == FAILURE){
				continue;
			}
			/*Check partition is write able*/
			if(check_partrdonly(mnt->mnt_dir) == 1){
				DPRINTF("************The partion is: \"%s\" Readonly\n", mnt->mnt_dir);
				continue;
			}
			if(!statfs(mnt->mnt_dir, &st) && (st.f_blocks > 0)){
				freesize = (unsigned long long)st.f_bsize * (unsigned long long)st.f_bavail;
				if(freesize > maxsize){
					strcpy(dirname, mnt->mnt_dir);
				}
			}
		}
	}	
	endmntent(fp);

	if(maxsize == 0){
		DPRINTF("Not Found Avaiable Partition\n");
		return -1;
	}
	return 0;
}

static int combin_with_remote_dir_files(webContext *context, const char *remote_dir, int recursive, int *total_cnt)
{
	PcsFileInfoList *list = NULL;
	PcsFileInfoListIterater iterater;
	PcsFileInfo *info = NULL;
	int page_index = 1,
		page_size = 1000;
	int cnt = 0, second;
	metadata_t meta;

	while (1) {
		list = pcs_list(context->pcs, remote_dir,
			page_index, page_size,
			"name", PcsFalse);
		if (!list) {
			if (pcs_strerror(context->pcs)) {
				fprintf(stderr, "Error: %s \n", pcs_strerror(context->pcs));
				if (context->timeout_retry && strstr(pcs_strerror(context->pcs), "Can't get response from the remote server") >= 0) {
					second = 10;
					while (second > 0) {
						printf("Retry after %d second...\n", second);
						sleep(1);
						second--;
					}
					//printf("Retrying...\n");
					continue;
				}
				return -1;
			}
			char localfile[PATH_MAX] = {0};
			printf("Empty---->%s\n", remote_dir);
			pcs_convert_path_to_local(remote_dir, localfile);	
			if(strlen(localfile)){
				CreateDirectoryRecursive(localfile);
			}			
			return 0;
		}

		cnt = list->count;
		if (total_cnt) (*total_cnt) += cnt;
		if (total_cnt && cnt > 0) {
			printf("Fetch %d Files\n", *total_cnt);
			fflush(stdout);
		}

		pcs_filist_iterater_init(list, &iterater, PcsFalse);
		while (pcs_filist_iterater_next(&iterater)) {
			info = iterater.current;			
			printf("List---->%s\n", info->path);
			if(info->size == 0){
				continue;
			}else if(info->path && 
					info->path[strlen(remote_dir) + 1] == '.'){
				continue;
			}
			memset(&meta, 0, sizeof(meta));			
			sprintf(meta.fs_id, "%llu", info->fs_id);
			strcpy(meta.path, info->path);
			sprintf(meta.ctime, "%ld", info->server_mtime);
			sprintf(meta.mtime, "%ld", info->server_mtime);
			if(info->md5)
				strcpy(meta.md5, info->md5);
			sprintf(meta.size, "%lld", info->size);
			meta.isdir = info->isdir;
			meta.ifhassubdir= info->ifhassubdir;
			meta.isdelete= 0;

			if(pcs_update_dbtable(context->db, &meta) == FAILURE){
				pcs_filist_destroy(list); 
				return -1;
			}
		}

		if (recursive) {
			pcs_filist_iterater_init(list, &iterater, PcsFalse);
			while (pcs_filist_iterater_next(&iterater)) {
				info = iterater.current;
				if (info->isdir) {
					if (combin_with_remote_dir_files(context, info->path, recursive, total_cnt)) {
						pcs_filist_destroy(list); 
						return -1;
					}
				}
			}
		}
		pcs_filist_destroy(list);
		if (cnt < page_size) {
			break;
		}
		page_index++;
	}
	return 0;
}

void *thread_login(void *arg)
{
	webContext *context = (webContext *)(arg);
	PcsRes pcsres;
	int state = LOGIN_FAILED;
	Pcs *pcs = NULL;
	
	printf("Login Thread %d Start\n", pthread_self());
	/*We need to renew pcs struct*/
	pcs = create_pcs(context);
	if(!pcs){
		printf("Login Thread Create PCS Failed\n");		
		state = LOGIN_FAILED;		
		goto log_finish;
	}
	pcs_clone_userinfo(pcs, context->pcs);
	
	pcsres = pcs_login(pcs);
	if(pcsres == PCS_OK){
		/*We Neet to clone back*/
		pcs_clone_userinfo(context->pcs, pcs);
		printf("Login Success. UID: %s\n", pcs_sysUID(context->pcs));	
		state = LOGIN_OK;
	}else{
		char errmsg[4096] = {0}, *pstr = NULL;
		int errornum = 0;

		strcpy(errmsg, pcs_strerror(context->pcs));
		printf("Login Failed: %s\n", errmsg);
		
		pstr = strstr(errmsg, "error: ");
		if(pstr == NULL){
			printf("Response Error, Not found error string\n");
			state = LOGIN_FAILED;
		}
		errornum = atoi(pstr+strlen("error: "));
		if(errornum == 1 || errornum == 2){
			state = LOGIN_BADUSER;
		}else if(errornum == 4 || errornum == 120021 ||
			errornum == 7){
			state = LOGIN_BADPASSWD;
		}else if(errornum == 3 || errornum == 6 ||
			errornum == 257 || errornum == 200010){
			state = LOGIN_BADVERIFY;
		}else{
			state = LOGIN_FAILED;
		}
	}
	printf("Destory Login PCS Struct[Just for save cookie]\n");
	destroy_pcs(pcs);
	
log_finish:	
	write(context->login_fd, &state, sizeof(state));
	return NULL;
}

void *thread_server(void *arg)
{
	webContext *context = (webContext *)(arg);
	struct timeval now;
	struct timespec outtime;
	long long size;
	time_t count = 0;
	PcsRes pcsres;
	int64_t quota, used;
	int total;
	
	while(PcsTrue){
		pthread_mutex_lock(&m_mutex);
		while(context->islogin == 0 || 
				context->syncon == 0 || 
					networkStatus){
			gettimeofday(&now, NULL);
			outtime.tv_sec = now.tv_sec + 60;
			outtime.tv_nsec = now.tv_usec * 1000;					
			pthread_cond_timedwait(&m_cond, &m_mutex, &outtime);
			if(networkStatus){
				/*Check network status*/
				if(pcs_connect_server() == SUCCESS){
					cloud_set_errno(ENETWORK, 0);
				}
			}
		}
		pthread_mutex_unlock(&m_mutex);

		if(count%100 == 0){
			pcsres = pcs_quota(context->pcs, &quota, &used);
			if (pcsres != PCS_OK) {
				fprintf(stderr, "Error: %s\n", pcs_strerror(context->pcs));
				usleep(500000);
				continue;
			}
			fprintf(stderr, "Capatity info:%lld==>%lld-->%lldbytes\n", quota, size, used);
			size = strtoll(context->used, NULL, 10);
			if(used -size > 10*1024*1024 || count == 0){
				fprintf(stderr, "Used Capatity changed: %lld-->%lldbytes\n", size, used);
				sprintf(context->used, "%lld", used);
				sprintf(context->quota, "%lld", quota);
				total = 0;
				combin_with_remote_dir_files(context, context->workdir, 1, &total);
			}
			/*check network*/
			if(pcs_connect_server() != SUCCESS){
				DPRINTF("Browse baidu is Not OK, BAD Network\n");
				cloud_set_errno(ENETWORK, ENOR_NETBAD);
			}else{		
				DPRINTF("Network is OK\n");
				cloud_set_errno(ENETWORK, 0);
			}
			/*Check Storage*/
			if(storageStatus){
				long long freesize = 0; 		
				long long capsize;				
				struct statfs st;
				if(!statfs(context->localdir, &st) && (st.f_blocks > 0)){
					freesize = (unsigned long long)st.f_bsize * (unsigned long long)st.f_bavail;
					if(freesize > 100*1024*1024){
						DPRINTF("Disk Free Space is %lldByte > %lldBytes , Rest errno\n", freesize, capsize);						
						cloud_set_errno(ESTORAGE, 0);
					}
				}
			}
		}
		if(count++ == 0xFFFFFFFF){
			count=0;
		}
		usleep(800000);
		
	}
}

void *thread_down_control(void *arg)
{
	webContext *context = (webContext *)(arg);
	int ret;
	rec_t task;

	while(PcsTrue){
		pthread_mutex_lock(&m_mutex);
		while(context->islogin == 0 || 
				context->syncon == 0 ||
					networkStatus || storageStatus){
			printf("Stop Download....\n");
			downloadStart = 0;
			pthread_cond_wait(&m_cond, &m_mutex);
		}		
		downloadStart = 1;
		printf("Start Download....\n");		
		pthread_mutex_unlock(&m_mutex);

		memset(&task, 0, sizeof(rec_t));
		ret = get_download_task(context->db, &task);
		if(ret == FAILURE){
			usleep(500000);
			continue;
		}
		/*set download*/
		cloud_set_downprogress(DOWN_ACTION, &task);
		DPRINTF("[DOWN] Excute A Record..[%s][%s][OPT=%d]\n",
			 task.dir?"DIR":"FILE", task.path,task.opt);
		
		ret = pcs_download_handle(context->db, &task);
		set_download_task(context->db, &task, ret);
			
	}
}

void *thread_down_task(void *arg)
{
	int current = (int)(arg), cur;
	char remtefile[2048] ={0}, localfile[2048] = {0};
	char *dir;
	int fd, ret;
	PcsRes res;
	
	if (setpriority(PRIO_PROCESS, 0, 19) == -1){
		DPRINTF("Failed to reduce sync download thread priority\n");
	}
	while(PcsTrue){
		/*We need wait dowload task*/
		if(downloadStart == 0){
			usleep(200000);
			continue;
		}
		/*Get Task*/
		if(mTask[current].CondThread != PCS_MULTI_RUN||
				mTask[current].seginfo.threadNum == 0){
			usleep(500000);
			continue;
		}
		memset(localfile, 0, sizeof(localfile));
		memset(remtefile, 0, sizeof(remtefile));
		
		pcs_convert_path_to_local(mTask[current].fileDown, localfile);
		strcpy(remtefile, mTask[current].fileDown);

		DPRINTF("CUID#%d: Multi-Down Thread [%s][sp:%lld-->ep:%lld/dl:%lld] Begin...\n", 
				mTask[current].seginfo.threadNum, mTask[current].fileDown, 
				mTask[current].seginfo.sp, mTask[current].seginfo.ep, mTask[current].seginfo.ds);

		/*创建目录*/
		dir = base_dir(localfile, -1);
		if (dir){
			if (CreateDirectoryRecursive(dir) != MKDIR_OK) {
				pcs_free(dir);
				ret = -1;
				goto finish_dw;
			}
			pcs_free(dir);
		}
		char tmpdownfile[2048] = {0};
		sprintf(tmpdownfile, "%s%s", localfile, PCS_TMP_FILE);
		if((fd = open(tmpdownfile, O_WRONLY)) < 0){
			DPRINTF("OPEN: [%s] error. [errmsg: %s]\n", 
					tmpdownfile, strerror(errno));
			ret = -1;
			goto finish_dw;
		}
		lseek(fd, 0L, SEEK_SET);
		ret = lseek(fd, mTask[current].seginfo.sp+mTask[current].seginfo.ds, SEEK_SET);
		if(ret == -1){
			DPRINTF("Fseek %s Failed...\n", tmpdownfile);
			ret = -1;
			goto finish_dw;
		}

		/*Begin to download*/		
		Pcs *pcs = NULL;
		pcs = create_pcs(&contexWeb);
		if (!pcs) {
			ret = -1;
			goto finish_dw;
		}
		pcs_clone_userinfo(pcs, contexWeb.pcs);
		pcs_setopts(pcs,
			PCS_OPTION_DOWNLOAD_WRITE_FUNCTION, &write_callback_multi_down,
			PCS_OPTION_DOWNLOAD_WRITE_FUNCTION_DATA, (void*)&fd,
			PCS_OPTION_PROGRESS_FUNCTION, &trans_callback_multi_down,
			PCS_OPTION_PROGRESS_FUNCTION_DATE, (void*)&mTask[current],
			PCS_OPTION_END);
		res = pcs_download(pcs, remtefile, 0, 
				mTask[current].seginfo.sp+mTask[current].seginfo.ds, mTask[current].seginfo.ep- (mTask[current].seginfo.sp+mTask[current].seginfo.ds)+1);
		if(res != PCS_OK){
			if(atoi(pcs_strerror(pcs)) == 404){				
				ret = -2;
			}else{
				printf("Download slice failed, retry delay 10 second, tid: %p. message: %s\n", pthread_self(), pcs_strerror(pcs));
				ret = -1;
			}
		}

	finish_dw:
		close(fd);
		pthread_mutex_lock(&m_down);		
		mTask[current].CondThread = PCS_MULTI_STOP;
		if(ret == -1){
			mTask[current].reCode= PCS_RECODE_ERR;
		}else if(ret == -2){
			mTask[current].reCode= PCS_RECODE_OK_NFOUND;
		}else{
			mTask[current].reCode = PCS_RECODE_OK;
		}
		if(pcs){
			destroy_pcs(pcs);
		}
		pthread_mutex_unlock(&m_down);
	}
}

/*hood cJSON 库中分配内存的方法，用于检查内存泄漏*/
static void hook_cjson()
{
	cJSON_Hooks hooks = { 0 };
	cJSON_InitHooks(&hooks);
}

/*显示上传进度*/
static int upload_progress(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
	char *path = (char *)clientp;
	static char tmp[64];

	tmp[63] = '\0';
	if (path)
		printf("Upload %s ", path);
	printf("%s", pcs_utils_readable_size(ulnow, tmp, 63, NULL));
	printf("/%s      \r", pcs_utils_readable_size(ultotal, tmp, 63, NULL));
	fflush(stdout);

	return 0;
}

/*输出验证码图片，并等待用户输入识别结果*/
static PcsBool verifycode(unsigned char *ptr, size_t size, char *captcha, size_t captchaSize, void *state)
{
	webContext *context = (webContext *)state;
	const char *savedfile;
	FILE *pf;
    fd_set fdset, fderr;
    struct timeval tv;	
	int rc, status = 0;

	savedfile = context->captchafile;

	pf = fopen(savedfile, "wb");
	if (!pf) {
		printf("Can't save the captcha image to %s.\n", savedfile);
		return PcsFalse;
	}
	fwrite(ptr, 1, size, pf);
	fclose(pf);

	/*notify to unblock*/
	status = LOGIN_VERIFY;
	if(write(context->login_fd, &status, sizeof(status))<=0){
		printf("Notify To Client---->Need VerifyCode\n");
	}
	printf("The captcha image at %s.\nPlease input the captcha code: ", savedfile);
verify_try:	
	FD_ZERO(&fdset);
	FD_SET(context->verify_fd, &fdset);
	FD_SET(context->verify_fd, &fderr);
	tv.tv_sec = 20;
	tv.tv_usec = 0;
	rc = select(context->verify_fd+1, &fdset, NULL, &fderr, &tv);
	printf("select result=%d tv.tv_sec=%d\n", rc, tv.tv_sec);
	if(rc == 0){
		printf("Receive Verify Code Time out\n");
		return PcsFalse;
	}else if(rc == -1){
		if(errno = EINTR){
			goto verify_try;
		}
		printf("Receive Verify Code-->Select Error:%d\n", rc);
		return PcsFalse;
	}
	if(FD_ISSET(context->verify_fd, &fderr) ||
			!FD_ISSET(context->verify_fd, &fdset)){
		printf("Receive Verify Code-->fderr Check or No read data\n");
		return PcsFalse;
	}
	if(read(context->verify_fd, captcha, captchaSize) <= 0){
		printf("Receive Verify Code-->Read Error:%s\n", strerror(errno));
		return PcsFalse;
	}

	printf("Receive User Input Verify Code:%s\n", captcha);
	
	return PcsTrue;
}

static PcsBool input_str(const char *tips, char *value, size_t valueSize, void *state)
{
	webContext *context = (webContext *)state;
	fd_set fdset, fderr;
	struct timeval tv;	
	int rc;

	printf("%s", tips);
verify_try: 
	FD_ZERO(&fdset);
	FD_SET(context->verify_fd, &fdset);
	FD_SET(context->verify_fd, &fderr);
	tv.tv_sec = 20;
	tv.tv_usec = 0;
	rc = select(context->verify_fd+1, &fdset, NULL, &fderr, &tv);
	printf("select result=%d tv.tv_sec=%d\n", rc, tv.tv_sec);
	if(rc == 0){
		printf("Receive Verify Code Time out\n");
		return PcsFalse;
	}else if(rc == -1){
		if(errno = EINTR){
			goto verify_try;
		}
		printf("Receive Verify Code-->Select Error:%d\n", rc);
		return PcsFalse;
	}
	if(FD_ISSET(context->verify_fd, &fderr) ||
			!FD_ISSET(context->verify_fd, &fdset)){
		printf("Receive Verify Code-->fderr Check or No read data\n");
		return PcsFalse;
	}
	if(read(context->verify_fd, value, valueSize) <= 0){
		printf("Receive Verify Code-->Read Error:%s\n", strerror(errno));
		return PcsFalse;
	}

	printf("Receive User Input Content:%s\n", value);
	
	return PcsTrue;
}

/*初始化PCS*/
static Pcs *create_pcs(webContext *context)
{
	Pcs *pcs = pcs_create(context->cookiefile);
	if (!pcs) return NULL;
	pcs_setopt(pcs, PCS_OPTION_CAPTCHA_FUNCTION, (void *)&verifycode);
	pcs_setopt(pcs, PCS_OPTION_CAPTCHA_FUNCTION_DATA, (void *)context);
    pcs_setopt(pcs, PCS_OPTION_INPUT_FUNCTION, (void *)&input_str);
    pcs_setopt(pcs, PCS_OPTION_INPUT_FUNCTION_DATA, (void *)context);
	pcs_setopts(pcs,
		PCS_OPTION_PROGRESS_FUNCTION, (void *)&upload_progress,
		PCS_OPTION_PROGRESS, (void *)((long)PcsFalse),
		PCS_OPTION_USAGE, (void*)context->user_agent,
		PCS_OPTION_CONNECTTIMEOUT, (void *)((long)CONNECTTIMEOUT),
		PCS_OPTION_END);
	return pcs;
}

static void destroy_pcs(Pcs *pcs)
{
	pcs_destroy(pcs);
}

/*把上下文转换为字符串*/
static char *context2str(webContext *context)
{
	char *json;
	cJSON *root, *item;

	root = cJSON_CreateObject();
	if(!root)
		return NULL;

	item = cJSON_CreateString(context->cookiefile);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}	
	cJSON_AddItemToObject(root, "cookiefile", item);
	item = cJSON_CreateString(context->captchafile);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "captchafile", item);

	item = cJSON_CreateString(context->workdir);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "workdir", item);

	item = cJSON_CreateString(context->localdir);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "localdir", item);

	item = cJSON_CreateString(context->dbdir);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "dbdir", item);

	item = cJSON_CreateNumber((double)context->list_page_size);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "list_page_size", item);

	item = cJSON_CreateString(context->list_sort_name);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "list_sort_name", item);

	item = cJSON_CreateString(context->list_sort_direction);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "list_sort_direction", item);

	item = cJSON_CreateBool(context->timeout_retry);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "timeout_retry", item);

	item = cJSON_CreateNumber(context->max_thread);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "max_thread", item);

	item = cJSON_CreateNumber(context->max_speed_per_thread);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "max_speed_per_thread", item);

	item = cJSON_CreateString(context->user_agent);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "user_agent", item);
	item = cJSON_CreateNumber(context->syncon);
	if(!item){		
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "syncon", item);

	json = cJSON_Print(root);

	cJSON_Delete(root);
	return json;
}


/*保存上下文*/
static void save_context(webContext *context)
{
	const char *filename;
	char *json;
	FILE *pf;

	json = context2str(context);
	if(!json){
		fprintf(stderr, "context2str error\n");
		return;
	}

	filename = context->contextfile;
	pf = fopen(filename, "wb");
	if (!pf) {
		fprintf(stderr, "Error: Can't open the file: %s\n", filename);
		pcs_free(json);
		return;
	}
	fwrite(json, 1, strlen(json), pf);
	fclose(pf);
	pcs_free(json);
}

/*还原保存的上下文。
成功返回0，失败返回非0值。*/
static int restore_context(webContext *context, const char *filename)
{
	char *filecontent = NULL;
	int filesize = 0;
	cJSON *root, *item;

	if (filename && access(filename, F_OK) == 0) {
		strcpy(context->contextfile, filename);
	}
	filesize = read_file(context->contextfile, &filecontent);
	if (filesize <= 0) {
		fprintf(stderr, "Error: Can't read the context file (%s).\n", context->contextfile);
		if (filecontent) pcs_free(filecontent);
		return -1;
	}
	root = cJSON_Parse(filecontent);
	if (!root) {
		fprintf(stderr, "Error: Broken context file (%s).\n", context->contextfile);
		pcs_free(filecontent);
		return -1;
	}

	item = cJSON_GetObjectItem(root, "cookiefile");
	if (item && item->valuestring && item->valuestring[0]) {
		if (!is_absolute_path(item->valuestring)) {
			printf("warning: Invalid context.cookiefile, the value should be absolute path, use default value: %s.\n", context->cookiefile);
		}
		else {
			if (strlen(context->cookiefile)){
				memset(context->cookiefile, 0, sizeof(context->cookiefile));
			}
			strncpy(context->cookiefile, item->valuestring, sizeof(context->cookiefile)-1);
		}
	}

	item = cJSON_GetObjectItem(root, "captchafile");
	if (item && item->valuestring && item->valuestring[0]) {
		if (!is_absolute_path(item->valuestring)) {
			printf("warning: Invalid context.captchafile, the value should be absolute path, use default value: %s.\n", context->captchafile);
		}
		else {
			if (strlen(context->captchafile)){
				memset(context->captchafile, 0, sizeof(context->captchafile));
			}
			strncpy(context->captchafile, item->valuestring, sizeof(context->captchafile)-1);
		}
	}

	item = cJSON_GetObjectItem(root, "workdir");
	if (item && item->valuestring && item->valuestring[0]) {
		if (item->valuestring[0] != '/') {
			printf("warning: Invalid context.workdir, the value should be absolute path, use default value: %s.\n", context->workdir);
		}
		else {
			if (strlen(context->workdir)){
				memset(context->workdir, 0, sizeof(context->workdir));
			}
			strncpy(context->workdir, item->valuestring, sizeof(context->workdir)-1);
		}
	}
	item = cJSON_GetObjectItem(root, "localdir");
	if (item && item->valuestring && item->valuestring[0]) {
		if (item->valuestring[0] != '/') {
			printf("warning: Invalid context.workdir, the value should be absolute path, use default value: %s.\n", context->localdir);
		}
		else {
			if (strlen(context->localdir)){
				memset(context->localdir, 0, sizeof(context->localdir));
			}
			strncpy(context->localdir, item->valuestring, sizeof(context->localdir)-1);
		}
	}

	item = cJSON_GetObjectItem(root, "list_page_size");
	if (item) {
		if (((int)item->valueint) < 1) {
			printf("warning: Invalid context.list_page_size, the value should be great than 0, use default value: %d.\n", context->list_page_size);
		}
		else {
			context->list_page_size = (int)item->valueint;
		}
	}

	item = cJSON_GetObjectItem(root, "list_sort_name");
	if (item && item->valuestring && item->valuestring[0]) {
		if (strcmp(item->valuestring, "name") && strcmp(item->valuestring, "time") && strcmp(item->valuestring, "size")) {
			printf("warning: Invalid context.list_sort_name, the value should be one of [name|time|size], use default value: %s.\n", context->list_sort_name);
		}
		else {
			if (strlen(context->list_sort_name)){
				memset(context->list_sort_name, 0, sizeof(context->list_sort_name));
			}
			strncpy(context->list_sort_name, item->valuestring, sizeof(context->list_sort_name)-1);
		}
	}

	item = cJSON_GetObjectItem(root, "list_sort_direction");
	if (item && item->valuestring && item->valuestring[0]) {
		if (strcmp(item->valuestring, "asc") && strcmp(item->valuestring, "desc")) {
			printf("warning: Invalid context.list_sort_direction, the value should be one of [asc|desc], use default value: %s.\n", context->list_sort_direction);
		}
		else {
			if (strlen(context->list_sort_direction)){
				memset(context->list_sort_direction, 0, sizeof(context->list_sort_direction));
			}
			strncpy(context->list_sort_direction, item->valuestring, sizeof(context->list_sort_direction)-1);
		}
	}
	
	item = cJSON_GetObjectItem(root, "timeout_retry");
	if (item) {
		context->timeout_retry = (int)item->valueint;
	}

	item = cJSON_GetObjectItem(root, "max_thread");
	if (item) {
		if (((int)item->valueint) < 1) {
			printf("warning: Invalid context.max_thread, the value should be great than 0, use default value: %d.\n", context->max_thread);
		}
		else {
			context->max_thread = (int)item->valueint;
		}
	}

	item = cJSON_GetObjectItem(root, "max_speed_per_thread");
	if (item) {
		if (((int)item->valueint) < 0) {
			printf("warning: Invalid context.max_speed_per_thread, the value should be >= 0, use default value: %d.\n", context->max_speed_per_thread);
		}
		else {
			context->max_speed_per_thread = (int)item->valueint;
		}
	}

	item = cJSON_GetObjectItem(root, "syncon");
	if (item) {
		if (((int)item->valueint) < 0) {
			printf("warning: Invalid context.syncon, the value should be >= 0, use default value: %d.\n", context->syncon);
		}
		else {
			context->syncon = (int)item->valueint;
		}
	}

	item = cJSON_GetObjectItem(root, "user_agent");
	if (item && item->valuestring && item->valuestring[0]) {
		if (strlen(context->user_agent)){
			memset(context->user_agent, 0, sizeof(context->user_agent));
		}
		strncpy(context->user_agent, item->valuestring, sizeof(context->user_agent)-1);
	}

	cJSON_Delete(root);
	pcs_free(filecontent);
	return 0;
}

/*初始化上下文*/
static int init_context(webContext *context)
{
	memset(context, 0, sizeof(webContext));
	contextfile(context->contextfile, sizeof(context->contextfile));
	cookiefile(context->cookiefile, sizeof(context->cookiefile));
	captchafile(context->captchafile, sizeof(context->captchafile));

	strncpy(context->workdir, PCS_REMOTE_DIR, sizeof(context->workdir));
	
	context->list_page_size = PRINT_PAGE_SIZE;
	strncpy(context->list_sort_name, "name", sizeof(context->list_sort_name));
	strncpy(context->list_sort_direction, "asc", sizeof(context->list_sort_direction));

	context->timeout_retry = PCS_HTTP_TRY;
	context->max_thread = PCS_THREAD_NUM;
	context->max_speed_per_thread = 0;
	context->syncon = 1;/*default is sync on*/
	strncpy(context->user_agent, USAGE, sizeof(context->user_agent));
	/*Check User disk*/
	if(pcs_get_localdir(context->localdir) < 0){		
		cloud_set_errno(ESTORAGE, ENOR_DISKFULL);
		return -1;
	}
	return 0;
}

static int init_database(webContext *context)
{
	int ret;

	if(strlen(context->localdir) == 0){
		cloud_set_errno(ESTORAGE, ENOR_DISKFULL);
		return -1;
	}
	if(access(context->localdir, F_OK) ||
		check_partrdonly(context->localdir) == 1){
		/*We Need to Find the base local dir*/		
		struct mntent *mnt;		
		char basepath[4096] ={0};
		FILE *fp = NULL;
		long long freesize = 0, maxsize = 0;	
		struct statfs st;
		
		fp = setmntent("/proc/mounts", "r");
		while ((mnt = getmntent(fp))){
			if (strstr(mnt->mnt_dir, "Volume")){
				/*We need to check partition*/
				memset(basepath, 0, PATH_MAX);
				if(check_dev(mnt->mnt_fsname) == FAILURE){
					continue;
				}
				/*Check partition is write able*/
				if(check_partrdonly(mnt->mnt_dir) == 1){
					DPRINTF("************The partion is: \"%s\" Readonly\n", mnt->mnt_dir);
					continue;
				}
				if(!statfs(mnt->mnt_dir, &st) && (st.f_blocks > 0)){
					freesize = (unsigned long long)st.f_bsize * (unsigned long long)st.f_bavail;
					if(freesize > maxsize){
						strcpy(context->localdir, mnt->mnt_dir);
					}
				}
			}
		}	
		endmntent(fp);
		
		if (0 == maxsize){
			DPRINTF("*************NOT FOUND PARTION***************.\n");
			return -1;
		}else{
			DPRINTF("************The partion is: \"%s\"\n", context->localdir);
		}
	}

	if(context->islogin){		
		int db_vers = -1;
		sqlite3 *db_tmp;

		if(context->db){
			sqlite3_close(context->db);
			context->db = NULL;
		}
		if(strlen(context->dbdir) == 0 || 
			access(context->dbdir, F_OK) != 0){
			snprintf(context->dbdir, sizeof(context->dbdir)-1,
					"%s/%s%s.db", context->localdir, PCS_DBDIR_POSTFIX, pcs_sysUID(context->pcs));
			if(access(context->dbdir, F_OK)){
				char dirpath[PATH_MAX] = {0};
				sprintf(dirpath, "%s/%s", context->localdir, PCS_DBDIR_POSTFIX);
				if(access(dirpath, F_OK)){
					CreateDirectoryRecursive(dirpath);
				}
			}
			DPRINTF("Update DB Dir:%s\n", context->dbdir);	
		}else{
			DPRINTF("Used Old DB Dir:%s\n", context->dbdir);	
		}
		ret = sqlite3_open(context->dbdir, &(db_tmp));
		if(ret){
			DPRINTF("[Auto Repaire] Can't open User DB: %s\n", sqlite3_errmsg(db_tmp));
			sqlite3_close(db_tmp);
			return -1;
		}
		sql_get_int_field(db_tmp, &db_vers, "PRAGMA user_version");
		if(db_vers != DB_VERSION){
			DPRINTF("DATABSE version not match [db_vers = %d, DB_VERSION = %d]\n", 
				db_vers, DB_VERSION);
			/* close db_tmp */
			sqlite3_close(db_tmp);
			remove(context->dbdir);
			ret = sqlite3_open(context->dbdir, &db_tmp);
			if(ret){
				DPRINTF("Can't open database: %s\n", sqlite3_errmsg(db_tmp));
				sqlite3_close(db_tmp);
				return -1;
			}		
		}
		/* create  table, if it not exist */
		ret = sql_exec(db_tmp, create_serverTable_sqlite);	
		if( ret != SUCCESS )
			DPRINTF("Error creating server table!\n");
		ret = sql_exec(db_tmp, create_localTable_sqlite);	
		if( ret != SUCCESS )
			DPRINTF("Error creating local table!\n");
		ret = sql_exec(db_tmp, create_downloadTable_sqlite);	
		if( ret != SUCCESS )
			DPRINTF("Error creating DOWN table!\n");
		ret = sql_exec(db_tmp, create_deleteTable_sqlite);	
		if( ret != SUCCESS )
			DPRINTF("Error creating DELETE table!\n");
		ret = sql_exec(db_tmp, create_doneTable_sqlite);	
		if( ret != SUCCESS )
			DPRINTF("Error creating DONE table!\n");			
		if(db_vers != DB_VERSION){
			/* create index */
			sql_exec(db_tmp, "create INDEX IDX_SERVER_ID ON SERVER(ID);");
			sql_exec(db_tmp, "create INDEX IDX_SERVER_PATH ON SERVER(PATH);");
			sql_exec(db_tmp, "create INDEX IDX_DOWN_ID ON DOWN(ID);");
			sql_exec(db_tmp, "create INDEX IDX_DOWN_PATH ON DOWN(PATH);");
			/* set db_tmp version */
			sql_exec(db_tmp, "PRAGMA user_version = %d", DB_VERSION);
		}

		context->db = db_tmp;			
		DPRINTF("Init Database Successful!\n");

		return 0;
	}

	DPRINTF("No Need To Init Database[No User Login]\n");

	return 0;
}

static void destory_database(webContext *context)
{
	if(context->db){
		sqlite3_close(context->db);
		context->db = NULL;
	}
}
static int login_successful(webContext *context)
{
	int64_t quota, used;
	char *bufdir;
	
	pthread_mutex_lock(&m_mutex);	
	context->islogin = 1;
	init_database(context);
	if(pcs_quota(context->pcs, &quota, &used) == PCS_OK){
		sprintf(context->quota, "%lld", quota);				
		sprintf(context->used, "%lld", used);
	}
	DPRINTF("UserInfo:%s Quota:%s Used:%s-->MakeDir=%s\n", 
			pcs_sysUID(context->pcs), context->quota,context->used, context->workdir);
	bufdir = combin_net_disk_path(context->workdir, NULL);
	if(bufdir){		
		PcsFileInfo *fi = NULL;
		fi = pcs_meta(context->pcs, bufdir);
		if (!fi) {
			pcs_mkdir(context->pcs, bufdir);
			printf("bufdir===================%s\n", bufdir);
		}else{	
			printf("No need to create===================%s\n", bufdir);
			pcs_fileinfo_destroy(fi);
		}		
		pcs_free(bufdir);
	}
	strcpy(context->username, pcs_sysUID(context->pcs));
	save_context(context);
	pthread_cond_broadcast(&m_cond);
	pthread_mutex_unlock(&m_mutex);

	return 0;
}

/***************************************************************************/
/*******************************PUBLIC INTERFACE****************************/
/****************************************************************************/
int pcs_web_api_init(void)
{
	char config[1024] = {0};
	int i;

	hook_cjson();

	contextfile(config, sizeof(config));
	if(access(config, F_OK) ||
		restore_context(&contexWeb, config)!=0){
		remove(config);
		DPRINTF("%s Not Exist or Restore Config Failed\n", config);
		init_context(&contexWeb);
	}else{
		DPRINTF("%s Restore Config Successful\n", config);
	}
	/*Config Init Finish*/
	contexWeb.pcs = create_pcs(&contexWeb);
	if (!contexWeb.pcs) {
		DPRINTF("Can't create pcs context.\n");
		goto init_err;
	}
	/*Init IPC*/
	contexWeb.verify_fd = ipc_pipe_init(PCS_IPC_VERIFY);
	if(contexWeb.verify_fd < 0){
		DPRINTF("Create IPC:%s Failed\n", PCS_IPC_VERIFY);
		goto init_err;
	}
	contexWeb.login_fd = ipc_pipe_init(PCS_IPC_LOGIN);
	if(contexWeb.login_fd < 0){
		DPRINTF("Create IPC:%s Failed\n", PCS_IPC_LOGIN);
		goto init_err;
	}	
	contexWeb.input_fd= ipc_pipe_init(PCS_IPC_INPUT);
	if(contexWeb.input_fd < 0){
		DPRINTF("Create IPC:%s Failed\n", PCS_IPC_INPUT);
		goto init_err;
	}
	if(pthread_create(&contexWeb.tserver, NULL, thread_server, (void *)&contexWeb) != 0){
		DPRINTF("Create Server Thread Failed\n");
		goto init_err;
	}
	pthread_detach(contexWeb.tserver);
	if(pthread_create(&contexWeb.tdown, NULL, thread_down_control, (void *)&contexWeb) != 0){
		DPRINTF("Create Down Thread Failed\n");
		goto init_err;
	}
	pthread_detach(contexWeb.tdown);
	for(i= 0; i< PCS_MULTI_DOWNNUM; i++){
		if(pthread_create(&contexWeb.tdwarr[i], NULL, thread_down_task, (void *)i) != 0){
			DPRINTF("Create Down Thread Failed\n");
			goto init_err;
		}		
		pthread_detach(contexWeb.tdwarr[i]);
	}

	DPRINTF("PCS Web API Init Successful\n");
	
	if(pcs_islogin(contexWeb.pcs) == PCS_LOGIN){
		login_successful(&contexWeb);		
	}
	/*Save Context config*/
	save_context(&contexWeb);

	return 0;
init_err:
	close(contexWeb.verify_fd); 	
	close(contexWeb.input_fd);
	close(contexWeb.login_fd);
	sqlite3_close(contexWeb.db);	
	destroy_pcs(contexWeb.pcs);
	contexWeb.islogin = 0;

	return -1;
}

int pcs_web_api_login(char *username, char *password, char *verifycode)
{
	PcsRes pcsres;
	int state = -1;

	if(!username || !password){
		DPRINTF("Username or Password empty\n");
		return -1;
	}

	if(contexWeb.tlogin){
		DPRINTF("Having Been Login, Please Wait....\n");
		return -3;
	}
	pcsres = pcs_islogin(contexWeb.pcs);
	if(pcsres == PCS_LOGIN){
		DPRINTF("You have been logon. You can logout by 'logout' command and then relogin.\n");
		if(atoi(contexWeb.used) == 0){
			int64_t quota, used;
			if(pcs_quota(contexWeb.pcs, &quota, &used) == PCS_OK){
				sprintf(contexWeb.quota, "%lld", quota);				
				sprintf(contexWeb.used, "%lld", used);
			}
		}
		strcpy(contexWeb.username, pcs_sysUID(contexWeb.pcs));
		contexWeb.islogin = 1;
		return 0;
	}else if(pcsres != PCS_NOT_LOGIN){
		DPRINTF("Something Error, We Not confirm if not login\n");
		return -2;
	}

	if (strlen(username) > 0) {
		pcs_setopt(contexWeb.pcs, PCS_OPTION_USERNAME, username);
	}

	if (strlen(password) > 0) {
		pcs_setopt(contexWeb.pcs, PCS_OPTION_PASSWORD, password);
	}

	if(pthread_create(&contexWeb.tlogin, NULL, thread_login, (void*)&contexWeb)!= 0){
		DPRINTF("Create Login Thread Failed[%s]\n", strerror(errno));
		return -1;
	}

	if(read(contexWeb.login_fd, &state, sizeof(state)) <= 0){
		DPRINTF("Read Login State Failed\n");
		return -1;
	}
	if(state == LOGIN_VERIFY){
		DPRINTF("Need User To INPUT Verify Code, Return Successful\n");
		strcpy(verifycode, contexWeb.captchafile);
		return 1;
	}else{
		DPRINTF("Thread Join login thread\n");
		pthread_join(contexWeb.tlogin, NULL);
		contexWeb.tlogin = 0;
	}
	if(state == LOGIN_OK){
		/*Need to notify other thread to start download*/
		login_successful(&contexWeb);
	}
	
	return state;
}

int pcs_web_api_verifycode(char *verifycode, int codesize)
{
	int state = -1;
	
	if(!verifycode || codesize > 32 ||
			strlen(verifycode) > 32){
		return -1;
	}
	
	if(write(contexWeb.verify_fd, verifycode, codesize) != codesize){
		DPRINTF("Write Verify Code Failed\n");
		return -1;
	}

	if(read(contexWeb.login_fd, &state, sizeof(state)) <= 0){
		DPRINTF("Read Login State Failed\n");
		return -1;
	}
	DPRINTF("Thread Join login thread\n");
	pthread_join(contexWeb.tlogin, NULL);
	contexWeb.tlogin = 0;
	
	if(state == LOGIN_OK){
		/*Need to notify other thread to start download*/
		login_successful(&contexWeb);
	}
	
	return state;
}

int pcs_web_api_logout(void)
{
	
	pthread_mutex_lock(&m_mutex);
	contexWeb.islogin = 0;
	pcs_logout(contexWeb.pcs);
	destory_database(&contexWeb);
	pthread_mutex_unlock(&m_mutex);

	cloud_set_dlprogress_interp(NULL);

	sleep(5);
	return 0;
}

int pcs_web_api_sync(void)
{
	int sync = 0;
	pthread_mutex_lock(&m_mutex);
	sync = contexWeb.syncon;
	pthread_mutex_unlock(&m_mutex);

	return sync;
}

int pcs_web_api_setsync(void)
{
	int sync = 0;
	pthread_mutex_lock(&m_mutex);
	sync = contexWeb.syncon;
	if(sync == 1){
		contexWeb.syncon = 0;		
		cloud_set_dlprogress_interp(NULL);
	}else{
		contexWeb.syncon = 1;
		pthread_cond_broadcast(&m_cond);
	}
	pthread_mutex_unlock(&m_mutex);

	return 0;
}


int pcs_web_api_link(int *login, int *sync)
{
	if(!login || !sync){
		return -1;
	}
	pthread_mutex_lock(&m_mutex);
	*sync = contexWeb.syncon;
	*login = contexWeb.islogin;
	pthread_mutex_unlock(&m_mutex);

	return 0;
}

int pcs_web_api_getinfo(char *username, char *quato, char *used)
{
	if(!username || !quato || !used){
		return -1;
	}

	pthread_mutex_lock(&m_mutex);
	if(contexWeb.islogin == 0){
		pthread_mutex_unlock(&m_mutex);
		return -1;
	}
	strcpy(quato, contexWeb.quota);	
	strcpy(used, contexWeb.used);
	strcpy(username, contexWeb.username);
	pthread_mutex_unlock(&m_mutex);

	return 0;
}

int pcs_web_api_location(char *path)
{
	if(!path){
		return -1;
	}
	pthread_mutex_lock(&m_mutex);
	sprintf(path, "%s/%s(%s)", contexWeb.localdir, PCS_LOCAL_DIR, contexWeb.username);	
	pthread_mutex_unlock(&m_mutex);
	return 0;
}

int pcs_web_api_dlprog(int *dlnum, progress_t *record)
{
	int num = 0, ret;
	if(!dlnum || !record){
		return -1;
	}	
	pthread_mutex_lock(&m_mutex);
	if(contexWeb.islogin == 0){
		pthread_mutex_unlock(&m_mutex);
		return -1;
	}
	pthread_mutex_unlock(&m_mutex);

	/*Get Download information*/
	pthread_mutex_lock(&m_down);
	ret = sql_get_int_field(contexWeb.db, &num, 
			"SELECT COUNT(*) FROM DOWN WHERE OPT ='%d'", CLOUD_CREATE);
	pthread_mutex_unlock(&m_down);
	if (ret == FAILURE){
		DPRINTF("Get Download Num Failed...\n");
		return -1;
	}
	
	memset(record, 0, sizeof(progress_t));
	ret = cloud_get_downprogress(record);
	if(ret == FAILURE){
		return -1;
	}

	return 0;	
}

long long  pcs_web_api_getdownfile_size(char *path, char *totoal)
{
	char lpath[PATH_MAX] = {0}, tmpbuf[PATH_MAX];
	struct stat st;
	int ret, current;
	long long size;
	int fd;
	saveDownloadState dwstate;
	segment *seginfo;
	
	if(!path || !totoal){
		return 0;
	}
	
	ret = pcs_convert_path_to_local(path, lpath);
	if (ret == FAILURE){
		DPRINTF("[GET_DOWN] CONVERT PATH error\n");
		return 0;
	}

	size = atoll(totoal);	
	memset(&st, 0, sizeof(struct stat));
	if(access(lpath, F_OK) == 0){
		lstat(lpath, &st);
		if(st.st_size == size){
			return size;
		}
	}
	sprintf(tmpbuf, "%s%s", lpath, PCS_TMP_FILE);	
	if(access(tmpbuf, F_OK) != 0){
		return 0;
	}

	fd = open(tmpbuf, O_RDONLY);
	if(fd < 0){
		DPRINTF("Open %s Failed...\n", tmpbuf);
		return 0;
	}

	lseek(fd, size, SEEK_SET);
	if(read(fd, &dwstate, sizeof(saveDownloadState)) != sizeof(saveDownloadState) ||
			dwstate.down_magic != THREAD_STATE_MAGIC){
		printf("Read %s Failed or Magic Error...\n", tmpbuf);
		close(fd);
		return 0;
	}
	
	seginfo = calloc(1, sizeof(segment) *dwstate.segnum);
	if(!seginfo){
		close(fd);
		return FAILURE;
	}
	if(read(fd, seginfo, sizeof(segment) *dwstate.segnum) 
			!= sizeof(segment) *dwstate.segnum){
		printf("Read  Segment error:%s..\n", tmpbuf);
		close(fd);
		free(seginfo);
		return 0;
	}
	close(fd);
	
	for(current = 0; current < dwstate.segnum; current++){
		
		size -= (seginfo[current].ep-seginfo[current].sp+1-seginfo[current].ds);
	}
	
	free(seginfo);
	printf("Down Info:[%lld/%sBytes]%s\n", size, totoal, path);

	return size;
}


int pcs_web_api_getlist(const char *sql, char ***pazResult, int *pnRow, int *pnColumn)
{
	int ret;
	
	if(!pazResult || !pnRow || !pnColumn){
		return -1;
	}
	pthread_mutex_lock(&m_mutex);
	if(contexWeb.islogin == 0){
		pthread_mutex_unlock(&m_mutex);
		return -1;
	}

	pthread_mutex_lock(&m_down);
	ret = sql_get_table(contexWeb.db, sql, pazResult, pnRow, pnColumn);
	pthread_mutex_unlock(&m_down);
	pthread_mutex_unlock(&m_mutex);

	return (ret == SUCCESS?0:-1);
}

int pcs_web_api_getdata_record(rec_t *rectp, char *fmt, ...)
{
	int ret;
	
	if(!rectp){
		return -1;
	}
	pthread_mutex_lock(&m_down);
	ret = sql_get_data_record(rectp, contexWeb.db, fmt);
	pthread_mutex_unlock(&m_down);

	return (ret == SUCCESS?0:-1);
}

int pcs_web_api_exesql(char *fmt, ...)
{
	int ret;

	pthread_mutex_lock(&m_down);
	ret = sql_exec(contexWeb.db,fmt);
	pthread_mutex_unlock(&m_down);

	
	return (ret == SUCCESS?0:-1);
}

int pcs_web_api_dlprogress_interp(char *path)
{	
	return cloud_set_dlprogress_interp(path);
}

int pcs_web_api_update_deletedb(char *path)
{
	int ret;
	char lpath[PATH_MAX] = {0}, tmpbuf[PATH_MAX] = {0};
	rec_t qrecord;
	
	if(!path){
		return -1;
	}
	cloud_set_dlprogress_interp(path);
	
	/*Delete Record*/
	ret = pcs_web_api_exesql("DELETE FROM DOWN WHERE DIR=%d AND PATH = '%q' ", 
					0, path);
	if(ret != 0){
		DPRINTF("DELETE DOWN RECORD  [%s] Failed\n", path);
		return -1;
	}	
	/*Remove TMP DOWN File*/
	ret = pcs_convert_path_to_local(path, lpath);
	if (ret == FAILURE){
		DPRINTF("[GET_DOWN] CONVERT PATH error\n");
		return -1;
	}
	sprintf(tmpbuf, "%s%s", lpath, PCS_TMP_FILE);
	if(access(tmpbuf, F_OK) == 0){
		remove(tmpbuf);
	}
	
	pthread_mutex_lock(&m_down);
	memset(&qrecord, 0, sizeof(qrecord));
	ret= sql_get_data_record(&qrecord, contexWeb.db, 
		"SELECT * FROM DETABLE WHERE DIR=%d AND PATH = '%q'", 
			0, path);
	if(ret == FAILURE){
		DPRINTF("QUERY DOWN [%s] error\n", path);
		pthread_mutex_unlock(&m_down);
		return -1;
	}
	if(qrecord.id == 0){
		DPRINTF("INSERT %s to DETABLE Table...\n", path);
		ret = sql_exec(contexWeb.db, "INSERT into DETABLE (PATH, DIR) "
				" values ('%q', '%d')", path, 0);
		if(ret == FAILURE){
			DPRINTF("INSERT DWON [%s] error\n", path);
			pthread_mutex_unlock(&m_down);
			return -1;
		}
	}
	
	pthread_mutex_unlock(&m_down);

	return 0;
}
