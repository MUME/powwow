SUBDIRS=src doc man

pkgdata_DATA = COPYING

EXTRA_DIST = .clang-tidy

# check the markup of the man page (needs mandoc)
lint:
	cd man && $(MAKE) $(AM_MAKEFLAGS) lint

# check the sources (needs clang-tidy)
tidy:
	cd src && $(MAKE) $(AM_MAKEFLAGS) tidy

help:
	@echo 'Usage: make [TARGET]'
	@echo ''
	@echo 'Running the tests:'
	@echo '  make check                 run all the tests'
	@echo '  make check TESTS=test_ptr  run a single test'
	@echo '  The results are in src/test-suite.log and src/<test>.log.'
	@echo ''
	@echo 'Available targets:'
	@echo '  all               build everything (the default)'
	@echo '  check             build and run the tests'
	@echo '  lint              check the markup of the man page (needs mandoc)'
	@echo '  tidy              check the sources (needs clang-tidy)'
	@echo '  install           install the programs and data files'
	@echo '  install-strip     like install, but strip the programs'
	@echo '  uninstall         remove the installed files'
	@echo '  clean             remove the files built by make'
	@echo '  distclean         also remove the files created by configure'
	@echo '  maintainer-clean  also remove files that need special tools to rebuild'
	@echo '  dist              create a source tarball'
	@echo '  distcheck         create a source tarball and check that it works'
	@echo '  help              show this list'

.PHONY: lint tidy help
