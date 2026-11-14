
#ifndef SLINKINFO_H
#define SLINKINFO_H

#include <stdio.h>

#include <yyjson.h>
#include <ezxml.h>

#ifdef __cplusplus
extern "C"
{
#endif

  extern void print_info_json (FILE *out, const char *json, size_t json_length, int verbose);
  extern void print_info_xml (FILE *out, char *xml, size_t xml_length, int verbose);

#ifdef __cplusplus
}
#endif

#endif /* SLINKINFO_H */
