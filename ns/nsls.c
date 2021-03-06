/*	nsls - list name server directory/file entries */
#include <errno.h>
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#if defined(_WIN32)
#include <winsock2.h>
#include "statbits.h"
#else
#include <unistd.h>
#endif
#include "Cgetopt.h"
#include "Cns.h"
#include "Cns_api.h"
#include "serrno.h"
#include "u64subr.h"
#define SIXMONTHS (6*30*24*60*60)
extern	char	*getenv();
#if sgi
extern char *strdup _PROTO((CONST char *));
#endif
int cflag;
int clflag;
int cmflag;
time_t current_time;
int dflag;
int delflag;
int dsflag;
int iflag;
int lflag;
int Rflag;
int Tflag;
int uflag;
int checksumflag;
int tflag;

int listdir(char *dir);
int listdir_t(char *dir);
int listentry(char *path,struct Cns_filestat *statbuf,char *comment);
int listsegs(char *path);
int listtpentry(char *path,int copyno,int fsec,u_signed64 segsize,int compression,char *vid,int side,int fseq,unsigned char blockid[4],char status,char *checksum_name,unsigned long checksum);
int procpath(char *fullpath);

int main(int argc, char **argv)
{
        int c;
        char *dp;
        char *endp;
        int errflg = 0;
        char fullpath[CA_MAXPATHLEN+1];
        int i;
        static struct Coptions longopts[] = {
        {"checksum", NO_ARGUMENT, &checksumflag, 1},
                {"class", NO_ARGUMENT, &clflag, 1},
                {"comment", NO_ARGUMENT, &cmflag, 1},
                {"deleted", NO_ARGUMENT, &delflag, 1},
                {"display_side", NO_ARGUMENT, &dsflag, 1},
                {"ds", NO_ARGUMENT, &dsflag, 1},
                {0, 0, 0, 0}
        };
        char *p;
        char *path;
#if defined(_WIN32)
        WSADATA wsadata;
#endif

        Copterr = 1;
        Coptind = 1;
        while ((c = Cgetopt_long (argc, argv, "tcdilRTu", longopts, NULL)) != EOF) {
                switch (c) {
                case 'c':
                        cflag++;
                        break;
                case 'd':
                        dflag++;
                        break;
                case 'i':
                        iflag++;
                        break;
                case 'l':
                        lflag++;
                        break;
                case 'R':
                        Rflag++;
                        break;
                case 'T':
                        Tflag++;
                        break;
                case 'u':
                        uflag++;
                        break;
                case 't':
                        tflag++;
                        break;
                case '?':
                        errflg++;
                        break;
                default:
                        break;
                }
        }
        (void) time (&current_time);
#if defined(_WIN32)
        if (WSAStartup (MAKEWORD (2, 0), &wsadata)) {
                fprintf (stderr, NS052);
                exit (SYERR);
        }
#endif

        if (Coptind >= argc && ! errflg) {	/* no file specified */
                if ((p = getenv ("CASTOR_HOME")) && strlen (p) <= CA_MAXPATHLEN) {
                        strcpy (fullpath, p);
                        if (procpath (fullpath)) {
                                fprintf (stderr, "%s: %s\n", fullpath, sstrerror(serrno));
#if defined(_WIN32)
                                WSACleanup();
#endif
                                exit (USERR);
                        }
                } else
                        errflg++;
        }
        if (errflg) {
                fprintf (stderr,
                    "usage: %s [-cdilRTut] [--class] [--comment] [--deleted] [--checksum] path...\n",
                        argv[0]);
#if defined(_WIN32)
                WSACleanup();
#endif
                exit (USERR);
        }
        for (i = Coptind; i < argc; i++) {
                path = argv[i];
                if (*path != '/' && strstr (path, ":/") == NULL) {
                        if ((p = getenv ("CASTOR_HOME")) == NULL ||
                            strlen (p) + strlen (path) + 1 > CA_MAXPATHLEN) {
                                fprintf (stderr, "%s: invalid path\n", path);
                                errflg++;
                                continue;
                        } else
                                sprintf (fullpath, "%s/%s", p, path);
                } else {
                        if (strlen (path) > CA_MAXPATHLEN) {
                                fprintf (stderr, "%s: %s\n", path,
                                    sstrerror(SENAMETOOLONG));
                                errflg++;
                                continue;
                        } else
                                strcpy (fullpath, path);
                }
                if (procpath (fullpath)) {
                        fprintf (stderr, "%s: %s\n", path, sstrerror(serrno));
                        errflg++;
                }
        }
#if defined(_WIN32)
        WSACleanup();
#endif
        if (errflg)
                exit (USERR);
        exit (0);
}

