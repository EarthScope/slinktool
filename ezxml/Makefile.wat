
# Use 'wmake -f Makefile.wat'

.BEFORE
	@set INCLUDE=.;$(%watcom)\H;$(%watcom)\H\NT
	@set LIB=.;$(%watcom)\LIB386

cc     = wcc386
cflags = -zq
lflags = OPT quiet OPT map
cvars  = $+$(cvars)$- -DWIN32 -DEZXML_NOMMAP

# To build a DLL uncomment the following two lines
#cflags = -zq -bd
#lflags = OPT quiet OPT map SYS nt_dll

LIB = libezxml.lib
DLL = libezxml.dll

INCS = -I.

OBJS = ezxml.obj

all: lib

lib:	$(OBJS) .SYMBOLIC
	wlib -b -n -c -q $(LIB) +$(OBJS)

dll:	$(OBJS) .SYMBOLIC
	wlink $(lflags) name libmseed file {$(OBJS)}

# Source dependencies:
ezxml.obj:	ezxml.c ezxml.h

# How to compile sources:
.c.obj:
	$(cc) $(cflags) $(cvars) $(INCS) $[@ -fo=$@

# Clean-up directives:
clean:	.SYMBOLIC
	del *.obj *.map
	del $(LIB) $(DLL)
