SUBDIRS=src doc man

pkgdata_DATA = COPYING

EXTRA_DIST = .clang-tidy AGENTS.md cygwin/bundle.sh cygwin/installer.sh \
             cygwin/make-icon.sh cygwin/powwow.ico cygwin/powwow.iss \
             cygwin/more cygwin/powwow-cmd cygwin/powwow-notepad \
             cygwin/powwow-start cygwin/update-licenses.sh

# check the markup of the man page (needs mandoc)
lint:
	cd man && $(MAKE) $(AM_MAKEFLAGS) lint

# check the sources (needs clang-tidy)
tidy:
	cd src && $(MAKE) $(AM_MAKEFLAGS) tidy

# on Cygwin: a tree of powwow with what it needs from Cygwin, and an
# installer of it for Windows (needs Inno Setup); see README
cygwin-bundle: all
	rm -rf powwow-win
	$(srcdir)/cygwin/bundle.sh powwow-win

cygwin-installer: cygwin-bundle
	$(srcdir)/cygwin/installer.sh powwow-win $(VERSION)

# download the Cygwin source packages of the bundle again, and update
# their license files in cygwin-licenses
update-cygwin-licenses:
	$(srcdir)/cygwin/update-licenses.sh

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
if CYGWIN
	@echo '  cygwin-bundle     build powwow-win: powwow with what it needs'
	@echo '  cygwin-installer  also make a Windows installer of it'
	@echo '  update-cygwin-licenses'
	@echo '                    update cygwin-licenses from the Cygwin sources'
endif
	@echo '  install           install the programs and data files'
	@echo '  install-strip     like install, but strip the programs'
	@echo '  uninstall         remove the installed files'
	@echo '  clean             remove the files built by make'
	@echo '  distclean         also remove the files created by configure'
	@echo '  help              show this list'

.PHONY: lint tidy cygwin-bundle cygwin-installer update-cygwin-licenses \
        help
