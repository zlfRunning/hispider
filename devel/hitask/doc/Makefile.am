EXTRA_DIST = hitaskd.ini hitask.ini base64.php rc.hitaskd rc.hitask

install: install-ini

conftodir = "`echo $(sysconfdir)|sed -e 's|/usr/etc|/etc|'`"

sysconftodir = $(conftodir)

install-ini:
	@echo install hitask.ini hitaskd to $(sysconftodir)
	@test -d $(sysconftodir)      || mkdir -p $(sysconftodir)
	@test -d $(sysconftodir)/init 	|| mkdir -p $(sysconftodir)/init
	@test -f $(sysconftodir)/hitask.ini \
		|| cp -p $(top_srcdir)/doc/hitask.ini $(sysconftodir)/hitask.ini
	@test -f $(sysconftodir)/hitaskd.ini \
		|| cp -p $(top_srcdir)/doc/hitaskd.ini $(sysconftodir)/hitaskd.ini
#	@test -f $(sysconftodir)/init/hitaskd \
#		|| (cp -p $(top_srcdir)/doc/rc.hitaskd $(sysconftodir)/init/hitaskd \
#			&& perl -i -p -e "s|usr/local|$(prefixdir)|g" $(sysconftodir)/init/hitaskd)
