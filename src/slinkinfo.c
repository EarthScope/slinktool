/***************************************************************************
 * slinkxml.c
 *
 * INFO message printing routines
 *
 * @author Chad Trabant, EarthScope Data Center
 ***************************************************************************/

#include <stdio.h>
#include <string.h>

#include <libslink.h>
#include <yyjson.h>

#include "slinkinfo.h"

/***************************************************************************
 * print_info_json():
 *
 * Format the specified JSON document into an identification summary.
 ***************************************************************************/
void
print_info_json (const char *json, size_t json_length, int verbose)
{
  yyjson_doc *doc  = yyjson_read (json, json_length, 0);
  yyjson_val *root = yyjson_doc_get_root (doc);

  size_t idx, max;
  yyjson_val *key, *val;

  size_t idx2, max2;
  yyjson_val *key2, *val2;

  if (doc == NULL || root == NULL)
  {
    sl_log (1, 0, "%s() JSON INFO not provided\n", __func__);
    return;
  }

  /* ERROR */
  yyjson_val *error = yyjson_obj_get (root, "error");
  if (error)
  {
    const char *error_code    = yyjson_get_str (yyjson_obj_get (error, "code"));
    const char *error_message = yyjson_get_str (yyjson_obj_get (error, "message"));

    fprintf (stderr, "error: %s, %s\n",
             (error_code) ? error_code : "",
             (error_message) ? error_message : "");

    return;
  }

  yyjson_val *capability  = yyjson_obj_get (root, "capability");
  yyjson_val *format      = yyjson_obj_get (root, "format");
  yyjson_val *filter      = yyjson_obj_get (root, "filter");
  yyjson_val *station     = yyjson_obj_get (root, "station");
  yyjson_val *connections = yyjson_obj_get (root, "connections");
  yyjson_val *client      = yyjson_obj_get (connections, "client");

  /* ID level INFO */
  if ((!capability && !format && !station && !client) || verbose)
  {
    const char *software     = yyjson_get_str (yyjson_obj_get (root, "software"));
    const char *organization = yyjson_get_str (yyjson_obj_get (root, "organization"));

    printf ("SeedLink server: %s\n"
            "Organization   : %s\n",
            (software) ? software : "",
            (organization) ? organization : "");

    const char *server_start = yyjson_get_str (yyjson_obj_get (root, "server_start"));
    if (server_start)
      printf ("Start time     : %s\n",
              (server_start) ? server_start : "");

    printf ("\n");
  }

  /* CAPABILITY */
  if (capability)
  {
    fprintf (stdout, "Capabilities:\n");
    yyjson_arr_foreach (capability, idx, max, val)
    {
      const char *val_str = yyjson_get_str (val);

      printf ("  %s\n",
              (val_str) ? val_str : "");
    }
  }

  /* FORMAT */
  if (format)
  {
    fprintf (stdout, "Formats:\n");
    yyjson_obj_foreach (format, idx, max, key, val)
    {
      const char *key_str   = yyjson_get_str (key);
      yyjson_val *mimetype  = yyjson_obj_get (val, "mimetype");
      const char *mime_str  = yyjson_get_str (mimetype);

      printf ("  %s: %s\n",
              (key_str) ? key_str : "",
              (mime_str) ? mime_str : "");

      yyjson_val *subformat = yyjson_obj_get (val, "subformat");
      if (subformat)
      {
        yyjson_obj_foreach (subformat, idx2, max2, key2, val2)
        {
          const char *key_str2 = yyjson_get_str (key2);
          const char *val_str2 = yyjson_get_str (val2);

          printf ("    %s: %s\n",
                  (key_str2) ? key_str2 : "",
                  (val_str2) ? val_str2 : "");
        }
      }
    }
  }

  /* FILTER */
  if (filter)
  {
    fprintf (stdout, "Filters:\n");
    yyjson_obj_foreach (filter, idx, max, key, val)
    {
      const char *key_str = yyjson_get_str (key);
      const char *val_str = yyjson_get_str (val);

      printf ("  %s: %s\n",
              (key_str) ? key_str : "",
              (val_str) ? val_str : "");
    }
  }

  /* STATION and STREAM */
  if (station)
  {
    yyjson_arr_foreach (station, idx, max, val)
    {
      const char *id          = yyjson_get_str (yyjson_obj_get (val, "id"));
      const char *description = yyjson_get_str (yyjson_obj_get (val, "description"));
      uint64_t start_seq      = yyjson_get_uint (yyjson_obj_get (val, "start_seq"));
      uint64_t end_seq        = yyjson_get_uint (yyjson_obj_get (val, "end_seq"));

      yyjson_val *stream = yyjson_obj_get (val, "stream");

      if (!stream) /* Station-only output */
      {
        if (verbose)
        {
          printf ("%-12s %s, start seq: %" PRIu64 ", end seq: %" PRIu64 "\n",
                  (id) ? id : "",
                  (description) ? description : "",
                  start_seq,
                  end_seq);
        }
        else
        {
          printf ("%-12s %s\n",
                  (id) ? id : "",
                  (description) ? description : "");
        }
      }
      else /* Station and Stream output */
      {
        yyjson_arr_foreach (stream, idx2, max2, val2)
        {
          const char *streamid   = yyjson_get_str (yyjson_obj_get (val2, "id"));
          const char *start_time = yyjson_get_str (yyjson_obj_get (val2, "start_time"));
          const char *end_time   = yyjson_get_str (yyjson_obj_get (val2, "end_time"));
          const char *format     = yyjson_get_str (yyjson_obj_get (val2, "format"));
          const char *subformat  = yyjson_get_str (yyjson_obj_get (val2, "subformat"));

          if (verbose)
          {
            printf ("%-12s %-12s  %s - %s, format: %s, subformat: %s\n",
                    (id) ? id : "",
                    (streamid) ? streamid : "",
                    (start_time) ? start_time : "",
                    (end_time) ? end_time : "",
                    (format) ? format : "",
                    (subformat) ? subformat : "");
          }
          else
          {
            printf ("%-12s %-12s  %s - %s\n",
                    (id) ? id : "",
                    (streamid) ? streamid : "",
                    (start_time) ? start_time : "",
                    (end_time) ? end_time : "");
          }
        }
      }
    }
  }

  /* CONNECTION */
  if (client)
  {
    yyjson_arr_foreach (client, idx, max, val)
    {
      const char *host           = yyjson_get_str (yyjson_obj_get (val, "host"));
      const char *ip_address     = yyjson_get_str (yyjson_obj_get (val, "ip_address"));
      const char *client_port    = yyjson_get_str (yyjson_obj_get (val, "client_port"));
      const char *type           = yyjson_get_str (yyjson_obj_get (val, "type"));
      const char *server_port    = yyjson_get_str (yyjson_obj_get (val, "server_port"));
      const char *connect_time   = yyjson_get_str (yyjson_obj_get (val, "connect_time"));
      const char *client_id      = yyjson_get_str (yyjson_obj_get (val, "client_id"));
      yyjson_val *lag_percent    = yyjson_obj_get (val, "lag_percent");
      yyjson_val *lag_seconds    = yyjson_obj_get (val, "lag_seconds");
      yyjson_val *tx_packets     = yyjson_obj_get (val, "transmit_packets");
      yyjson_val *tx_packet_rate = yyjson_obj_get (val, "transmit_packet_rate");
      yyjson_val *tx_bytes       = yyjson_obj_get (val, "transmit_bytes");
      yyjson_val *tx_byte_rate   = yyjson_obj_get (val, "transmit_byte_rate");
      yyjson_val *rx_packets     = yyjson_obj_get (val, "receive_packets");
      yyjson_val *rx_packet_rate = yyjson_obj_get (val, "receive_packet_rate");
      yyjson_val *rx_bytes       = yyjson_obj_get (val, "receive_bytes");
      yyjson_val *rx_byte_rate   = yyjson_obj_get (val, "receive_byte_rate");
      yyjson_val *stream_count   = yyjson_obj_get (val, "stream_count");
      const char *match          = yyjson_get_str (yyjson_obj_get (val, "match"));
      const char *reject         = yyjson_get_str (yyjson_obj_get (val, "reject"));

      printf ("%s [%s:%s] using %s on port %s, connected at %s\n",
              (host) ? host : "",
              (ip_address) ? ip_address : "",
              (client_port) ? client_port : "",
              (type) ? type : "",
              (server_port) ? server_port : "",
              (connect_time) ? connect_time : "");

      if (client_id)
        printf ("  Client ID: %s\n", client_id);

      if (lag_percent || lag_seconds)
        printf ("  Lag: %.1f%%, %.1f seconds\n",
                yyjson_get_real (lag_percent), yyjson_get_real (lag_seconds));

      if (tx_packets || tx_packet_rate || tx_bytes || tx_byte_rate)
        printf ("  Transmit: %" PRIu64 " packets, %.1f packets/sec, %" PRIu64 " bytes, %.1f bytes/sec\n",
                yyjson_get_uint (tx_packets), yyjson_get_real (tx_packet_rate),
                yyjson_get_uint (tx_bytes), yyjson_get_real (tx_byte_rate));

      if (rx_packets || rx_packet_rate || rx_bytes || rx_byte_rate)
        printf ("  Receive: %" PRIu64 " packets, %.1f packets/sec, %" PRIu64 " bytes, %.1f bytes/sec\n",
                yyjson_get_uint (rx_packets), yyjson_get_real (rx_packet_rate),
                yyjson_get_uint (rx_bytes), yyjson_get_real (rx_byte_rate));

      if (stream_count)
        printf ("  Stream count: %" PRIu64 "\n", yyjson_get_uint (stream_count));

      if (match)
        printf ("  Match: %s\n", match);

      if (reject)
        printf ("  Reject: %s\n", reject);

      printf ("\n");
    }
  }

  yyjson_doc_free (doc);
}

