
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

Docker:
A Dockerfile is included for building a container with slinktool compiled.
To build:
```
docker build -t slinktool:latest .
```
The entrypoint script will pass though all arguments so usage follows the same syntax as the regular executable:
```
docker run slinktool -v -o data.mseed slink.host.com:18000
```
Note that if you are connecting to seedlink servers running on your local computer, you will need to add `--network="host"` to the docker command in order to use the host name `localhost`.

## Licensing

Copyright (C) 2016 Chad Trabant, IRIS Data Management Center

This library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation; either version 3 of the
License, or (at your option) any later version.

This library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License (GNU-LGPL) for more details.

You should have received a copy of the GNU Lesser General Public
License along with this software.
If not, see <https://www.gnu.org/licenses/>.
