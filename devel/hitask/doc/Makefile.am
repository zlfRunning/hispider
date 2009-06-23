EXTRA_DIST = hitaskd.ini hitask.ini

install: install-ini

conftodir = "`echo $(sysconfdir)|sed -e 's|/usr/etc|/etc|'`"

sysconftodir = $(conftodir)

install-ini:
	@echo install hitask.ini hitaskd to $(sysconftodir)
	@test -d $(sysconftodir)      || mkdir -p $(sysconftodir)
	@test -f $(sysconftodir)/hitask.ini \
		|| cp -p $(top_srcdir)/doc/hitask.ini $(sysconftodir)/hitask.ini
	@test -f $(sysconftodir)/hitaskd.ini \
		|| cp -p $(top_srcdir)/doc/hitaskd.ini $(sysconftodir)/hitaskd.ini

