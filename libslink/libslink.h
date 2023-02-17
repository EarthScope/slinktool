/***************************************************************************
 * libslink.h:
 *
 * Interface declarations for the SeedLink library (libslink).
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

#ifndef LIBSLINK_H
#define LIBSLINK_H 1

#ifdef __cplusplus
extern "C" {
#endif

#define LIBSLINK_VERSION "2.7.1"       /**< libslink version */
#define LIBSLINK_RELEASE "2023.007"    /**< libslink release date */

/** @defgroup seedlink-connection SeedLink Connection */
/** @defgroup connection-state Connection State */
/** @defgroup logging Central Logging */
/** @defgroup miniseed-record miniSEED Records */
/** @defgroup utility-functions General Utility Functions */


/* Portability to the XScale (ARM) architecture requires a packed
 * attribute in certain places but this only works with GCC for now. */
#if defined (__GNUC__)
  #define SLP_PACKED __attribute__ ((packed))
#else
  #define SLP_PACKED
#endif

/* C99 standard headers */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <ctype.h>

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
  #define SLP_WIN 1
#endif

/* Set platform specific features, Windows, Solaris, then everything else */
#if defined(SLP_WIN)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <process.h>
  #include <io.h>

  /* For MSVC 2012 and earlier define standard int types, otherwise use inttypes.h */
  #if defined(_MSC_VER) && _MSC_VER <= 1700
    typedef signed char int8_t;
    typedef unsigned char uint8_t;
    typedef signed short int int16_t;
    typedef unsigned short int uint16_t;
    typedef signed int int32_t;
    typedef unsigned int uint32_t;
    typedef signed __int64 int64_t;
    typedef unsigned __int64 uint64_t;
  #else
    #include <inttypes.h>
  #endif

  #if defined(_MSC_VER)
    #if !defined(PRId64)
      #define PRId64 "I64d"
    #endif
    #if !defined(SCNd64)
      #define SCNd64 "I64d"
    #endif

    #define strdup _strdup
    #define read _read
    #define write _write
    #define open _open
    #define close _close
    #define snprintf _snprintf
    #define vsnprintf _vsnprintf
    #define strncasecmp _strnicmp
  #endif

#elif defined(__sun__) || defined(__sun)
  #include <unistd.h>
  #include <inttypes.h>
  #include <errno.h>
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <sys/time.h>
  #include <sys/utsname.h>
  #include <pwd.h>

#else
  #include <unistd.h>
  #include <inttypes.h>
  #include <errno.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <sys/time.h>
  #include <sys/utsname.h>
  #include <pwd.h>

#endif

/* Use int for SOCKET if not already defined */
#ifndef SOCKET
/** @def SOCKET
    @brief A portable type for a socket descriptor */
  #define SOCKET int
#endif

/** @addtogroup logging
    @brief Central logging functions for the library and calling programs

    This central logging facility is used for all logging performed by
    the library.

    The logging can be configured to send messages to arbitrary
    functions, referred to as \c log_print() and \c diag_print().
    This allows output to be re-directed to other logging systems if
    needed.

    It is also possible to assign prefixes to log messages for
    identification, referred to as \c logprefix and \c errprefix.

    @anchor logging-levels
    Three message levels are recognized:
    - 0 : Normal log messages, printed using \c log_print() with \c logprefix
    - 1  : Diagnostic messages, printed using \c diag_print() with \c logprefix
    - 2+ : Error messages, printed using \c diag_print() with \c errprefix

    It is the task of the \c sl_log(), \c sl_ms_log_l(), and
    \c sl_ms_log_rl() functions to format a message using printf
    conventions and pass the formatted string to the appropriate
    printing function.

    @{ */

/** @brief Logging parameters */
typedef struct SLlog_s
{
  void (*log_print) ();   /**< Log message printing function */
  const char *logprefix;  /**< Log message message prefix */
  void (*diag_print) ();  /**< Warning & error message printing function */
  const char *errprefix;  /**< Warning & error message prefix */
  int verbosity;          /**< Logging verbosity */
} SLlog;
/** @} */

/** @addtogroup seedlink-connection
    @brief Definitions and functions related to SeedLink connections
    @{ */

#define SL_DEFAULT_HOST "localhost"  /**< Default host for libslink */
#define SL_DEFAULT_PORT "18000"      /**< Default port for libslink */

