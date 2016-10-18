
# Build environment can be configured the following
# environment variables:
#   CC : Specify the C compiler to use
#   CFLAGS : Specify compiler options to use

LIB_OBJS = ezxml.o

LIB_A = libezxml.a

all: $(LIB_A)

$(LIB_A): $(LIB_OBJS)
	rm -f $(LIB_A)
	ar -csq $(LIB_A) $(LIB_OBJS)

clean:
	rm -f $(LIB_OBJS) $(LIB_A)

# Implicit rule for building object files
%.o: %.c
	$(CC) $(CFLAGS) -c $<

install:
	@echo
	@echo "No install target, copy the library and header yourself"
	@echo
