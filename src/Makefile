
# Build environment can be configured the following
# environment variables:
#   CC : Specify the C compiler to use
#   CFLAGS : Specify compiler options to use

# Required compiler parameters
CFLAGS += -I../libslink -I../ezxml

LDFLAGS = -L../libslink -L../ezxml
LDLIBS  = -lslink -lezxml

# For Windows w/ Unix-like build environments uncomment the following line
# This is needed for MinGW but not for Cygwin
#LDLIBS = -lslink -lezxml -lws2_32

# For SunOS/Solaris uncomment the following line
#LDLIBS = -lslink -lezxml -lsocket -lnsl -lrt

BIN  = ../slinktool

SRCS = dsarchive.c archive.c slinkxml.c slinktool.c
OBJS = $(SRCS:.c=.o)

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) -o $(BIN) $(OBJS) $(LDFLAGS) $(LDLIBS)

# Standard object building
.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(BIN)

install:
	@echo
	@echo "No install method, copy the binary and man page yourself"
	@echo
