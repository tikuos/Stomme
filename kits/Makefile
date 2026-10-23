# Forward kit targets to the standalone root Makefile.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

.PHONY: all
all:
	@$(MAKE) --no-print-directory -C .. all

Makefile: ;

%:
	@$(MAKE) --no-print-directory -C .. $@
