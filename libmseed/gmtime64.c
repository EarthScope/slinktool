/***************************************************************************
 * The contained ms_gmtime64_r() is a 64-bit version of the standard
 * gmtime_r() and was derived from the y2038 project:
 * https://github.com/evalEmpire/y2038/
 *
 * It has been modified to use closed-form arithmetic (Howard Hinnant's
 * era/day-of-era algorithm) instead of a linear search loop.
 *
 * Original copyright and license are included.
 ***************************************************************************/

/*

Copyright (c) 2007-2010  Michael G Schwern

This software originally derived from Paul Sheer's pivotal_gmtime_r.c.

The MIT License:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

static const short julian_days_by_month[2][12] = {
    {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
    {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335},
};

/* IS_LEAP is used all over the place to index on arrays, so make sure it always returns 0 or 1. */
#define IS_LEAP(n) \
  ((!(((n) + 1900) % 400) || (!(((n) + 1900) % 4) && (((n) + 1900) % 100))) ? 1 : 0)

/* Allegedly, some <termios.h> define a macro called WRAP, so use a longer name like WRAP_TIME64. */
#define WRAP_TIME64(a, b, m) ((a) = ((a) < 0) ? ((b)--, (a) + (m)) : (a))

struct tm *
ms_gmtime64_r (const int64_t *in_time, struct tm *p)
{
  int v_tm_sec, v_tm_min, v_tm_hour, v_tm_mon, v_tm_wday;
  int64_t v_tm_tday;
  int leap;
  int64_t m;
  int64_t time;
  int64_t year;

  if (!in_time || !p)
    return NULL;

  time = *in_time;

  v_tm_sec = (int)(time % 60);
  time /= 60;
  v_tm_min = (int)(time % 60);
  time /= 60;
  v_tm_hour = (int)(time % 24);
  time /= 24;
  v_tm_tday = time;

  WRAP_TIME64 (v_tm_sec, v_tm_min, 60);
  WRAP_TIME64 (v_tm_min, v_tm_hour, 60);
  WRAP_TIME64 (v_tm_hour, v_tm_tday, 24);

  v_tm_wday = (int)((v_tm_tday + 4) % 7);
  if (v_tm_wday < 0)
    v_tm_wday += 7;

  /* Days-to-civil conversion (Howard Hinnant's era/day-of-era algorithm,
   * http://howardhinnant.github.io/date_algorithms.html): shift the epoch
   * to 0000-03-01 so leap days fall at the end of the internal year, then
   * resolve the 400-year Gregorian cycle and its century/4-year/1-year
   * sub-cycles by division, and the month by one more division. Closed
   * form, no loop, no static state. */
  int64_t z = v_tm_tday + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;                                      /* [0, 146096] */
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0, 399] */
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);               /* [0, 365] */
  int64_t mp = (5 * doy + 2) / 153;         /* [0, 11], month from March */
  int64_t mon1 = mp + ((mp < 10) ? 3 : -9); /* [1, 12], month from January */

  m = doy - (153 * mp + 2) / 5; /* [0, 30], day of month, 0-based */
  v_tm_mon = (int)(mon1 - 1);
  year = yoe + era * 400 + ((mon1 <= 2) ? 1 : 0) - 1900;

  leap = IS_LEAP (year);

  p->tm_year = (int)year;

  if (p->tm_year != year)
  {
    return NULL;
  }

  /* At this point m is less than a year so casting to an int is safe */
  p->tm_mday = (int)m + 1;
  p->tm_yday = julian_days_by_month[leap][v_tm_mon] + (int)m;
  p->tm_sec = v_tm_sec;
  p->tm_min = v_tm_min;
  p->tm_hour = v_tm_hour;
  p->tm_mon = v_tm_mon;
  p->tm_wday = v_tm_wday;

  return p;
}
