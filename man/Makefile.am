man_MANS = powwow.6

CLEANFILES = powwow.6

# check the markup of the man page; override MANDOC to use another
# mandoc binary
MANDOC = mandoc

lint: $(man_MANS)
	$(MANDOC) -T lint $(man_MANS)

.PHONY: lint