#define SLRECSIZEMIN        48       /**< Min supported miniSEED record size */
#define SLRECSIZEMAX        4096     /**< Max supported miniSEED record size */
#define SLRECSIZE           512      /**< Default miniSEED record size */
#define MAX_HEADER_SIZE     128      /**< Max record header size */
#define SLHEADSIZE          8        /**< SeedLink header size */
#define SELSIZE             8        /**< Maximum selector size */
#define BUFSIZE             8192     /**< Size of receiving buffer */
#define SIGNATURE           "SL"     /**< SeedLink header signature */
#define INFOSIGNATURE       "SLINFO" /**< SeedLink INFO packet signature */
#define MAX_LOG_MSG_LENGTH  200      /**< Maximum length of log messages */

/* Return values for sl_collect() and sl_collect_nb() */
#define SLPACKET    1  /**< sl_collect()/sl_collect_nb() return value when SLpacket is returned */
#define SLTERMINATE 0  /**< sl_collect()/sl_collect_nb() return value on connection termination or error */
#define SLNOPACKET -1  /**< sl_collect_nb() return value when no packet is available */

/* SeedLink packet types */
#define SLDATA 0     /**< SeedLink packet: waveform data record */
#define SLDET  1     /**< SeedLink packet: detection record */
#define SLCAL  2     /**< SeedLink packet: calibration record */
#define SLTIM  3     /**< SeedLink packet: timing record */
#define SLMSG  4     /**< SeedLink packet: message record */
#define SLBLK  5     /**< SeedLink packet: general record */
#define SLNUM  6     /**< SeedLink packet: used as the error indicator (same as SLCHA) */
#define SLCHA  6     /**< SeedLink packet: for requesting channel info or detectors */
#define SLINF  7     /**< SeedLink packet: a non-terminating XML formatted message in a miniSEED
			  log record, used for INFO responses */
#define SLINFT 8     /**< SeedLink packet: a terminating XML formatted message in a miniSEED log
			  record, used for INFO responses */
#define SLKEEP 9     /**< SeedLink packet: an XML formatted message in a miniSEED log
			  record, used for keepalive/heartbeat responses */

/* The station and network code used for uni-station mode */
#define UNISTATION  "UNI"  /**< Station code for uni-station mode */
#define UNINETWORK  "XX"   /**< Network code for uni-station mode */

/** @brief SeedLink packet */
typedef struct slpacket_s
{
  char    *slhead;              /**< SeedLink header */
  char    *msrecord;            /**< miniSEED record */
  int      reclen;              /**< miniSEED record length */
} SLpacket;

/** @brief Stream information */
typedef struct slstream_s
{
  char   *net;      	        /**< The network code */
  char   *sta;      	        /**< The station code */
  char   *selectors;	        /**< SeedLink style selectors for this station */
  int     seqnum;	        /**< SeedLink sequence number for this station */
  char    timestamp[30];        /**< Time stamp of last packet received */
  struct  slstream_s *next;     /**< The next station in the chain */
} SLstream;

/** @brief Persistent connection state information */
typedef struct stat_s
{
  char    databuf[BUFSIZE];     /**< Data buffer for received packets */
  int     recptr;               /**< Receive pointer for databuf */
  int     sendptr;              /**< Send pointer for databuf */
  SLpacket slpack;              /**< Transient, client-specific SLPacket pointers */
  int8_t  expect_info;          /**< Do we expect an INFO response? */

  int8_t  netto_trig;           /**< Network timeout trigger */
  int8_t  netdly_trig;          /**< Network re-connect delay trigger */
  int8_t  keepalive_trig;       /**< Send keepalive trigger */

  double  netto_time;           /**< Network timeout time stamp */
  double  netdly_time;          /**< Network re-connect delay time stamp */
  double  keepalive_time;       /**< Keepalive time stamp */

  enum                          /**< Connection state */
    {
      SL_DOWN, SL_UP, SL_DATA
    }
  sl_state;

  enum                          /**< INFO query state */
    {
      NoQuery, InfoQuery, KeepAliveQuery
    }
  query_mode;

} SLstat;

/** @brief SeedLink connection description */
typedef struct slcd_s
{
  SLstream   *streams;		/**< Pointer to stream chain (a linked list of structs) */
  char       *sladdr;           /**< The host:port of SeedLink server */
  char       *begin_time;	/**< Beginning of time window */
  char       *end_time;		/**< End of time window */

  int8_t      resume;           /**< Boolean flag to control resuming with seq. numbers */
  int8_t      multistation;     /**< Boolean flag to indicate multistation mode */
  int8_t      dialup;           /**< Boolean flag to indicate dial-up mode */
  int8_t      batchmode;        /**< Batch mode (1 - requested, 2 - activated) */
  int8_t      lastpkttime;      /**< Boolean flag to control last packet time usage */
  int8_t      terminate;        /**< Boolean flag to control connection termination */

  int         keepalive;        /**< Interval to send keepalive/heartbeat (secs) */
  int         iotimeout;        /**< Timeout for network I/O operations (seconds) */
  int         netto;            /**< Network timeout (secs) */
  int         netdly;           /**< Network reconnect delay (secs) */

  float       protocol_ver;     /**< Version of the SeedLink protocol in use */
  const char *info;             /**< INFO level to request */
  SOCKET      link;		/**< The network socket descriptor */
  SLstat     *stat;             /**< Persistent state information */
  SLlog      *log;              /**< Logging parameters */
} SLCD;

