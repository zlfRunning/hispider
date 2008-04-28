EXTRA_DIST = rc.hispider  rc.hispiderd rc.hispider.ini  rc.hispiderd.ini setup.sh 

install: install-ini

install-ini:
	echo install hispider.ini hispiderd to $(sysconfdir)
	test -d $(sysconfdir)      || $(MKINSTALLDIRS) $(sysconfdir)
	cp -p $(top_srcdir)/doc/rc.hispider.ini $(sysconfdir)/hispider.ini
	cp -p $(top_srcdir)/doc/rc.hispiderd.ini $(sysconfdir)/hispiderd.ini
