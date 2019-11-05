man_MANS = powwow.6
EXTRA_DIST = powwow.6.utf-8

CLEANFILES = powwow.6

powwow.6: powwow.6.utf-8
if MAN_PAGE_ENCODING_IS_UTF_8
	cp $< $@
else
	iconv -f UTF-8 -t "$(MAN_PAGE_ENCODING)" < $< > $@
endif

