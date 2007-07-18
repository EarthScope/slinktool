
# Use 'wmake -f Makefile.wat'

.BEFORE
	@set INCLUDE=.;$(%watcom)\H;$(%watcom)\H\NT
	@set LIB=.;$(%watcom)\LIB386

cc     = wcc386
cflags = -zq
lflags = OPT quiet OPT map LIBRARY ..\libslink\libslink.lib LIBRARY ..\ezxml\libezxml.lib LIBRARY ws2_32.lib
cvars  = $+$(cvars)$- -DWIN32

BIN = ..\slinktool.exe

INCS = -I..\libslink -I..\ezxml

all: $(BIN)

$(BIN):	archive.obj dsarchive.obj slinkxml.obj slinktool.obj
	wlink $(lflags) name $(BIN) file {archive.obj dsarchive.obj slinkxml.obj slinktool.obj}

# Source dependencies:
archive.obj:	archive.c archive.h
dsarchive.obj:	dsarchive.c dsarchive.h archive.h
slinktxml.obj:	slinkxml.c slinkxml.h
slinktool.obj:	slinktool.c dsarchive.h slinkxml.h

# How to compile sources:
.c.obj:
	$(cc) $(cflags) $(cvars) $(INCS) $[@ -fo=$@

# Clean-up directives:
clean:	.SYMBOLIC
	del *.obj *.map $(BIN)

