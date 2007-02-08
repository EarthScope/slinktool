
#ifndef SLINKXML_H
#define SLINKXML_H

#include <ezxml.h>

#ifdef __cplusplus
extern "C"
{
#endif

extern void prtinfo_identification(ezxml_t xmldoc);
extern void prtinfo_stations(ezxml_t xmldoc);
extern void prtinfo_streams(ezxml_t xmldoc);
extern void prtinfo_gaps(ezxml_t xmldoc);
extern void prtinfo_connections(ezxml_t xmldoc);

#ifdef __cplusplus
}
#endif

#endif				/* SLINKXML_H */
