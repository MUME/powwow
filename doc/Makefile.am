pkgdata_DATA = powwow.md powwow.help unicode-names.txt emoji-names.txt
EXTRA_DIST = README.follow README.modules \
		Hacking Config.demo \
		powwow.md powwow.help unicode-names.txt unicode-names.sh \
		emoji-names.txt emoji-names.sh update-unicode.sh


# download the latest Unicode data, and make the files of names again
update-unicode:
	sh $(srcdir)/update-unicode.sh

.PHONY: update-unicode
