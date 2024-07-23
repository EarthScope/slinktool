
# slinktool, the all-in-one SeedLink client.

For usage information see the [slinktool manual](doc/slinktool.md)
in the 'doc' directory.

## Building and Installation

In most environments a simple 'make' will compile the program.

SunOS/Solaris:
In order to compile under Solaris the 'src/Makefile' needs to be edited.
See the Makefile for instructions.

Windows:
A Makefile.win is included for building for using with Nmake, i.e.
'nmake -f Makefile.win'.

For further installation simply copy the resulting binary and man page
(in the 'doc' directory) to appropriate system directories.

## Archiving miniSEED

The record archiving function of slinktool invoked with the `-A`, or 
preset `-BUD` and `-SDS`, options is deprecated and may be removed in a
future release.  This program is not recommended for long-term collection
of data streams.  Instead, the dedicated program `slarchive` should
be used:

https://github.com/EarthScope/slarchive

## License

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0)

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Copyright (C) 2023 Chad Trabant, EarthScope Data Services