int procpath(char *fullpath)
{
        char comment[CA_MAXCOMMENTLEN+1];
        struct Cns_filestat statbuf;
        if (tflag){
            if(Cns_get_Data_daemon(fullpath, &statbuf) < 0){
			 return (-1);
		}
		if(statbuf.filemode& S_IFDIR)
			return(listdir_t(fullpath));
            	return(listentry(fullpath, &statbuf, comment));
        }
        if (Cns_stat (fullpath, &statbuf) < 0)
                return (-1);

        if (statbuf.filemode & S_IFDIR && ! dflag)
                return (listdir (fullpath));
        else if (Tflag)
                return (listsegs (fullpath));
        else {
                if (statbuf.status == 'D' && delflag == 0)
                        return (0);
                if (cmflag)
                         (void) Cns_getcomment (fullpath, comment);
                return (listentry (fullpath, &statbuf, comment));
        }
}


int listdir(char *dir)
{
        char curdir[CA_MAXPATHLEN+1];
        struct Cns_direncomm *dcp;
        struct dirlist {
                char *d_name;
                struct dirlist *next;
        };
        Cns_DIR *dirp;
        struct dirlist *dlc;		/* pointer to current directory in the list */
        struct dirlist *dlf = NULL;	/* pointer to first directory in the list */
        struct dirlist *dll;		/* pointer to last directory in the list */
        struct dirent *dp;
        struct Cns_direnstatc *dxcp;
        struct Cns_direnstat *dxp;
        struct Cns_direntape *dxtp;

        if ((dirp = Cns_opendir (dir)) == NULL)
                return (-1);

        if (Rflag)
                printf ("\n%s:\n", dir);
        if (! clflag && ! iflag && ! lflag && ! Rflag && ! Tflag) {
                if (! cmflag)
                        /* dirent entries contain enough information */
                        while ((dp = Cns_readdir (dirp)) != NULL) {
                                printf ("%s\n", dp->d_name);
                        }
                else
                        while ((dcp = Cns_readdirc (dirp)) != NULL) {
                                printf ("%s %s\n", dcp->d_name, dcp->comment);
                        }
        } else if (Tflag) {
                while ((dxtp = Cns_readdirxt (dirp)) != NULL) {
                        listtpentry (dxtp->d_name, dxtp->copyno, dxtp->fsec,
                            dxtp->segsize, dxtp->compression, dxtp->vid,
                            dxtp->side, dxtp->fseq, dxtp->blockid, dxtp->s_status,
                dxtp->checksum_name, dxtp->checksum);
                }
        } else {
                if (! cmflag)
                        while ((dxp = Cns_readdirx (dirp)) != NULL) {
                                listentry (dxp->d_name, (Cns_filestat*)dxp, NULL);
                                if (Rflag && (dxp->filemode & S_IFDIR)) {
                                        if ((dlc = (struct dirlist *)
                                            malloc (sizeof(struct dirlist))) == NULL ||
                                            (dlc->d_name = strdup (dxp->d_name)) == NULL) {
                                                serrno = errno;
                                                return (-1);
                                        }
                                        dlc->next = 0;
                                        if (dlf == NULL)
                                                dlf = dlc;
                                        else
                                                dll->next = dlc;
                                        dll = dlc;
                                }
                        }
                else
                        while ((dxcp = Cns_readdirxc (dirp)) != NULL) {
                                listentry (dxcp->d_name, (Cns_filestat*)dxcp, dxcp->comment);
                                if (Rflag && (dxcp->filemode & S_IFDIR)) {
                                        if ((dlc = (struct dirlist *)
                                            malloc (sizeof(struct dirlist))) == NULL ||
                                            (dlc->d_name = strdup (dxcp->d_name)) == NULL) {
                                                serrno = errno;
                                                return (-1);
                                        }
                                        dlc->next = 0;
                                        if (dlf == NULL)
                                                dlf = dlc;
                                        else
                                                dll->next = dlc;
                                        dll = dlc;
                                }
                        }
        }
        (void) Cns_closedir (dirp);
        while (dlf) {
                if (strcmp (dir, "/"))
                        sprintf (curdir, "%s/%s", dir, dlf->d_name);
                else
                        sprintf (curdir, "/%s", dlf->d_name);
                if (listdir (curdir) < 0)
                        fprintf (stderr, "%s: %s\n", curdir, sstrerror(serrno));
                free (dlf->d_name);
                dlc = dlf;
                dlf = dlf->next;
                free (dlc);
        }
        return (0);
}


