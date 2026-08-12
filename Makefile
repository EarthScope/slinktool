# Build environment can be configured the following
# environment variables:
#   CC : Specify the C compiler to use
#   CFLAGS : Specify compiler options to use

# Add recommended compiler optimization level
CFLAGS += -O2

# Export for sub-makes
export CFLAGS

.PHONY: all clean
all clean: libslink libmseed ezxml
	$(MAKE) -C src $@

.PHONY: libslink
libslink:
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: libmseed
libmseed:
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: ezxml
ezxml:
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: install
install:
	@echo
	@echo "No install method"
	@echo "Copy the binary and documentation to desired location"
	@echo
