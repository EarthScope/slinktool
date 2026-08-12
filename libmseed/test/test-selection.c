#include <libmseed.h>
#include <tau/tau.h>

TEST (selection, match)
{
  MS3Selections *selections = NULL;
  const MS3Selections *match = NULL;
  const MS3SelectTime *timematch = NULL;
  nstime_t starttime;
  nstime_t endtime;
  int rv;

  starttime = ms_timestr2nstime ("2010-02-27T06:50:00.069539Z");
  endtime = ms_timestr2nstime ("2010-02-27T07:55:51.069539Z");

  /* Add selections */
  rv = ms3_addselect (&selections, "FDSN:XX_*", NSTUNSET, NSTUNSET, 0);
  REQUIRE (rv == 0, "ms3_addselect() did not return expected 0");

  rv = ms3_addselect_comp (&selections, "YY", "STA1", "", "B_H_Z", NSTUNSET, NSTUNSET, 0);
  REQUIRE (rv == 0, "ms3_addselect_comp() did not return expected 0");

  rv = ms3_addselect_comp (&selections, "YY", "STA1", "", "LHZ", starttime, endtime, 2);
  REQUIRE (rv == 0, "ms3_addselect_comp() did not return expected 0");

  rv = ms3_addselect (&selections, "FDSN:ZZ_*_*_[BL]_H_[Z12]", NSTUNSET, NSTUNSET, 0);
  REQUIRE (rv == 0, "ms3_addselect() did not return expected 0");

  /* Test matches */
  match = ms3_matchselect (selections, "FDSN:XX_S2__L_H_Z", NSTUNSET, NSTUNSET, 1, NULL);
  CHECK (match != NULL, "ms3_matchselect() did not return expected match");

  match = ms3_matchselect (selections, "FDSN:YY_STA1__B_H_Z", starttime, endtime, 2, &timematch);
  CHECK (match != NULL, "ms3_matchselect() did not return expected match");
  CHECK (timematch != NULL, "ms3_matchselect() did not return expected time match");

  match = ms3_matchselect (selections, "FDSN:YY_STA1__L_H_Z", starttime, endtime, 2, &timematch);
  CHECK (match != NULL, "ms3_matchselect() did not return expected match");
  CHECK (timematch != NULL, "ms3_matchselect() did not return expected time match");

  match = ms3_matchselect (selections, "FDSN:ZZ_STA1_LO_B_H_Z", starttime, endtime, 0, &timematch);
  CHECK (match != NULL, "ms3_matchselect() did not return expected match");

  /* Test non matches */
  match = ms3_matchselect (selections, "FDSN:YY_STA2__B_H_Z", starttime, endtime, 0, &timematch);
  CHECK (match == NULL, "ms3_matchselect() returned unexpected match");
  CHECK (timematch == NULL, "ms3_matchselect() returned unexpected time match");

  match = ms3_matchselect (selections, "FDSN:YY_STA1__L_H_Z", 0, 10, 0, &timematch);
  CHECK (match == NULL, "ms3_matchselect() returned unexpected match");
  CHECK (timematch == NULL, "ms3_matchselect() returned unexpected time match");

  match = ms3_matchselect (selections, "FDSN:YY_STA1__L_H_Z", starttime, endtime, 3, &timematch);
  CHECK (match == NULL, "ms3_matchselect() returned unexpected match");
  CHECK (timematch == NULL, "ms3_matchselect() returned unexpected time match");

  match = ms3_matchselect (selections, "FDSN:ZZ_STA1_LO_H_H_Z", starttime, endtime, 0, &timematch);
  CHECK (match == NULL, "ms3_matchselect() did not return expected match");
  CHECK (timematch == NULL, "ms3_matchselect() did not return expected time match");

  ms3_freeselections (selections);
}

