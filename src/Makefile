
# Standard compiler parameters
CFLAGS += -I../libslink -I../ezxml

# GCC specific parameters
GCC = gcc
GCCFLAGS = -O2 -Wall -I../libslink -I../ezxml

LDFLAGS = -L../libslink -L../ezxml
LDLIBS  = -lslink -lezxml

# For SunOS/Solaris uncomment the following line
#LDLIBS = -lslink -lezxml -lsocket -lnsl -lrt

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

