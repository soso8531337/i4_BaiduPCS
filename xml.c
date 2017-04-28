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

#include "common.h"
#include "xml.h"


/*
 * elem: label or element name
 * val: the label value
 * buf: the buffer object
 */
int 
xml_add_elem(int flag, const char *elem, const char *val, char *buf)
{
	if (!elem ||  !buf)
	{
		DPRINTF("elem or var or buf is null\n");
		return FAILURE;
	}
	if (flag == XML_ELEM_START) {
		sprintf(&buf[strlen(buf)], "<%s>", elem);
	} else if (flag == XML_ELEM_END) {
		sprintf(&buf[strlen(buf)], "</%s>", elem);
	} else if (flag == XML_LABEL) {
		if(val == NULL){
			sprintf(&buf[strlen(buf)], "<%s></%s>", elem, elem);			
		}else{
			sprintf(&buf[strlen(buf)], "<%s>%s</%s>", elem, val, elem);
		}
	}
	return SUCCESS;
}

/* Purpose  : Add XML header
 * In       : flag:
 * buf: the buffer 
 * Return   : SUCCESS: 1
 *            FAILURE: 0
 */
int 
xml_add_header(char *buf)
{
	if (!buf)
	{
		DPRINTF("buf is null\n");
		return FAILURE;
	}
	sprintf(&buf[strlen(buf)], "%s", "<?xml version=\"1.0\" ?>");
	sprintf(&buf[strlen(buf)], "%s", "<root>");
	return SUCCESS;
}

/* Purpose  : Add XML end
 * In       : flag:
 * buf: the buffer 
 * Return   : SUCCESS: 1
 *            FAILURE: 0
 */ 
int 
xml_add_end(char *buf)
{
	if (!buf)
	{
		DPRINTF("buf is null\n");
		return FAILURE;
	}
	sprintf(&buf[strlen(buf)], "%s", "</root>");
	return SUCCESS;
}