TEST (selection, openended)
{
  MS3Selections *selections = NULL;
  const MS3Selections *match = NULL;
  const MS3SelectTime *timematch = NULL;
  nstime_t winstart = ms_timestr2nstime ("2010-01-01T00:00:00Z");
  nstime_t winend = ms_timestr2nstime ("2010-01-02T00:00:00Z");
  nstime_t before = ms_timestr2nstime ("2009-12-01T00:00:00Z");
  nstime_t after = ms_timestr2nstime ("2010-02-01T00:00:00Z");
  int rv;

  /* Selection with a single bounded time window */
  rv = ms3_addselect (&selections, "FDSN:XX_TEST__B_H_Z", winstart, winend, 0);
  REQUIRE (rv == 0, "ms3_addselect() did not return expected 0");

  /* Open-ended end, starting before the window end */
  match = ms3_matchselect (selections, "FDSN:XX_TEST__B_H_Z", before, NSTUNSET, 0, &timematch);
  CHECK (match != NULL, "open-ended-end query starting before window end should match");

  match = ms3_matchselect (selections, "FDSN:XX_TEST__B_H_Z", winstart, NSTUNSET, 0, &timematch);
  CHECK (match != NULL, "open-ended-end query starting inside window should match");

  /* Open-ended end, starting after the window end: must NOT match */
  match = ms3_matchselect (selections, "FDSN:XX_TEST__B_H_Z", after, NSTUNSET, 0, &timematch);
  CHECK (match == NULL, "open-ended-end query starting after window end should not match");

  /* Open-ended start, ending after the window start: should match */
  match = ms3_matchselect (selections, "FDSN:XX_TEST__B_H_Z", NSTUNSET, after, 0, &timematch);
  CHECK (match != NULL, "open-ended-start query ending after window start should match");

  /* Open-ended start, ending before the window start: must NOT match */
  match = ms3_matchselect (selections, "FDSN:XX_TEST__B_H_Z", NSTUNSET, before, 0, &timematch);
  CHECK (match == NULL, "open-ended-start query ending before window start should not match");

  /* Fully open query should match the bounded window */
  match = ms3_matchselect (selections, "FDSN:XX_TEST__B_H_Z", NSTUNSET, NSTUNSET, 0, &timematch);
  CHECK (match != NULL, "fully open query should match bounded window");

  ms3_freeselections (selections);
}

TEST (selection, error)
{
  MS3Selections *selections = NULL;
  const MS3Selections *match = NULL;
  nstime_t starttime = ms_timestr2nstime ("2010-02-27T06:50:00.069539Z");
  nstime_t endtime = ms_timestr2nstime ("2010-02-27T07:55:51.069539Z");
  int rv;

  rv = ms3_addselect (NULL, "FDSN:XX_*", NSTUNSET, NSTUNSET, 0);
  REQUIRE (rv == -1, "ms3_addselect() did not return expected -1");

  rv = ms3_addselect (&selections, NULL, NSTUNSET, NSTUNSET, 0);
  REQUIRE (rv == -1, "ms3_addselect() did not return expected -1");

  /* Inverted time window: start later than end */
  rv = ms3_addselect (&selections, "FDSN:XX_*", endtime, starttime, 0);
  REQUIRE (rv == -1, "ms3_addselect() did not reject inverted window");

  /* Open-ended windows on either side must still be accepted */
  rv = ms3_addselect (&selections, "FDSN:XX_*", NSTUNSET, endtime, 0);
  REQUIRE (rv == 0, "ms3_addselect() did not accept open-ended start");

  rv = ms3_addselect (&selections, "FDSN:XX_*", starttime, NSTUNSET, 0);
  REQUIRE (rv == 0, "ms3_addselect() did not accept open-ended end");

  match = ms3_matchselect (NULL, "FDSN:YY_STA1__L_H_Z", NSTUNSET, NSTUNSET, 1, NULL);
  REQUIRE (match == NULL, "ms3_matchselect() did not return expected NULL");

  match = ms3_matchselect (selections, "FDSN:YY_STA1__L_H_Z", NSTUNSET, NSTUNSET, 1, NULL);
  REQUIRE (match == NULL, "ms3_matchselect() did not return expected NULL");

  ms3_freeselections (selections);
}