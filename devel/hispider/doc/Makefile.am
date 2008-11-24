EXTRA_DIST = rc.hispiderd.ini rc.hispider.ini

install: install-ini

conftodir = "`echo $(sysconfdir)|sed -e 's|/usr/etc|/etc|'`"

sysconftodir = $(conftodir)

install-ini:
	@echo install hispider.ini hispiderd to $(sysconftodir)
	@test -d $(sysconftodir)      || mkdir -p $(sysconftodir)
	@test -f $(sysconftodir)/hispider.ini \
		|| cp -p $(top_srcdir)/doc/rc.hispider.ini $(sysconftodir)/hispider.ini
	@test -f $(sysconftodir)/hispiderd.ini \
		|| cp -p $(top_srcdir)/doc/rc.hispiderd.ini $(sysconftodir)/hispiderd.ini

