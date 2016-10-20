# <p >slinktool 
###  SeedLink client for data stream inspection, data collection and server testing</p>

1. [Name](#)
1. [Synopsis](#synopsis)
1. [Description](#description)
1. [Options](#options)
1. [Examples](#examples)
1. [Seedlink Selectors](#seedlink-selectors)
1. [Archiving Data](#archiving-data)
1. [Stream List File](#stream-list-file)
1. [Notes](#notes)
1. [Author](#author)

## <a id='synopsis'>Synopsis</a>

<pre >
slinktool [options] [host][:][port]
</pre>

## <a id='description'>Description</a>

<p ><b>slinktool</b> connects to a <u>SeedLink</u> server and queries the server for informaion or requests data using uni-station or multi-station mode and prints information about the packets received. All received packets can optionally be dumped to a single file or saved in custom directory and file layouts.</p>

## <a id='options'>Options</a>

<b>-V</b>

<p style="padding-left: 30px;">Report program version and exit.</p>

<b>-h</b>

<p style="padding-left: 30px;">Print program usage and exit.</p>

<b>-v</b>

<p style="padding-left: 30px;">Be more verbose.  This flag can be used multiple times ("-v -v" or "-vv") for more verbosity.  One flag: report basic handshaking (link configuration) details and briefly report each packet received.  Two flags: report the details of the handshaking, each packet received and detailed connection diagnostics.</p>

<b>-P</b>

<p style="padding-left: 30px;">Ping the server: connect, print out the server ID and exit.  If the server was successfully contacted the return code will be 0, if errors were encountered the return code will be 1.</p>

<b>-p</b>

<p style="padding-left: 30px;">Print details of received Mini-SEED data records. This flag can be used multiple times ("-p -p" or "-pp") for more detail.  One flag: a single summary line for each data packet received.  Two flags: details of the Mini-SEED data records received, including information from fixed header and 100/1000/1001 blockettes.</p>

<b>-u</b>

<p style="padding-left: 30px;">Print data samples in data packets, implies at least one -p flag.</p>

<b>-nd </b><u>delay</u>

<p style="padding-left: 30px;">The network reconnect delay (in seconds) for the connection to the SeedLink server.  If the connection breaks for any reason this will govern how soon a reconnection should be attempted. The default value is 30 seconds.</p>

<b>-nt </b><u>timeout</u>

<p style="padding-left: 30px;">The network timeout (in seconds) for the connection to the SeedLink server.  If no data [or keep alive packets?] are received in this time the connection is closed and re-established (after the reconnect delay has expired).  The default value is 600 seconds. A value of 0 disables the timeout.</p>

<b>-k </b><u>keepalive</u>  (requires SeedLink >= 3)

<p style="padding-left: 30px;">Keepalive packet interval (in seconds) at which keepalive (heartbeat) packets are sent to the server.  Keepalive packets are only sent if nothing is received within the interval.</p>

<b>-x </b><u>statefile</u>[:<u>interval</u>]

<p style="padding-left: 30px;">During client shutdown the last received sequence numbers and time stamps (start times) for each data stream will be saved in this file. If this file exists upon startup the information will be used to resume the data streams from the point at which they were stopped.  In this way the client can be stopped and started without data loss, assuming the data are still available on the server.  If <u>interval</u> is specified the state will be saved every <u>interval</u> packets that are received.  Otherwise the state will be saved only on normal program termination.</p>

<b>-d</b>

<p style="padding-left: 30px;">Configure the connection in "dial-up" mode.  The remote server will close the connection when it has sent all of the data in it's buffers for the selected data streams.  This is opposed to the normal behavior of waiting indefinately for data.</p>

<b>-b</b>

<p style="padding-left: 30px;">Configure the connection in "batch" mode.  Negotiation with the remote server is made faster by minimizing acknowledgement checks.</p>

<b>-o </b><u>dumpfile</u>

<p style="padding-left: 30px;">If specified, all packets (Mini-SEED records) received will be appended to this file.  The file is created if it does not exist.  A special mode for this option is to send all received packets to standard output when the dumpfile is specified as '-'.  In this case all output besides these records will be redirected to standard error.</p>

<b>-A </b><u>format</u>

<p style="padding-left: 30px;">If specified, all packets (Mini-SEED records) received will be appended to a directory/file structure defined by <b>format</b>. All directories implied in the <b>format</b> string will be created if necessary.  See the section <u>Archiving data</u>.</p>

<b>-SDS </b><u>SDSdir</u>

<p style="padding-left: 30px;">If specified, all packets (Mini-SEED records) received will be saved into a Simple Data Structure (SDS) dir/file structure starting at the specified directory.  This directory and all subdirectories will be created if necessary.  This option is esentially a preset version of '-A' option.  The SDS dir/file structure is:</p>
<pre style="padding-left: 30px;">
SDSdir/YEAR/NET/STA/CHAN.TYPE/NET.STA.LOC.CHAN.TYPE.YEAR.DAY
</pre>

<b>-BUD </b><u>BUDdir</u>

<p style="padding-left: 30px;">If specified, all waveform data packets (Mini-SEED data records) received will be saved into a Buffer of Uniform Data (BUD) dir/file structure starting at the specified directory.  This directory and all subdirectories will be created if necessary.  This option is esentially a preset version of '-A' option.  The BUD dir/file structure is:</p>
<pre style="padding-left: 30px;">
BUDdir/NET/STA/STA.NET.LOC.CHAN.YEAR.DAY
</pre>

<b>-s </b><u>selectors</u>

<p style="padding-left: 30px;">This defines default selectors.  If no multi-station data streams are configured these selectors will be used for uni-station mode. Otherwise these selectors will be used when no selectors are specified for a given stream using the '-S' or '-l' options.</p>

<b>-l </b><u>streamfile</u>

<p style="padding-left: 30px;">A list of streams will be read from the given file.  This option implies multi-station mode.  The format of the stream list file is given below in the section <u>Stream list file</u>.</p>

<b>-S </b><u>stream[:selectors],...</u>  (requires SeedLink >= 2.5)

<p style="padding-left: 30px;">A list of streams is given as an argument.  This option implies multi-station mode.  The stream list is composed of multiple streams (stations) and optional selectors.  <u>stream</u> should be provided in NET\_STA format and <u>selectors</u> are normal SeedLink selectors, see examples and notes below.  If no selectors are provided for a given stream, the default selectors, if defined, will be used.</p>

<b>-tw </b><u>start:[end]</u>  (requires SeedLink >= 3)

<p style="padding-left: 30px;">Specifies a time window applied, by the server, to data streams. The format for both times is year,month,day,hour,min,sec; for example: "2002,08,05,14,00:2002,08,05,14,15,00". The end time is optional but the colon must be present.  If no end time is specified the server will send data indefinately.  This option will override any saved state information.</p>

<p style="padding-left: 30px;">Warning: time windowing might be disabled on the remote server.</p>

<b>-i </b><u>level</u>  (requires SeedLink >= 3)

<p style="padding-left: 30px;">Send an information request (INFO); the returned raw XML response is displayed.  Possible levels are: ID, CAPABILITIES, STATIONS, STREAMS, GAPS, CONNECTIONS, ALL.</p>

<p style="padding-left: 30px;">Formatted INFO shortcuts (formats the XML for readability):</p>

<pre style="padding-left: 30px;">
-I   print server id/version and exit
-L   print station list and exit
-Q   print stream list and exit
-G   print gap list and exit
-C   print connection list and exit
</pre>

<p >Warning: informational (INFO) messages might be disabled on the server.</p>

<b>[host][:][port]</b>

<p style="padding-left: 30px;">A required argument, specifies the address of the SeedLink server in host:port format.  Either the host, port or both can be omitted.  If host is omitted then localhost is assumed, i.e. ':18000' implies 'localhost:18000'.  If the port is omitted then 18000 is assumed, i.e. 'localhost' implies 'localhost:18000'.  If only ':' is specified 'localhost:18000' is assumed.</p>

## <a id='examples'>Examples</a>

<b>All-station/Uni-station mode example:</b>

<p style="padding-left: 30px;">The following would connect to a SeedLink server at slink.host.com port 18000 and configure the link in all-station/uni-station mode, exactly which data are received depends on the data being served by the SeedLink server on that particular port.  Additionally, all of the received packets are appended to the file 'data.mseed' and each packet received is reported on the standard output.</p>

<p style="padding-left: 30px;"><b>> slinktool -v -o data.mseed slink.host.com:18000</b></p>

<p style="padding-left: 30px;">The '-s' argument could be used to indicate selectors to limit the type of packets sent by the SeedLink server (without selectors all packet types are sent).  The following would limit this connection to BHZ channel waveform data with a location code of 10 (see an explanation of SeedLink selectors below).  Additionally another verbose flag is given, causing slinktool to report detailed header information from data records.</p>

<p style="padding-left: 30px;"><b>> slinktool -vv -s 10BHZ.D -o data.mseed slink.host.com:18000</b></p>

<b>Multi-station mode example:</b>

<p style="padding-left: 30px;">The following example would connect to a SeedLink server on localhost port 18010 and configure the link in multi-station mode.  Each station specified with the '-S' argument will be requested, optionally specifying selectors for each station.</p>

<p style="padding-left: 30px;"><b>> slinktool -v -S GE\_WLF,MN\_AQU:00???,IU\_KONO:BHZ.D :18010</b></p>

<p style="padding-left: 30px;">This would request GEOFON station WLF (all data as no selectors were indicated), MedNet station AQU with location code 00 (all channels) and IU network station KONO (only waveform data) from channel BHZ.</p>

<p style="padding-left: 30px;">Of course, a variety of different data selections can be made:</p>

<p style="padding-left: 30px;"><b>-s 'BHE.D BHN.D' -S 'GE\_STU,GE\_MALT,GE\_WLF'</b>   (horizontal BH channels, data only)</p>

<p style="padding-left: 30px;"><b>-s BHZ -S GE\_STU,GE\_WLF,GE\_RUE,GE\_EIL</b>   (vertical channels only)</p>

<b>Wildcarding network and station codes</b>

<p style="padding-left: 30px;">Some SeedLink implementation support wildcarding of the network and station codes, when this is the case the only two wildcard characters recognized are '\*' for one or more characters and '?' for any single character.</p>

<p style="padding-left: 30px;">As an example, all US network data can be requested using the following syntax:</p>

<p style="padding-left: 30px;"><b>-S 'US\_\*'</b></p>

## <a id='seedlink-selectors'>Seedlink Selectors</a>

<p >SeedLink selectors are used to request specific types of data within a given data stream, in effect limiting the default action of sending all data types.  A data packet is sent to the client if it matches any positive selector (without leading "!") and doesn't match any negative selectors (with a leading "!").  The general format of selectors is LLSSS.T, where LL is location, SSS is channel and T is type (one of [DECOTL] for Data, Event, Calibration, Blockette, Timing, and Log records).  "LL", ".T", and "LLSSS." can be omitted, implying anything in that field.  It is also possible to use "?" in place of L and S as a single character wildcard.  Multiple selectors are separated by space(s).</p>

<pre >
Some examples:
BH?          - BHZ, BHN, BHE (all record types)
00BH?.D      - BHZ, BHN, BHE with location code '00' (data records)
BH? !E       - BHZ, BHN, BHE (excluding detection records)
BH? E        - BHZ, BHN, BHE & detection records of all channels
!LCQ !LEP    - exclude LCQ and LEP channels
!L !T        - exclude log and timing records
</pre>

## <a id='archiving-data'>Archiving Data</a>

<p >Using the '-A <b>format</b>' option received data can be saved in a custom directory and file structure.  The archive <b>format</b> argument is expanded for each packet processed using the following flags:</p>

<pre >
  <b>n</b> : network code, white space removed
  <b>s</b> : station code, white space removed
  <b>l</b> : location code, white space removed
  <b>c</b> : channel code, white space removed
  <b>Y</b> : year, 4 digits
  <b>y</b> : year, 2 digits zero padded
  <b>j</b> : day of year, 3 digits zero padded
  <b>H</b> : hour, 2 digits zero padded
  <b>M</b> : minute, 2 digits zero padded
  <b>S</b> : second, 2 digits zero padded
  <b>F</b> : fractional seconds, 4 digits zero padded
  <b>%</b> : the percent (%) character
  <b>#</b> : the number (#) character
  <b>t</b> : single character type code:
         D - waveform data packet
         E - detection packet
         C - calibration packet
         T - timing packet
         L - log packet
         O - opaque data packet
         U - unknown/general packet
         I - INFO packet
         ? - unidentifiable packet
</pre>

<p >The flags are prefaced with either the <b>%</b> or <b>#</b> modifier. The <b>%</b> modifier indicates a defining flag while the <b>#</b> indicates a non-defining flag.  All received packets with the same set of defining flags will be saved to the same file. Non-defining flags will be expanded using the values in the first packet received for the resulting file name.</p>

<p >Time flags are based on the start time of the given packet.</p>

<p >For example, the format string:</p>

<p ><b>/archive/%n/%s/%n.%s.%l.%c.%Y.%j</b></p>

<p >would be expanded to day length files named something like:</p>

<p ><b>/archive/NL/HGN/NL.HGN..BHE.2003.055</b></p>

<p >Using non-defining flags the format string:</p>

<p ><b>/data/%n.%s.%Y.%j.%H:#M:#S.miniseed</b></p>

<p >would be expanded to:</p>

<p ><b>/data/NL.HGN.2003.044.14:17:54.miniseed</b></p>

<p >resulting in hour length files because the minute and second are specified with the non-defining modifier.  The minute and second fields are from the first packet in the file.</p>

## <a id='stream-list-file'>Stream List File</a>

<p >The stream list file used with the '-l' option is expected to define a data stream on each line.  The format of each line is:</p>

<pre >
Network Station [selectors]
</pre>

<p >The selectors are optional.  If default selectors are also specified (with the '-s' option), they they will be used when no selectors are specified for a given stream.  An example file follows:</p>

<pre >
----  Begin example file -----
# Comment lines begin with a '#' or '\*'
# Example stream list file for use with the -l argument of slclient or
# with the sl\_read\_streamlist() libslink function.
GE ISP  BH?.D
NL HGN
MN AQU  BH? HH?
----  End example file -----
</pre>

## <a id='notes'>Notes</a>

<p >All diagnostic output from slinktool is printed to standard error (stderr), exceptions are when printing Mini-SEED packet details (the -p flag), when printing unpacked samples (the -u flag) and when printing the raw or formatted responses to INFO requests.</p>

## <a id='author'>Author</a>

<pre >
Chad Trabant
ORFEUS Data Center/EC-Project MEREDIAN
IRIS Data Management Center
</pre>


(man page 2016/10/19)