int listdir_t(char *dir)
{
	char curdir[CA_MAXPATHLEN+1];
	struct Cns_direncomm *dcp;
	struct dirlist {
		char *d_name;
		struct dirlist *next;
	};
	Cns_DIR *dirp;
	struct dirlist *dlc;		/* pointer to current directory in the list */
	struct dirlist *dlf = NULL;	/* pointer to first directory in the list */
	struct dirlist *dll;		/* pointer to last directory in the list */
	struct dirent *dp;
	struct Cns_direnstatc *dxcp;
	struct Cns_direnstat *dxp;
	struct Cns_direntape *dxtp;
	if ((dirp = Cns_opendir_t (dir)) == NULL)
		return (-1);
	if (Rflag)
		printf ("\n%s:\n", dir);
	if (! clflag && ! iflag && ! lflag && ! Rflag && ! Tflag) {
		if (! cmflag)
			/* dirent entries contain enough information */
			while ((dp = Cns_readdir_t (dirp)) != NULL) {
				printf ("%s\n", dp->d_name);
			}
		else
			while ((dcp = Cns_readdirc_t (dirp)) != NULL) {
				printf ("%s %s\n", dcp->d_name, dcp->comment);
			}
	} else if (Tflag) {
		while ((dxtp = Cns_readdirxt_t (dirp)) != NULL) {
			listtpentry (dxtp->d_name, dxtp->copyno, dxtp->fsec,
			    dxtp->segsize, dxtp->compression, dxtp->vid,
			    dxtp->side, dxtp->fseq, dxtp->blockid, dxtp->s_status,
                dxtp->checksum_name, dxtp->checksum);
		}
	} else {
		if (! cmflag)
			while ((dxp = Cns_readdirx_t (dirp)) != NULL) {
				listentry (dxp->d_name, (Cns_filestat*)dxp, NULL);
				if (Rflag && (dxp->filemode & S_IFDIR)) {
					if ((dlc = (struct dirlist *)
					    malloc (sizeof(struct dirlist))) == NULL ||
					    (dlc->d_name = strdup (dxp->d_name)) == NULL) {
						serrno = errno;
						return (-1);
					}
					dlc->next = 0;
					if (dlf == NULL)
						dlf = dlc;
					else
						dll->next = dlc;
					dll = dlc;
				}
			}
		else
			while ((dxcp = Cns_readdirxc_t (dirp)) != NULL) {
				listentry (dxcp->d_name, (Cns_filestat*)dxcp, dxcp->comment);
				if (Rflag && (dxcp->filemode & S_IFDIR)) {
					if ((dlc = (struct dirlist *)
					    malloc (sizeof(struct dirlist))) == NULL ||
					    (dlc->d_name = strdup (dxcp->d_name)) == NULL) {
						serrno = errno;
						return (-1);
					}
					dlc->next = 0;
					if (dlf == NULL)
						dlf = dlc;
					else
						dll->next = dlc;
					dll = dlc;
				}
			}
	}
	(void) Cns_closedir_t (dirp);
	while (dlf) {
		if (strcmp (dir, "/"))
			sprintf (curdir, "%s/%s", dir, dlf->d_name);
		else
			sprintf (curdir, "/%s", dlf->d_name);
		if (listdir_t (curdir) < 0)
			fprintf (stderr, "%s: %s\n", curdir, sstrerror(serrno));
		free (dlf->d_name);
		dlc = dlf;
		dlf = dlf->next;
		free (dlc);
	}
	return (0);
}


