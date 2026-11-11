# the license of the Unicode data that the names are made from goes with
# them
pkgdata_DATA = powwow.md powwow.help unicode-license.txt
if HAVE_ZLIB
# powwow reads them through zlib
pkgdata_DATA += unicode-names.txt.gz emoji-names.txt.gz
CLEANFILES = unicode-names.txt.gz emoji-names.txt.gz
else
pkgdata_DATA += unicode-names.txt emoji-names.txt
endif
EXTRA_DIST = README.follow README.modules \
		Hacking Config.demo \
		powwow.md powwow.help unicode-names.txt unicode-names.sh \
		emoji-names.txt emoji-names.sh update-unicode.sh \
                unicode-license.txt


unicode-names.txt.gz: unicode-names.txt
	gzip -9cn $(srcdir)/unicode-names.txt > $@

emoji-names.txt.gz: emoji-names.txt
	gzip -9cn $(srcdir)/emoji-names.txt > $@

# download the latest Unicode data, and make the files of names again
update-unicode:
	sh $(srcdir)/update-unicode.sh

.PHONY: update-unicode
