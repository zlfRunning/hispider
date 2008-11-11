EXTRA_DIST = rc.hispiderd.ini rc.hispider.ini

install: install-ini

install-ini:
	echo install hispider.ini hispiderd to $(sysconfdir)
	test -d $(sysconfdir)      || mkdir -p $(sysconfdir)
	test -f $(sysconfdir)/hispider.ini \
		|| cp -p $(top_srcdir)/doc/rc.hispider.ini $(sysconfdir)/hispider.ini
	test -f $(sysconfdir)/hispiderd.ini \
		|| cp -p $(top_srcdir)/doc/rc.hispiderd.ini $(sysconfdir)/hispiderd.ini