extern int sl_collect (SLCD *slconn, SLpacket **slpack);
extern int sl_collect_nb (SLCD *slconn, SLpacket **slpack);
extern int sl_collect_nb_size (SLCD *slconn, SLpacket **slpack, int maxrecsize);
extern SLCD *sl_newslcd (void);
extern void sl_freeslcd (SLCD *slconn);
extern int sl_addstream (SLCD *slconn, const char *net, const char *sta,
                         const char *selectors, int seqnum,
                         const char *timestamp);
extern int sl_setuniparams (SLCD *slconn, const char *selectors,
                            int seqnum, const char *timestamp);
extern int sl_request_info (SLCD *slconn, const char *infostr);
extern int sl_sequence (const SLpacket *);
extern int sl_packettype (const SLpacket *);
extern void sl_terminate (SLCD *slconn);

extern int sl_read_streamlist (SLCD *slconn, const char *streamfile,
                               const char *defselect);
extern int sl_parse_streamlist (SLCD *slconn, const char *streamlist,
                                const char *defselect);

extern int sl_configlink (SLCD *slconn);
extern int sl_send_info (SLCD *slconn, const char *info_level,
                         int verbose);
extern SOCKET sl_connect (SLCD *slconn, int sayhello);
extern int sl_disconnect (SLCD *slconn);
extern int sl_ping (SLCD *slconn, char *serverid, char *site);
extern int sl_senddata (SLCD *slconn, void *buffer, size_t buflen,
                        const char *ident, void *resp, int resplen);
extern int sl_recvdata (SLCD *slconn, void *buffer, size_t maxbytes,
                        const char *ident);
extern int sl_recvresp (SLCD *slconn, void *buffer, size_t maxbytes,
                        const char *command, const char *ident);
/** @} */

/** @addtogroup logging
    @{ */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((__format__ (__printf__, 3, 4)))
#endif
extern int sl_log (int level, int verb, const char *format, ...);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((__format__ (__printf__, 4, 5)))
#endif
extern int sl_log_r (const SLCD * slconn, int level, int verb, const char *format, ...);
#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((__format__ (__printf__, 4, 5)))
#endif

extern int sl_log_rl (SLlog *log, int level, int verb, const char *format, ...);
extern void sl_loginit (int verbosity,
                        void (*log_print) (const char *), const char *logprefix,
                        void (*diag_print) (const char *), const char *errprefix);
extern void sl_loginit_r (SLCD *slconn, int verbosity,
                          void (*log_print) (const char *), const char *logprefix,
                          void (*diag_print) (const char *), const char *errprefix);
extern SLlog *sl_loginit_rl (SLlog *log, int verbosity,
                             void (*log_print) (const char *), const char *logprefix,
                             void (*diag_print) (const char *), const char *errprefix);
/** @} */

/** @addtogroup connection-state
    @brief Basic functionality for saving and recovering connections

    @{ */
extern int sl_recoverstate (SLCD *slconn, const char *statefile);
extern int sl_savestate (SLCD *slconn, const char *statefile);
/** @} */

/** @addtogroup miniseed-record
    @brief Basic functionality for handling mini SEED records

    @{ */

/** @brief Generic struct for header of miniSEED 2 blockettes */
struct sl_blkt_head_s
{
  uint16_t  blkt_type;
  uint16_t  next_blkt;
} SLP_PACKED;

/** @brief SEED binary time (10 bytes) */
struct sl_btime_s
{
  uint16_t  year;
  uint16_t  day;
  uint8_t   hour;
  uint8_t   min;
  uint8_t   sec;
  uint8_t   unused;
  uint16_t  fract;
} SLP_PACKED;

/** @brief 100 Blockette (12 bytes) */
struct sl_blkt_100_s
{
  uint16_t  blkt_type;
  uint16_t  next_blkt;
  float     sample_rate;
  int8_t    flags;
  uint8_t   reserved[3];
} SLP_PACKED;

/** @brief 1000 Blockette (8 bytes) */
struct sl_blkt_1000_s
{
  uint16_t  blkt_type;
  uint16_t  next_blkt;
  uint8_t   encoding;
  uint8_t   word_swap;
  uint8_t   rec_len;
  uint8_t   reserved;
} SLP_PACKED;

