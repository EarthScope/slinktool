
# Build environment can be configured the following
# environment variables:
#   CC : Specify the C compiler to use
#   CFLAGS : Specify compiler options to use

# Options specific for GCC
GCC = gcc
GCCFLAGS = -O2 -Wall -fPIC

LIB_OBJS = ezxml.o

LIB_A = libezxml.a

all: $(LIB_A)

$(LIB_A): $(LIB_OBJS)
	rm -f $(LIB_A)
	ar -csq $(LIB_A) $(LIB_OBJS)

clean:
	rm -f $(LIB_OBJS) $(LIB_A)

cc:
	@$(MAKE) "CC=$(CC)" "CFLAGS=$(CFLAGS)"

gcc:
	@$(MAKE) "CC=$(GCC)" "CFLAGS=$(GCCFLAGS)"

gcc32:
	@$(MAKE) "CC=$(GCC)" "CFLAGS=-m32 $(GCCFLAGS)"

gcc64:
	@$(MAKE) "CC=$(GCC)" "CFLAGS=-m64 $(GCCFLAGS)"

debug:
	$(MAKE) "CC=$(CC)" "CFLAGS=-g $(CFLAGS)"

gccdebug:
	$(MAKE) "CC=$(GCC)" "CFLAGS=-g $(GCCFLAGS)"

gcc32debug:
	$(MAKE) "CC=$(GCC)" "CFLAGS=-g -m32 $(GCCFLAGS)"

gcc64debug:
	$(MAKE) "CC=$(GCC)" "CFLAGS=-g -m64 $(GCCFLAGS)"

gcc32gprof:
	$(MAKE) "CC=$(GCC)" "CFLAGS=-pg -m32 $(GCCFLAGS)"

gcc64gprof:
	$(MAKE) "CC=$(GCC)" "CFLAGS=-pg -m64 $(GCCFLAGS)"

# Implicit rule for building object files
%.o: %.c
	$(CC) $(CFLAGS) -c $<

install:
	@echo
	@echo "No install target, copy the library and header yourself"
	@echo
