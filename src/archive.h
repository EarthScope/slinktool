
#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <libslink.h>

extern int  arch_streamproc (const char *archformat, const MSrecord *msr,
			     int reclen, int type, int idletimeout);
extern int  sds_streamproc (const char *sdsdir, const MSrecord *msr,
			    int reclen, int type, int idletimeout);
extern int  bud_streamproc (const char *buddir, const MSrecord *msr,
			    int reclen, int idletimeout);
extern int  dlog_streamproc (const char *sdsdir, const MSrecord *msr,
			     int reclen, int type, int idletimeout);
#endif
