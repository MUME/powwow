SUBDIRS=src doc man

pkgdata_DATA = COPYING

EXTRA_DIST = .clang-tidy AGENTS.md

# what autogen.sh makes, so that make maintainer-clean leaves the tree as
# git has it
MAINTAINERCLEANFILES = Makefile.in aclocal.m4 compile config.guess \
                       config.h.in config.h.in~ config.sub configure \
                       configure~ depcomp install-sh missing test-driver

# check the markup of the man page (needs mandoc), and of the Markdown
# with markdownlint-cli2 if configure found it, with its rules in
# .markdownlint-cli2.yaml
EXTRA_DIST += .markdownlint-cli2.yaml
lint:
	cd man && $(MAKE) $(AM_MAKEFLAGS) lint
	@if test -n '$(MARKDOWNLINT)'; then \
	    echo "$(MARKDOWNLINT) AGENTS.md doc/powwow.md"; \
	    '$(MARKDOWNLINT)' --config $(srcdir)/.markdownlint-cli2.yaml \
	        $(srcdir)/AGENTS.md doc/powwow.md; \
	else \
	    echo 'no markdownlint-cli2: the Markdown is not checked'; \
	fi

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
	@echo '  lint              check the markup of the man page and markdown'
	@echo '  tidy              check the sources (needs clang-tidy)'
	@echo '  install           install the programs and data files'
	@echo '  install-strip     like install, but strip the programs'
	@echo '  uninstall         remove the installed files'
	@echo '  clean             remove the files built by make'
	@echo '  distclean         also remove the files created by configure'
	@echo '  maintainer-clean  also remove the files that autogen.sh makes'
	@echo '  help              show this list'

.PHONY: lint tidy help