/***************************************************************************
 * print_info_xml():
 *
 * Format the specified XML document into an identification summary.
 ***************************************************************************/
void
print_info_xml (char *xml, size_t xml_length, int verbose)
{
  ezxml_t xmldoc;

  if ((xmldoc = ezxml_parse_str (xml, xml_length)) == NULL)
  {
    sl_log (2, 0, "%s(): XML parse error\n", __func__);

    return;
  }

  char *rootname = ezxml_name (xmldoc);

  if (rootname == NULL || strcmp (rootname, "seedlink"))
  {
    sl_log (1, 0, "XML INFO root tag is not <seedlink>, invalid data\n");
    return;
  }

  ezxml_t station = ezxml_child (xmldoc, "station");

  if (verbose || !station)
  {
    const char *software     = ezxml_attr (xmldoc, "software");
    const char *organization = ezxml_attr (xmldoc, "organization");
    printf ("SeedLink server: %s\n"
            "Organization   : %s\n",
            (software) ? software : "",
            (organization) ? organization : "");

    const char *started = ezxml_attr (xmldoc, "started");
    if (started)
      printf ("Start time     : %s\n", started);

    printf ("\n");
  }

  /* STATION and STREAM and GAP or CONNECTION */
  for (; station; station = ezxml_next (station))
  {
    const char *network     = ezxml_attr (station, "network");
    const char *name        = ezxml_attr (station, "name");
    const char *description = ezxml_attr (station, "description");
    const char *begin_seq   = ezxml_attr (station, "begin_seq");
    const char *end_seq     = ezxml_attr (station, "end_seq");

    ezxml_t stream     = ezxml_child (station, "stream");
    ezxml_t connection = ezxml_child (station, "connection");

    char id[100] = {0};
    snprintf (id, sizeof (id), "%s_%s",
              (network) ? network : "",
              (name) ? name : "");

    if (!connection) /* Real stations, and optionally streams */
    {
      if (!stream) /* Station-only output */
      {
        if (verbose)
        {
          printf ("%-12s %s, start seq: %s, end seq: %s\n",
                  id,
                  (description) ? description : "",
                  begin_seq,
                  end_seq);
        }
        else
        {
          printf ("%-12s %s\n",
                  id,
                  (description) ? description : "");
        }
      }
      else /* Station and Stream output */
      {
        for (; stream; stream = ezxml_next (stream))
        {
          const char *location   = ezxml_attr (stream, "location");
          const char *seedname   = ezxml_attr (stream, "seedname");
          const char *begin_time = ezxml_attr (stream, "begin_time");
          const char *end_time   = ezxml_attr (stream, "end_time");

          char streamid[100] = {0};
          snprintf (streamid, sizeof (streamid), "%s_%s",
                    (location) ? location : "",
                    (seedname) ? seedname : "");

          printf ("%-12s %-12s  %s - %s\n",
                  id,
                  streamid,
                  (begin_time) ? begin_time : "",
                  (end_time) ? end_time : "");

          ezxml_t gap;
          for (gap = ezxml_child (stream, "gap"); gap; gap = ezxml_next (gap))
          {
            const char *begin_time = ezxml_attr (gap, "begin_time");
            const char *end_time   = ezxml_attr (gap, "end_time");

            printf ("  Gap: %s - %s\n",
                    (begin_time) ? begin_time : "",
                    (end_time) ? end_time : "");
          }
        }
      }
    }
    else /* Connection details encoded in station structure */
    {
      for (; connection; connection = ezxml_next (connection))
      {
        const char *host         = ezxml_attr (connection, "host");
        const char *client_port  = ezxml_attr (connection, "port");
        const char *connect_time = ezxml_attr (connection, "ctime");
        const char *txcount      = ezxml_attr (connection, "txcount");
        const char *txbytes      = ezxml_attr (connection, "totBytes");

        printf ("%s [:%s], connected at %s\n",
                (host) ? host : "",
                (client_port) ? client_port : "",
                (connect_time) ? connect_time : "");

        if (description)
          printf ("  Client ID: %s\n", description);

        printf ("  Station ID: %s\n", id);

        if (verbose)
        {
          ezxml_t selector = ezxml_child (connection, "selector");
          for (; selector; selector = ezxml_next (selector))
          {
            const char *pattern = ezxml_attr (selector, "pattern");
            printf ("    Selector: %s\n", (pattern) ? pattern : "");
          }
        }

        if (txcount || txbytes)
          printf ("  Transmit: %s packets, %s bytes\n",
                  (txcount) ? txcount : "",
                  (txbytes) ? txbytes : "-");

        if (verbose)
        {
          const char *current_seq = ezxml_attr (connection, "current_seq");
          printf ("  Current sequence: %s\n", current_seq);

          const char *realtime = ezxml_attr (connection, "realtime");
          printf ("  Realtime: %s\n", (realtime) ? realtime : "");

          const char *window = (ezxml_child (connection, "window")) ? "yes" : "no";
          printf ("  Time window: %s\n", window);

          const char *end_of_data = ezxml_attr (connection, "end_of_data");
          printf ("  End of data: %s\n", (end_of_data) ? end_of_data : "");
        }

        printf ("\n");
      }
    }
  }

  ezxml_free (xmldoc);
}
