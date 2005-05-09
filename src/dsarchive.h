
#ifndef DSARCHIVE_H
#define DSARCHIVE_H

#include <stdio.h>
#include <time.h>
#include <libslink.h>

/* For the data stream chains */
typedef struct DataStream_s
{
  char   *defkey;
  FILE   *filep;
  time_t  modtime;
  struct DataStream_s *next;
}
DataStream;


extern int ds_streamproc (DataStream **streamroot, char *pathformat,
			  const MSrecord *msr, int reclen, int type,
			  int idletimeout);

#endif

