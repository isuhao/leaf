#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dlfcn.h>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#include <netinet/in.h>
#endif

#include "marshall.h"
#include "Cns_api.h"
#include "Cns.h"
#include "serrno.h"


int Cns_download_seg(const char *path, off_t offset, size_t size, char *location, int filesize)
{
	char *actual_path;
	int msglen;
	char *sbp;
	char *rbp;
        char repbuf[REQBUFSZ];
	char *q;
	char func[16];
	char sendbuf[REQBUFSZ];
	int c;	
	int res;
	char server[CA_MAXHOSTNAMELEN+1];
	struct Cns_api_thread_info *thip;
	strcpy(func, "Cns_download_seg");
	if(Cns_apiinit(&thip))
		return -1;
	uid_t uid=getuid();
	gid_t gid=getgid();
#if defined(_WIN32)
	if(uid<0||gid<0){
		Cns_errmsg(func, NS053);
		serrno=SENOMAPFND;
		return -1;
	}
#endif
	if(!path||!location||offset<0||size<=0||filesize<0){
		serrno=EFAULT;
		return -1;
	}
	if(strlen(path)>CA_MAXPATHLEN){
		serrno=ENAMETOOLONG;
		return -1;
	}
	
	if (Cns_selectsrvr (path, thip->server, server, &actual_path))
       		return (-1);

        /* Build request header */

        sbp = sendbuf;
        marshall_LONG (sbp, CNS_MAGIC);
        marshall_LONG (sbp, CNS_DOWNLOAD_SEG);
        q = sbp;        /* save pointer. The next field will be updated */
        msglen = 3 * LONGSIZE;
        marshall_LONG (sbp, msglen);
	
        /* Build request body */

        marshall_LONG (sbp, uid);
        marshall_LONG (sbp, gid);
        marshall_HYPER (sbp, thip->cwd);
      	
        marshall_STRING (sbp, actual_path);
	 marshall_LONG (sbp, offset);
	 marshall_LONG (sbp, size);
	marshall_STRING (sbp, location);
	marshall_LONG(sbp, filesize);
        msglen = sbp - sendbuf;
        marshall_LONG (q, msglen);      /* update length field */

        while ((c = send2nsd (NULL, server, sendbuf, msglen, NULL, 0)) &&
            serrno == ENSNACT)
                sleep (RETRYI);
        if (c && serrno == SENAMETOOLONG) serrno = ENAMETOOLONG;
/*
       while ((c = send2nsd (NULL, server, sendbuf, msglen, repbuf, sizeof(repbuf))) &&
            serrno == ENSNACT)
                sleep (RETRYI);
        if (c == 0) {
                rbp = repbuf;
                unmarshall_LONG(rbp, res);
        }
        if (c && serrno == SENAMETOOLONG) serrno = ENAMETOOLONG;
*/  
      return res;	
}
