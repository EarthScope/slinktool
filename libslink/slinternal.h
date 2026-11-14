/***************************************************************************
 * slinternal.h:
 *
 * Declarations shared between library source files, not part of the
 * public API.  Names avoid the sl_ prefix, which is exported.
 *
 * This file is part of the SeedLink Library.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ***************************************************************************/

#ifndef SLINTERNAL_H
#define SLINTERNAL_H 1

#include <stdio.h>

#include "libslink.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int sli_add_matchedstream (SLCD *slconn, const char *stationid, uint64_t seqnum,
                                  const char *isotime);
extern int sli_linetoolong (FILE *fp, const char *line);

#ifdef __cplusplus
}
#endif

#endif /* SLINTERNAL_H */
