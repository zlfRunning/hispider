EXTRA_DIST = rc.hispider  rc.hispider.ini  setup.sh 

install: install-ini

install-ini:
	echo install hispider.ini to $(sysconfdir)
	test -d $(sysconfdir)      || $(MKINSTALLDIRS) $(sysconfdir)
	cp -p $(top_srcdir)/doc/rc.hispider.ini $(sysconfdir)/hispider.ini
