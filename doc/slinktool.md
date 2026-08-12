# slinktool — SeedLink client for data stream inspection, data collection and server testing

1. [Synopsis](#synopsis)
1. [Description](#description)
1. [Options](#options)
1. [Examples](#examples)
1. [Seedlink Selectors](#seedlink-selectors)
1. [Stream List File](#stream-list-file)
1. [Environment](#environment)
1. [Notes](#notes)
1. [Author](#author)

## <a id="synopsis">Synopsis</a>

```
slinktool [options] [host][:][port]

```

## <a id="description">Description</a>

<b>slinktool</b> connects to a <em>SeedLink</em> server and queries the server for information or requests data using uni-station or multi-station mode and prints information about the packets received. All received packets can optionally be dumped to a single file.

## <a id="options">Options</a>

- <b>-V</b>
  Report program version and exit.

- <b>-h</b>
  Print program usage and exit.

- <b>-v</b>
  Be more verbose.  This flag can be used multiple times ("-v -v" or "-vv") for more verbosity.  One flag: report basic handshaking (link configuration) details and briefly report each packet received.  Two flags: report the details of the handshaking, each packet received and detailed connection diagnostics.

- <b>-P</b>
  Ping the server: connect, print out the server ID and exit.  If the server was successfully contacted the return code will be 0, if errors were encountered the return code will be 1.

- <b>-p</b>
  Print details of received miniSEED data records. This flag can be used multiple times ("-p -p" or "-pp") for more detail.  One flag: a single summary line for each data packet received.  Two+ flags: more details of the payload received.

- <b>-u</b>
  Print data samples in data packets, implies at least one -p flag. By default only 6 lines of samples are printed (36 samples), adding more -u flags (e.g. -uu) will print all samples.

- <b>-T</b>
  Enable a secure TLS connection.  TLS is also enabled automatically when the server port is 18500.

- <b>-Ap</b>
  Prompt for username and password authentication details (SeedLink v4 only).  See also the <em>SEEDLINK_USERNAME</em> and <em>SEEDLINK_PASSWORD</em> environment variables.

- <b>-At</b>
  Prompt for a JWT authentication token (SeedLink v4 only).

- <b>-3</b>
  Use the SeedLink 3.x protocol explicitly.  By default the protocol is negotiated with the server.

- <b>-4</b>
  Use the SeedLink 4.0 protocol explicitly.  By default the protocol is negotiated with the server.

- -nd <em>delay</em>
  The network reconnect delay (in seconds) for the connection to the SeedLink server.  If the connection breaks for any reason this will govern how soon a reconnection should be attempted. The default value is 30 seconds.

- -nt <em>timeout</em>
  The network timeout (in seconds) for the connection to the SeedLink server.  If no data or keepalive packets are received in this time the connection is closed and re-established (after the reconnect delay has expired).  The default value is 600 seconds. A value of 0 disables the timeout.

- -k <em>keepalive</em>  (requires SeedLink &gt;= 3)
  Keepalive packet interval (in seconds) at which keepalive (heartbeat) packets are sent to the server.  Keepalive packets are only sent if nothing is received within the interval.

- -x <em>statefile</em>[:<em>interval</em>]
  During client shutdown the last received sequence numbers and time stamps (start times) for each data stream will be saved in this file. If this file exists upon startup the information will be used to resume the data streams from the point at which they were stopped.  In this way the client can be stopped and started without data loss, assuming the data are still available on the server.  If <em>interval</em> is specified the state will be saved every <em>interval</em> packets that are received.  Otherwise the state will be saved only on normal program termination.

- <b>-d</b>
  Configure the connection in "dial-up" mode.  The remote server will close the connection when it has sent all of the data in its buffers for the selected data streams.  This is opposed to the normal behavior of waiting indefinitely for data.

- <b>-b</b>
  Configure the connection in "batch" mode (SeedLink v3).  Negotiation with the remote server is made faster by minimizing acknowledgement checks.

- -o <em>dumpfile</em>
  If specified, all packets (miniSEED records) received will be appended to this file.  The file is created if it does not exist.  A special mode for this option is to send all received packets to standard output when the dumpfile is specified as '-'.  In this case all output besides these records will be redirected to standard error.

- -s <em>selectors</em>
  This defines default selectors.  If no multi-station data streams are configured these selectors will be used for uni-station mode. Otherwise these selectors will be used when no selectors are specified for a given stream using the '-S' or '-l' options.

- -l <em>streamfile</em>
  A list of streams will be read from the given file.  This option implies multi-station mode.  The format of the stream list file is given below in the section <em>Stream list file</em>.

- -S <em>stream[:selectors],...</em>  (requires SeedLink &gt;= 2.5)
  A list of streams is given as an argument.  This option implies multi-station mode.  The stream list is composed of multiple streams (stations) and optional selectors.  <em>stream</em> should be provided in NET_STA format and <em>selectors</em> are normal SeedLink selectors, see examples and notes below.  If no selectors are provided for a given stream, the default selectors, if defined, will be used.

- -ts <em>starttime</em>
  Specify a start time to request from the server. Warning: time windowing might be disabled on the remote server.

- -te <em>endtime</em>
  Specify an end time to request from the server. Warning: time windowing might be disabled on the remote server.

- -i <em>type</em>  (requires SeedLink &gt;= 3)
  Send an information request (INFO) and print the raw response. Standard info types include: ID, CAPABILITIES, STATIONS, STREAMS, CONNECTIONS, FORMATS, GAPS, ALL.

- -F <em>type</em>  (requires SeedLink &gt;= 3)
  Send an information request (INFO), parse the response, and print a formatted form.  Uses the same info types as \fB-i\fR.

- <b>-f</b>
  Increase the level of detail included in formatted INFO output. This flag can be used multiple times.

  Formatted INFO shortcuts:

  ```
  -I   equivalent to -F ID
  -L   equivalent to -F STATIONS
  -Q   equivalent to -F STREAMS
  -G   equivalent to -F GAPS
  -C   equivalent to -F CONNECTIONS
  ```

  Warning: informational (INFO) messages might be disabled on the server.

- <b>[host][:][port]</b>
  A required argument, specifies the address of the SeedLink server in host:port format.  Either the host, port or both can be omitted.  If host is omitted then localhost is assumed, i.e. ':18000' implies 'localhost:18000'.  If the port is omitted then 18000 is assumed, i.e. 'localhost' implies 'localhost:18000'.  If only ':' is specified 'localhost:18000' is assumed.  Port 18500 enables TLS automatically.

## <a id="examples">Examples</a>

- <b>All-station/Uni-station mode example:</b>
  The following would connect to a SeedLink server at slink.host.com port 18000 and configure the link in all-station/uni-station mode, exactly which data are received depends on the data being served by the SeedLink server on that particular port.  Additionally, all of the received packets are appended to the file 'data.mseed' and each packet received is reported on the standard output.

  <b>&gt; slinktool -v -o data.mseed slink.host.com:18000</b>

  The '-s' argument could be used to indicate selectors to limit the type of packets sent by the SeedLink server (without selectors all packet types are sent).  The following would limit this connection to BHZ channel waveform data with a location code of 10 (see an explanation of SeedLink selectors below).  Additionally another verbose flag is given, causing slinktool to report detailed header information from data records.

  <b>&gt; slinktool -vv -s 10BHZ.D -o data.mseed slink.host.com:18000</b>

- <b>Multi-station mode example:</b>
  The following example would connect to a SeedLink server on localhost port 18010 and configure the link in multi-station mode.  Each station specified with the '-S' argument will be requested, optionally specifying selectors for each station.

  <b>&gt; slinktool -v -S GE_WLF,MN_AQU:00???,IU_KONO:BHZ.D :18010</b>

  This would request GEOFON station WLF (all data as no selectors were indicated), MedNet station AQU with location code 00 (all channels) and IU network station KONO (only waveform data) from channel BHZ.

  Of course, a variety of different data selections can be made:

  <b>-s 'BHE.D BHN.D' -S 'GE_STU,GE_MALT,GE_WLF'</b>   (horizontal BH channels, data only)

  <b>-s BHZ -S GE_STU,GE_WLF,GE_RUE,GE_EIL</b>   (vertical channels only)

- <b>Wildcarding network and station codes</b>
  Some SeedLink implementations support wildcarding of the network and station codes, when this is the case the only two wildcard characters recognized are '*' for one or more characters and '?' for any single character.

  As an example, all US network data can be requested using the following syntax:

  <b>-S 'US_*'</b>

## <a id="seedlink-selectors">Seedlink Selectors</a>

SeedLink selectors are used to request specific types of data within a given data stream, in effect limiting the default action of sending all data types.  A data packet is sent to the client if it matches any positive selector (without leading "!") and doesn't match any negative selectors (with a leading "!").  The general format of selectors is LLSSS.T, where LL is location, SSS is channel and T is type (one of [DECOTL] for Data, Event, Calibration, Blockette, Timing, and Log records).  "LL", ".T", and "LLSSS." can be omitted, implying anything in that field.  It is also possible to use "?" in place of L and S as a single character wildcard.  Multiple selectors are separated by space(s).

```
Some examples:
BH?          - BHZ, BHN, BHE (all record types)
00BH?.D      - BHZ, BHN, BHE with location code '00' (data records)
BH? !E       - BHZ, BHN, BHE (excluding detection records)
BH? E        - BHZ, BHN, BHE & detection records of all channels
!LCQ !LEP    - exclude LCQ and LEP channels
!L !T        - exclude log and timing records
```

## <a id="stream-list-file">Stream List File</a>

The stream list file used with the '-l' option is expected to define a data stream on each line.  The format of each line is:

```
StationID [selectors]
```

<em>StationID</em> is in NET_STA format.  The selectors are optional.  If default selectors are also specified (with the '-s' option), they will be used when no selectors are specified for a given stream.  An example file follows:

```
----  Begin example file -----
# Comment lines begin with a '#'
# Example stream list file for use with the -l argument of slinktool or
# with the sl_add_streamlist_file() libslink function.
GE_ISP  BH?.D
NL_HGN
MN_AQU  BH? HH?
----  End example file -----
```

## <a id="environment">Environment</a>

- <b>SEEDLINK_USERNAME</b>

- <b>SEEDLINK_PASSWORD</b>
  If both are set, they are used for SeedLink v4 USERPASS authentication. This is an alternative to prompting with \fB-Ap\fR.  The server must support authentication.

- <b>LIBSLINK_CA_CERT_FILE</b>
  Location of a CA certificate file to load for TLS server verification.

- <b>LIBSLINK_CA_CERT_PATH</b>
  Path to a directory of CA certificate files to load for TLS server verification.

- <b>LIBSLINK_TLS_DEBUG</b>
  TLS debugging output level (integer), increase for more detail.

- <b>LIBSLINK_CERT_UNVERIFIED_OK</b>
  If set, allow unverified TLS certificates (TLS 1.2 connections only).

## <a id="notes">Notes</a>

All diagnostic output from slinktool is printed to standard error (stderr), exceptions are when printing miniSEED packet details (the -p flag), when printing unpacked samples (the -u flag) and when printing the raw or formatted responses to INFO requests.

## <a id="author">Author</a>

```
Chad Trabant
previously ORFEUS Data Center/EC-Project MEREDIAN
previously IRIS Data Management Center
EarthScope Data Services
```

---

*Generated from man page dated 2026/08/12.*
