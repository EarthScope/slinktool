
# Standard compiler parameters
CFLAGS  = -O -I../libslink \
	  -I/usr/include/libxml2 -I/usr/local/include/libxml2

# GCC specific parameters
GCC = gcc
GCCFLAGS = -O2 -Wall -I../libslink \
           -I/usr/include/libxml2 -I/usr/local/include/libxml2

LDFLAGS = -L../libslink -L/usr/local/lib
LDLIBS  = -lslink -lxml2

# For SunOS/Solaris uncomment the following line
#LDLIBS = -lslink -lxml2 -lsocket -lnsl -lrt

BIN  = ../slinktool

OBJS = dsarchive.o archive.o slinkxml.o slinktool.o

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $(BIN) $(OBJS) $(LDFLAGS) $(LDLIBS)

static: $(OBJS)
	$(CC) $(CFLAGS) -static -o $(BIN) $(OBJS) $(LDFLAGS) $(LDLIBS)

cc:
	@$(MAKE) "CC=$(CC)" "CFLAGS=$(CFLAGS)"

gcc:
	@$(MAKE) "CC=$(GCC)" "CFLAGS=$(GCCFLAGS)"

debug:
	$(MAKE) "CC=$(CC)" "CFLAGS=-g $(CFLAGS)"

gccdebug:
	$(MAKE) "CC=$(GCC)" "CFLAGS=-g $(GCCFLAGS)"

clean:
	rm -f $(OBJS) $(BIN)

install:
	@echo
	@echo "No install method, copy the binary and man page yourself"
	@echo