/** @brief 1001 Blockette (8 bytes) */
struct sl_blkt_1001_s
{
  uint16_t  blkt_type;
  uint16_t  next_blkt;
  int8_t    timing_qual;
  int8_t    usec;
  uint8_t   reserved;
  int8_t    frame_cnt;
} SLP_PACKED;

/** @brief Fixed section of data header for miniSEED 2 (48 bytes) */
struct sl_fsdh_s
{
  char        sequence_number[6];
  char        dhq_indicator;
  char        reserved;
  char        station[5];
  char        location[2];
  char        channel[3];
  char        network[2];
  struct sl_btime_s start_time;
  uint16_t    num_samples;
  int16_t     samprate_fact;
  int16_t     samprate_mult;
  uint8_t     act_flags;
  uint8_t     io_flags;
  uint8_t     dq_flags;
  uint8_t     num_blockettes;
  int32_t     time_correct;
  uint16_t    begin_data;
  uint16_t    begin_blockette;
} SLP_PACKED;

/* Unpacking/decompression error flag values */
#define MSD_NOERROR          0        /**< No errors */
#define MSD_UNKNOWNFORMAT   -1        /**< Unknown data format */
#define MSD_SAMPMISMATCH    -2        /**< Num. samples in header is not the number unpacked */
#define MSD_BADSAMPCOUNT    -4        /**< Sample count is bad, negative? */
#define MSD_STBADLASTMATCH  -5        /**< Steim, last sample does not match */
#define MSD_STBADCOMPFLAG   -6        /**< Steim, invalid compression flag(s) */

typedef struct SLMSrecord_s {
  const char            *msrecord;    /**< Pointer to original record */
  struct sl_fsdh_s       fsdh;        /**< Fixed Section of Data Header */
  struct sl_blkt_100_s  *Blkt100;     /**< Blockette 100, if present */
  struct sl_blkt_1000_s *Blkt1000;    /**< Blockette 1000, if present */
  struct sl_blkt_1001_s *Blkt1001;    /**< Blockette 1001, if present */
  int32_t               *datasamples; /**< Unpacked 32-bit data samples */
  int32_t                numsamples;  /**< Number of unpacked samples */
  int8_t                 unpackerr;   /**< Unpacking/decompression error flag */
} SLMSrecord;

extern SLMSrecord *sl_msr_new (void);
extern void sl_msr_free (SLMSrecord **msr);
extern SLMSrecord *sl_msr_parse (SLlog *log, const char *msrecord, SLMSrecord **msr,
                                 int8_t blktflag, int8_t unpackflag);
extern SLMSrecord *sl_msr_parse_size (SLlog *log, const char *msrecord, SLMSrecord **msr,
                                      int8_t blktflag, int8_t unpackflag, int slrecsize);
extern int sl_msr_print (SLlog *log, SLMSrecord *msr, int details);
extern char *sl_msr_srcname (SLMSrecord *msr, char *srcname, int8_t quality);
extern int sl_msr_dsamprate (SLMSrecord *msr, double *samprate);
extern double sl_msr_dnomsamprate (SLMSrecord *msr);
extern double sl_msr_depochstime (SLMSrecord *msr);
/** @} */

/** @addtogroup utility-functions
    @brief General utilities

    @{ */

extern double sl_dtime (void);
extern int sl_doy2md (int year, int jday, int *month, int *mday);
extern int sl_checkversion (const SLCD *slconn, float version);
extern int sl_checkslcd (const SLCD *slconn);
extern int sl_readline (int fd, char *buffer, int buflen);

/*@ @brief For a linked list of strings, as filled by strparse() */
typedef struct SLstrlist_s {
  char               *element;
  struct SLstrlist_s *next;
} SLstrlist;

extern int sl_strparse (const char *string, const char *delim, SLstrlist **list);
extern int sl_strncpclean (char *dest, const char *source, int length);

/* Generic byte swapping routines */
extern void sl_gswap2 (void *data2);
extern void sl_gswap3 (void *data3);
extern void sl_gswap4 (void *data4);
extern void sl_gswap8 (void *data8);

/* Generic byte swapping routines for memory aligned quantities */
extern void sl_gswap2a (void *data2);
extern void sl_gswap4a (void *data4);
extern void sl_gswap8a (void *data8);

/* Byte swap macro for the BTime struct */
#define SL_SWAPBTIME(x) \
  sl_gswap2 (x.year);   \
  sl_gswap2 (x.day);    \
  sl_gswap2 (x.fract);
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LIBSLINK_H */
