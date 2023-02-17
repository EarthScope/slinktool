# libslink: the SeedLink client library

This package contains the source code, documentation and example code
for libslink, the SeedLink client library.  For further information
regarding the library interface and usage see the documentation in the
'doc' directory, including a Users Guide and man pages.

The library should work in Linux, BSD (and derivatives like macOS),
Solaris and MS-Windows environments.

## Building and Installing

In most Unix/Linux environments a simple 'make' will build the program.

The CC and CFLAGS environment variables can be used to configure
the build parameters.

## Extras

The 'example' directory includes an example SeedLink client that uses
libslink.

The 'doc' directory includes all associated documentation including
a Users Guide and man pages for library functions.

## Threading

The library is thread-safe under Unix-like environments with the
condition that each connection descriptor (SLCD) is handled by a
single thread.  Thread independent logging schemes are possible.
Under WIN32 the library is not thread-safe.

## Licensing

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0)

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Copyright (C) 2022 Chad Trabant, EarthScope Data Services

## Acknowlegements --

Numerous improvements have been incorporated based on feedback and
patches submitted by others.  Individual acknowlegements are included
in the ChangeLog.  Thank you!

Initial development at the ORFEUS Data Center/EC MEREDIAN Project
Continuing maintenance at the IRIS Data Management Center, now
EarthScope Data Services.
