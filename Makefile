
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
