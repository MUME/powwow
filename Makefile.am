SUBDIRS=src doc man

pkgdata_DATA = COPYING

# check the markup of the man page (needs mandoc)
lint:
	cd man && $(MAKE) $(AM_MAKEFLAGS) lint

.PHONY: lint
