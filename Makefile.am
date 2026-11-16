SUBDIRS=src doc man

pkgdata_DATA = COPYING

EXTRA_DIST = .clang-tidy AGENTS.md cygwin/bundle.sh cygwin/installer.sh \
             cygwin/launcher.sh icons/make-icon.sh icons/powwow.ico \
             cygwin/powwow.iss \
             cygwin/powwow-cmd \
             cygwin/powwow-start cygwin/update-licenses.sh \
             launcher/CMakeLists.txt launcher/main.cpp \
             launcher/launcher.cpp launcher/launcher.h launcher/Main.qml \
             launcher/MudsPage.qml launcher/SettingsPage.qml \
             launcher/MudDialog.qml launcher/AboutPage.qml \
             launcher/ManualPage.qml launcher/FocusRing.qml \
             launcher/SelectableText.qml launcher/EntryField.qml \
             launcher/PowwowDialog.qml launcher/InfoDialog.qml \
             launcher/DuplicateDialog.qml launcher/ListRow.qml \
             launcher/PowwowToolTip.qml launcher/ShortcutMenuItem.qml \
             launcher/HiliteEditor.qml launcher/ColorButton.qml \
             launcher/macos.h launcher/macos.mm \
             launcher/powwow-launcher.rc launcher/make-icns.sh \
             launcher/make-dmg.sh launcher/qt-licenses.sh \
             launcher/gitversion.cmake

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

# the launcher, built with CMake in launcher/build; CMAKE_FLAGS go to
# CMake, such as -DCMAKE_PREFIX_PATH=DIR if it does not find Qt in DIR.
# On Cygwin, it is built with the MinGW build of Qt for Windows, in
# launcher-win: QT is the folder of that Qt, by default the newest in
# C:\Qt
CMAKE = cmake
CMAKE_FLAGS =
QT =
if CYGWIN
launcher:
	$(srcdir)/cygwin/launcher.sh build '$(QT)'
else
launcher:
	$(CMAKE) -S $(srcdir)/launcher -B launcher/build \
	    -DPOWWOW_BUILD_PROGRAM='$(abs_builddir)/src/powwow' $(CMAKE_FLAGS)
	$(CMAKE) --build launcher/build --parallel
endif

# check the sources of the launcher with clang-tidy, with the checks of
# .clang-tidy, one at a time, as clang-tidy shows its progress through
# several, and the QML files with qmllint
if !CYGWIN
launcher-tidy: launcher
	@for f in main.cpp launcher.cpp; do \
	    echo "$(CLANG_TIDY) launcher/$$f"; \
	    $(CLANG_TIDY) --quiet --config-file=$(srcdir)/.clang-tidy \
	        --header-filter='launcher/[^/]*\.h$$' -p launcher/build \
	        $(CLANG_TIDY_FLAGS) $(srcdir)/launcher/$$f || exit 1; \
	done
	$(CMAKE) --build launcher/build --target all_qmllint
endif

# on Cygwin: a tree of powwow with what it needs from Cygwin and the
# launcher, and an installer of it for Windows (needs Inno Setup); see
# README; bundle.sh with +, as it runs make, which then shares the jobs
# of make -j; V=1, which make passes on from its command line or the
# environment, makes the scripts show their commands as they run them
windows-bundle: all
	+$(srcdir)/cygwin/bundle.sh powwow-win
	$(srcdir)/cygwin/launcher.sh bundle powwow-win '$(QT)'

# the version of an installer or a disk image in $version: a -dev version
# is named after the commit it is built from, as #ver shows it, such as
# 1.3.2-dev-ebae0231
release_version = version='$(VERSION)'; \
	commit=`sed -n 's/^.define GIT_COMMIT "\(.*\)"$$/\1/p' \
	        src/gitversion.h`; \
	case $$version in \
	    *-dev) test -z "$$commit" || version=$$version-$$commit ;; \
	esac

windows-installer: windows-bundle
	@$(release_version); \
	echo "$(srcdir)/cygwin/installer.sh powwow-win $$version"; \
	$(srcdir)/cygwin/installer.sh powwow-win "$$version"

# on macOS: a disk image of the launcher, with powwow in it and the
# libraries of both (needs macdeployqt of Qt); see README; with +, as the
# script runs make, which then shares the jobs of make -j
macos-dmg: all launcher
	+@$(release_version); \
	echo "$(srcdir)/launcher/make-dmg.sh $$version"; \
	$(srcdir)/launcher/make-dmg.sh "$$version"

# download the Cygwin source packages of the bundle again, and update
# their license files in cygwin-licenses
update-cygwin-licenses:
	$(srcdir)/cygwin/update-licenses.sh

# make clean removes the bundle and the installers; only make
# maintainer-clean removes the licenses and the source packages they come
# from, which take a download to get again
clean-local:
	rm -rf powwow-win powwow-*-setup.exe launcher/build launcher-win \
	    powwow-mac powwow-*.dmg

maintainer-clean-local:
	rm -rf cygwin-licenses cygwin-sources qt-licenses

help:
	@echo 'Usage: make [TARGET]'
	@echo ''
	@echo 'Running the tests:'
	@echo '  make check                 run all the tests'
	@echo '  make check TESTS=test_ptr  run a single test'
	@echo '  The results are in src/test-suite.log and src/<test>.log.'
	@echo ''
if CYGWIN
	@echo 'Showing the commands:'
	@echo '  make V=1 [TARGET]  also show the commands that the scripts of the'
	@echo '                     cygwin targets run, as they run them'
	@echo ''
endif
	@echo 'Available targets:'
	@echo '  all               build powwow and its files, but not the launcher'
	@echo '                    (the default)'
	@echo '  check             build and run the tests'
	@echo '  lint              check the markup of the man page and markdown'
	@echo '  tidy              check the sources (needs clang-tidy)'
if CYGWIN
	@echo '  launcher          build the launcher with Qt for MinGW in QT=DIR'
else
	@echo '  launcher          build the launcher with CMake (needs Qt 6.8)'
	@echo '  launcher-tidy     check the sources of the launcher (needs'
	@echo '                    clang-tidy)'
endif
if CYGWIN
	@echo '  windows-bundle    build powwow-win: powwow with what it needs,'
	@echo '                    and the launcher, with Qt in QT=DIR'
	@echo '  windows-installer also make a Windows installer of it'
	@echo '  update-cygwin-licenses'
	@echo '                    update cygwin-licenses from the Cygwin sources'
endif
if MACOS
	@echo '  macos-dmg         make a disk image of the launcher with powwow'
endif
	@echo '  install           install the programs and data files'
	@echo '  install-strip     like install, but strip the programs'
	@echo '  uninstall         remove the installed files'
	@echo '  clean             remove the files built by make'
	@echo '  distclean         also remove the files created by configure'
	@echo '  maintainer-clean  also remove the files that autogen.sh makes'
	@echo '  help              show this list'

.PHONY: lint tidy windows-bundle windows-installer macos-dmg \
	update-cygwin-licenses \
        launcher launcher-tidy help
