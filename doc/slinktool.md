# slinktool
### SeedLink client for data stream inspection, data collection and server testing

1. [Synopsis](#synopsis)
1. [Description](#description)
1. [Options](#options)
1. [Examples](#examples)
1. [SeedLink SELECTORS](#seedlink-selectors)
1. [Stream list file](#stream-list-file)
1. [Environment](#environment)
1. [Notes](#notes)
1. [Author](#author)

## <a id='synopsis'>Synopsis</a>

<pre>
slinktool [options] [host][:][port]
</pre>

## <a id='description'>Description</a>

<p><b>slinktool</b> connects to a <i>SeedLink</i> server and queries the server for information or requests data using uni-station or multi-station mode and prints information about the packets received. All received packets can optionally be dumped to a single file.</p>

## <a id='options'>Options</a>

<dl>
<dt><b>-V</b></dt>
<dd>
<p>Report program version and exit.</p>

</dd>
<dt><b>-h</b></dt>
<dd>
<p>Print program usage and exit.</p>

</dd>
<dt><b>-v</b></dt>
<dd>
<p>Be more verbose.  This flag can be used multiple times ("-v -v" or "-vv") for more verbosity.  One flag: report basic handshaking (link configuration) details and briefly report each packet received.  Two flags: report the details of the handshaking, each packet received and detailed connection diagnostics.</p>

</dd>
<dt><b>-P</b></dt>
<dd>
<p>Ping the server: connect, print out the server ID and exit.  If the server was successfully contacted the return code will be 0, if errors were encountered the return code will be 1.</p>

</dd>
<dt><b>-p</b></dt>
<dd>
<p>Print details of received miniSEED data records. This flag can be used multiple times ("-p -p" or "-pp") for more detail.  One flag: a single summary line for each data packet received.  Two+ flags: more details of the payload received.</p>

</dd>
<dt><b>-u</b></dt>
<dd>
<p>Print data samples in data packets, implies at least one -p flag. By default only 6 lines of samples are printed (36 numeric samples or 420 characters of text), adding more -u flags (e.g. -uu) will print all samples.</p>

</dd>
<dt><b>-T</b></dt>
<dd>
<p>Enable a secure TLS connection.  TLS is also enabled automatically when the server port is 18500.</p>

</dd>
<dt><b>-Ap</b></dt>
<dd>
<p>Prompt for username and password authentication details (SeedLink v4 only).  See also the <i>SEEDLINK_USERNAME</i> and <i>SEEDLINK_PASSWORD</i> environment variables.</p>

</dd>
<dt><b>-At</b></dt>
<dd>
<p>Prompt for a JWT authentication token (SeedLink v4 only).</p>

</dd>
<dt><b>-3</b></dt>
<dd>
<p>Use the SeedLink 3.x protocol explicitly.  By default the protocol is negotiated with the server.</p>

</dd>
<dt><b>-4</b></dt>
<dd>
<p>Use the SeedLink 4.0 protocol explicitly.  By default the protocol is negotiated with the server.</p>

</dd>
<dt><b>-nd</b> <i>delay</i></dt>
<dd>
<p>The network reconnect delay (in seconds) for the connection to the SeedLink server.  If the connection breaks for any reason this will govern how soon a reconnection should be attempted. The default value is 30 seconds.</p>

</dd>
<dt><b>-nt</b> <i>timeout</i></dt>
<dd>
<p>The network timeout (in seconds) for the connection to the SeedLink server.  If no data or keepalive packets are received in this time the connection is closed and re-established (after the reconnect delay has expired).  The default value is 600 seconds. A value of 0 disables the timeout.</p>

</dd>
<dt><b>-k</b> <i>keepalive</i>  (requires SeedLink &gt;= 3)</dt>
<dd>
<p>Keepalive packet interval (in seconds) at which keepalive (heartbeat) packets are sent to the server.  Keepalive packets are only sent if nothing is received within the interval.</p>

</dd>
<dt><b>-x</b> <i>statefile</i>[:<i>interval</i>]</dt>
<dd>
<p>During client shutdown the last received sequence numbers and time stamps (start times) for each data stream will be saved in this file. If this file exists upon startup the information will be used to resume the data streams from the point at which they were stopped.  In this way the client can be stopped and started without data loss, assuming the data are still available on the server.  If <i>interval</i> is specified the state will be saved every <i>interval</i> packets that are received.  Otherwise the state will be saved only on normal program termination.  Stations matched by a wildcarded stream selection, or received in all-station mode, are tracked individually.  The state is written to <i>statefile</i>.tmp and then renamed, so the directory containing the state file must be writable.</p>

</dd>
<dt><b>-d</b></dt>
<dd>
<p>Configure the connection in "dial-up" mode.  The remote server will close the connection when it has sent all of the data in its buffers for the selected data streams.  This is opposed to the normal behavior of waiting indefinitely for data.</p>

</dd>
<dt><b>-b</b></dt>
<dd>
<p>Configure the connection in "batch" mode (SeedLink v3).  Negotiation with the remote server is made faster by minimizing acknowledgement checks.</p>

</dd>
<dt><b>-o</b> <i>dumpfile</i></dt>
<dd>
<p>If specified, all miniSEED data records (miniSEED 2 or 3) received will be appended to this file.  INFO responses are not written to the file.  The file is created if it does not exist.  A special mode for this option is to send all received data records to standard output when the dumpfile is specified as '-'.  In this case all output besides these records will be redirected to standard error.</p>

</dd>
<dt><b>-s</b> <i>selectors</i></dt>
<dd>
<p>This defines default selectors.  If no multi-station data streams are configured these selectors will be used for uni-station mode. Otherwise these selectors will be used when no selectors are specified for a given stream using the '-S' or '-l' options.</p>

</dd>
<dt><b>-l</b> <i>streamfile</i></dt>
<dd>
<p>A list of streams will be read from the given file.  This option implies multi-station mode.  The format of the stream list file is given below in the section <i>Stream list file</i>.</p>

</dd>
<dt><b>-S</b> <i>stream[:selectors],...</i>  (requires SeedLink &gt;= 2.5)</dt>
<dd>
<p>A list of streams is given as an argument.  This option implies multi-station mode.  The stream list is composed of multiple streams (stations) and optional selectors.  <i>stream</i> should be provided in NET_STA format and <i>selectors</i> are normal SeedLink selectors, see examples and notes below.  If no selectors are provided for a given stream, the default selectors, if defined, will be used.</p>

</dd>
<dt><b>-ts</b> <i>starttime</i></dt>
<dd>
<p>Specify a start time to request from the server.  Times are in UTC and may be given in ISO 8601 form, <i>YYYY-MM-DDThh:mm:ss[.ffffff]</i>, or SeedLink comma-separated form, <i>YYYY,MM,DD,hh,mm,ss</i>.  Trailing fields may be omitted. Warning: time windowing might be disabled on the remote server.</p>

</dd>
<dt><b>-te</b> <i>endtime</i></dt>
<dd>
<p>Specify an end time to request from the server, in the same formats as <b>-ts</b>. Warning: time windowing might be disabled on the remote server.</p>

</dd>
<dt><b>-i</b> <i>type</i>  (requires SeedLink &gt;= 3)</dt>
<dd>
<p>Send an information request (INFO) and print the response without interpretation.  SeedLink v3 XML responses are printed as received, SeedLink v4 JSON responses are printed with indentation added. Standard info types are: ID, CAPABILITIES, STATIONS, STREAMS and CONNECTIONS.  GAPS and ALL are only available with SeedLink v3, and FORMATS only with SeedLink v4.  When combined with <b>-S</b>, the stream list is passed as a selection pattern with the request; the server might ignore it.</p>

<p>Warning: informational (INFO) messages might be disabled on the server, and some types (e.g. CONNECTIONS) are often restricted.</p>

</dd>
<dt><b>-F</b> <i>type</i>  (requires SeedLink &gt;= 3)</dt>
<dd>
<p>Send an information request (INFO), parse the response, and print a formatted form.  Uses the same info types as <b>-i</b>.</p>

</dd>
<dt><b>-f</b></dt>
<dd>
<p>Increase the level of detail included in formatted INFO output. This flag can be used multiple times.</p>

</dd>
<dt><b>-I</b></dt>
<dd>
<p>Equivalent to <b>-F ID</b>.</p>

</dd>
<dt><b>-L</b></dt>
<dd>
<p>Equivalent to <b>-F STATIONS</b>.</p>

</dd>
<dt><b>-Q</b></dt>
<dd>
<p>Equivalent to <b>-F STREAMS</b>.</p>

</dd>
<dt><b>-G</b></dt>
<dd>
<p>Equivalent to <b>-F GAPS</b> (SeedLink v3 only).</p>

</dd>
<dt><b>-C</b></dt>
<dd>
<p>Equivalent to <b>-F CONNECTIONS</b>.</p>

</dd>
<dt><b>[host][:][port]</b></dt>
<dd>
<p>A required argument, specifies the address of the SeedLink server in host:port format.  Either the host, port or both can be omitted.  If host is omitted then localhost is assumed, i.e. ':18000' implies 'localhost:18000'.  If the port is omitted then 18000 is assumed, i.e. 'localhost' implies 'localhost:18000'.  If only ':' is specified 'localhost:18000' is assumed.  Port 18500 enables TLS automatically.</p>

</dd>
</dl>

## <a id='examples'>Examples</a>

<dl>
<dt><b>All-station/Uni-station mode example:</b></dt>
<dd>
<p>The following would connect to a SeedLink server at slink.host.com port 18000 and configure the link in all-station/uni-station mode, exactly which data are received depends on the data being served by the SeedLink server on that particular port.  Additionally, all of the received packets are appended to the file 'data.mseed' and each packet received is reported on the standard output.</p>

<p><b>&gt; slinktool -v -o data.mseed slink.host.com:18000</b></p>

<p>The '-s' argument could be used to indicate selectors to limit the type of packets sent by the SeedLink server (without selectors all packet types are sent).  The following would limit this connection to BHZ channel waveform data with a location code of 10 (see an explanation of SeedLink selectors below).  Additionally two '-p' flags are given, causing slinktool to print detailed header information from each data record.</p>

<p><b>&gt; slinktool -v -pp -s 10BHZ.D -o data.mseed slink.host.com:18000</b></p>

</dd>
<dt><b>Multi-station mode example:</b></dt>
<dd>
<p>The following example would connect to a SeedLink server on localhost port 18010 and configure the link in multi-station mode.  Each station specified with the '-S' argument will be requested, optionally specifying selectors for each station.</p>

<p><b>&gt; slinktool -v -S GE_WLF,MN_AQU:00???,IU_KONO:BHZ.D :18010</b></p>

<p>This would request GEOFON station WLF (all data as no selectors were indicated), MedNet station AQU with location code 00 (all channels) and IU network station KONO (only waveform data) from channel BHZ.</p>

<p>Of course, a variety of different data selections can be made:</p>

<p><b>-s 'BHE.D BHN.D' -S 'GE_STU,GE_MALT,GE_WLF'</b><br>(horizontal BH channels, data only)</p>

<p><b>-s BHZ -S GE_STU,GE_WLF,GE_RUE,GE_EIL</b><br>(vertical channels only)</p>

</dd>
<dt><b>Wildcarding network and station codes</b></dt>
<dd>
<p>Some SeedLink implementations support wildcarding of the network and station codes, when this is the case the only two wildcard characters recognized are '*' for zero or more characters and '?' for any single character.</p>

<p>As an example, all US network data can be requested using the following syntax:</p>

<p><b>-S 'US_*'</b></p>

</dd>
</dl>

## <a id='seedlink-selectors'>SeedLink SELECTORS</a>

<p>SeedLink selectors are used to request specific types of data within a given data stream, in effect limiting the default action of sending all data types.  A data packet is sent to the client if it matches any positive selector (without leading "!") and doesn't match any negative selectors (with a leading "!").  Multiple selectors are separated by space(s).</p>

<p>The SeedLink v3 format of selectors is LLSSS.T, where LL is location, SSS is channel and T is type (one of [DECOTL] for Data, Event, Calibration, Opaque, Timing, and Log records).  "LL", ".T", and "LLSSS." can be omitted, implying anything in that field.  It is also possible to use "?" in place of L and S as a single character wildcard.</p>

<p>The SeedLink v4 format of selectors is LL_B_S_SS.T, where LL is location, B is band, S is source and SS is subsource, the components of an FDSN source identifier channel.  The location may be "*" to match any location, and "?" and "*" may be used as wildcards.  When connected with SeedLink v4, selectors in v3 format are converted automatically, e.g. 00BHZ becomes 00_B_H_Z, and BHZ becomes *_B_H_Z.</p>

<pre>
Some v3 examples:
BH?          - BHZ, BHN, BHE (all record types)
00BH?.D      - BHZ, BHN, BHE with location code '00' (data records)
BH? !E       - BHZ, BHN, BHE (excluding detection records)
BH? E        - BHZ, BHN, BHE &amp; detection records of all channels
!LCQ !LEP    - exclude LCQ and LEP channels
!L !T        - exclude log and timing records

Some v4 examples:
*_B_H_?      - BHZ, BHN, BHE, any location
00_B_H_Z.D   - BHZ with location code '00' (data records)
</pre>

## <a id='stream-list-file'>Stream list file</a>

<p>The stream list file used with the '-l' option is expected to define a data stream on each line.  The format of each line is:</p>

<pre>
StationID [selectors]
</pre>

<p><i>StationID</i> is in NET_STA format.  The selectors are optional.  If default selectors are also specified (with the '-s' option), they will be used when no selectors are specified for a given stream.  An example file follows:</p>

<pre>
----  Begin example file -----
# Comment lines begin with a '#'
# Example stream list file for use with the -l argument of slinktool or
# with the sl_add_streamlist_file() libslink function.
GE_ISP  BH?.D
NL_HGN
MN_AQU  BH? HH?
----  End example file -----
</pre>

## <a id='environment'>Environment</a>

<dl>
<dt><b>SEEDLINK_USERNAME</b></dt>
<dt><b>SEEDLINK_PASSWORD</b></dt>
<dd>
<p>If both are set, they are used for SeedLink v4 USERPASS authentication. This is an alternative to prompting with <b>-Ap</b>.  The server must support authentication.</p>

</dd>
<dt><b>LIBSLINK_CA_CERT_FILE</b></dt>
<dd>
<p>Location of a CA certificate file to load for TLS server verification.</p>

</dd>
<dt><b>LIBSLINK_CA_CERT_PATH</b></dt>
<dd>
<p>Path to a directory of CA certificate files to load for TLS server verification.</p>

</dd>
<dt><b>LIBSLINK_TLS_DEBUG</b></dt>
<dd>
<p>TLS debugging output level (integer), increase for more detail.</p>

</dd>
<dt><b>LIBSLINK_CERT_UNVERIFIED_OK</b></dt>
<dd>
<p>If set, allow unverified TLS certificates (TLS 1.2 connections only).</p>

</dd>
</dl>

## <a id='notes'>Notes</a>

<p>Diagnostic and error messages from slinktool are printed to standard error (stderr).  Packet details (the -p flag), unpacked samples (the -u flag), the per-packet line reported with -v, and responses to INFO requests are printed to standard output (stdout).  When the dumpfile is '-' (<b>-o -</b>) all of this output is printed to standard error so that standard output contains only data records.</p>

## <a id='author'>Author</a>

<pre>
Chad Trabant
previously ORFEUS Data Center/EC-Project MEREDIAN
previously IRIS Data Management Center
EarthScope Data Services
</pre>

(man page 2026/11/14)