int listentry(char *path, struct Cns_filestat *statbuf, char *comment)
{
        struct passwd *pw;
        struct group *gr;
        char modestr[11];
        static gid_t sav_gid = -1;
        static char sav_gidstr[7];
        time_t ltime;
        static uid_t sav_uid = -1;
        static char sav_uidstr[CA_MAXUSRNAMELEN+1];
        char timestr[13];
        struct tm *tm;
        char tmpbuf[21];

        if (statbuf->status == 'D' && delflag == 0)
                return (0);
        if (iflag)
                printf ("%s ", u64tostr (statbuf->fileid, tmpbuf, 20));
        if (clflag)
                printf ("%d ", statbuf->fileclass);
        if (lflag) {
                if (statbuf->filemode & S_IFDIR)
                        modestr[0] = 'd';
                else
                        modestr[0] = statbuf->status;
                modestr[1] = (statbuf->filemode & S_IRUSR) ? 'r' : '-';
                modestr[2] = (statbuf->filemode & S_IWUSR) ? 'w' : '-';
                if (statbuf->filemode & S_IXUSR)
                        if (statbuf->filemode & S_ISUID)
                                modestr[3] = 's';
                        else
                                modestr[3] = 'x';
                else
                        modestr[3] = '-';
                modestr[4] = (statbuf->filemode & S_IRGRP) ? 'r' : '-';
                modestr[5] = (statbuf->filemode & S_IWGRP) ? 'w' : '-';
                if (statbuf->filemode & S_IXGRP)
                        if (statbuf->filemode & S_ISGID)
                                modestr[6] = 's';
                        else
                                modestr[6] = 'x';
                else
                        modestr[6] = '-';
                modestr[7] = (statbuf->filemode & S_IROTH) ? 'r' : '-';
                modestr[8] = (statbuf->filemode & S_IWOTH) ? 'w' : '-';
                if (statbuf->filemode & S_IXOTH)
                        if (statbuf->filemode & S_ISVTX)
                                modestr[9] = 't';
                        else
                                modestr[9] = 'x';
                else
                        modestr[9] = '-';
                modestr[10] = '\0';
                if (statbuf->uid != sav_uid) {
                        sav_uid = statbuf->uid;
                        if (pw = getpwuid (sav_uid))
                                strcpy (sav_uidstr, pw->pw_name);
                        else
                                sprintf (sav_uidstr, "%-8u", sav_uid);
                }
                if (statbuf->gid != sav_gid) {
                        sav_gid = statbuf->gid;
                        if (gr = getgrgid (sav_gid)) {
                                strncpy (sav_gidstr, gr->gr_name, sizeof(sav_gidstr) - 1);
                                sav_gidstr[sizeof(sav_gidstr) - 1] = '\0';
                        } else
                                sprintf (sav_gidstr, "%-6u", sav_gid);
                }
                if (cflag)
                        ltime = statbuf->ctime;
                else if (uflag)
                        ltime = statbuf->atime;
                else
                        ltime = statbuf->mtime;
                tm = localtime (&ltime);
                if (ltime < current_time - SIXMONTHS ||
                    ltime > current_time + 60)
                        strftime (timestr, 13, "%b %d  %Y", tm);
                else
                        strftime (timestr, 13, "%b %d %H:%M", tm);
                printf ("%s %3d %-8.8s %-6.6s %s %s ",
                    modestr, statbuf->nlink, sav_uidstr, sav_gidstr,
                    u64tostr (statbuf->filesize, tmpbuf, 20), timestr);
        }

	char basename[214];
       	char *p;	
	p = path + strlen (path) - 1;
	while (*p == '/' && p != path)
		*p = '\0';
	if ((p = strrchr (path, '/')) == NULL)
		p = path - 1;
	strcpy (basename, (*(p + 1)) ? p + 1 : "/");
	if (p <= path)	
		p++;
	*p = '\0';

	printf ("%s", basename);
        if (cmflag)
                printf (" %s", comment);
        printf ("\n");
        return (0);
}

int listsegs(char *path)
{
        int c;
        struct Cns_fileid file_uniqueid;
        int i;
        int nbseg;
        struct Cns_segattrs *segattrs;

        memset (&file_uniqueid, 0, sizeof(struct Cns_fileid));
        if ((c = Cns_getsegattrs (path, &file_uniqueid, &nbseg , &segattrs)))
                return (c);
        for (i = 0; i < nbseg; i++) {
                listtpentry (path, (segattrs+i)->copyno, (segattrs+i)->fsec,
                    (segattrs+i)->segsize, (segattrs+i)->compression,
                    (segattrs+i)->vid, (segattrs+i)->side, (segattrs+i)->fseq,
                    (segattrs+i)->blockid, (segattrs+i)->s_status,
            (segattrs+i)->checksum_name, (segattrs+i)->checksum);
        }
        if (segattrs)
                free (segattrs);
        return (0);
}

int listtpentry(char *path,int copyno,int fsec,u_signed64 segsize,int compression,char *vid,int side,int fseq,unsigned char blockid[4],char status,char *checksum_name,unsigned long checksum)
{
        char tmpbuf[21];

        if (status == 'D' && delflag == 0)
                return (0);
        if (dsflag || side > 0)
                printf ("%c %d %3d %-6.6s/%d %5d %02x%02x%02x%02x %s %3d ",
                    status, copyno, fsec, vid, side, fseq,
                    blockid[0], blockid[1], blockid[2], blockid[3],
                    u64tostr (segsize, tmpbuf, 20), compression);
        else
                printf ("%c %d %3d %-6.6s   %5d %02x%02x%02x%02x %s %3d ",
                    status, copyno, fsec, vid, fseq,
                    blockid[0], blockid[1], blockid[2], blockid[3],
                    u64tostr (segsize, tmpbuf, 20), compression);

    if (checksumflag) {
        if (checksum_name != NULL && checksum_name[0] != '\0') {
            printf ("%*s %08lx ", CA_MAXCKSUMNAMELEN, checksum_name, checksum);
        } else {
            printf ("%*s %08lx ", CA_MAXCKSUMNAMELEN, "-", 0);
        }
        }
        printf ("%s\n", path);
        return (0);
}
