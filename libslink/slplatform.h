/***************************************************************************
 * slplatform.h:
 *
 * Platform specific headers.  This file provides a basic level of platform
 * portability.
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
 *
 * Copyright (C) 2022:
 * @author Chad Trabant, EarthScope Data Services
 ***************************************************************************/

#ifndef SLPLATFORM_H
#define SLPLATFORM_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "libslink.h"

extern int slp_sockstartup (void);
extern int slp_sockconnect (SOCKET sock, struct sockaddr * inetaddr, int addrlen);
extern int slp_sockclose (SOCKET sock);
extern int slp_socknoblock (SOCKET sock);
extern int slp_noblockcheck (void);
extern int slp_setsocktimeo (SOCKET socket, int timeout);
extern int slp_openfile (const char *filename, char perm);
extern const char *slp_strerror(void);
extern double slp_dtime(void);
extern void slp_usleep(unsigned long int useconds);

#ifdef __cplusplus
}
#endif

#endif /* SLPLATFORM_H */
