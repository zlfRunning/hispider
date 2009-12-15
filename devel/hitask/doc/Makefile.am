EXTRA_DIST = hitaskd.ini hitask.ini base64.php rc.hitaskd rc.hitask

install: install-ini

conftodir = "`echo $(sysconfdir)|sed -e 's|/usr/etc|/etc|'`"

sysconftodir = $(conftodir)

install-ini:
	@echo install hitask.ini hitaskd to $(sysconftodir)
	@test -d $(sysconftodir)      || mkdir -p $(sysconftodir)
	@test -d $(sysconftodir)/init.d || mkdir -p $(sysconftodir)/init.d
	@test -f $(sysconftodir)/hitask.ini \
		|| cp -p $(top_srcdir)/doc/hitask.ini $(sysconftodir)/hitask.ini
	@test -f $(sysconftodir)/hitaskd.ini \
		|| cp -p $(top_srcdir)/doc/hitaskd.ini $(sysconftodir)/hitaskd.ini
	@test -f $(sysconftodir)/init.d/hitaskd \
		|| cp -p $(top_srcdir)/doc/rc.hitaskd $(sysconftodir)/init.d/hitaskd
	@test -f $(sysconftodir)/init.d/hitask \
		|| cp -p $(top_srcdir)/doc/rc.hitask $(sysconftodir)/init.d/hitask
	@perl -i -p -e "s@/usr/local/bin@$(prefix)/bin@g" \
		$(sysconftodir)/init.d/hitaskd $(sysconftodir)/init.d/hitask
	@perl -i -p -e "s@/usr/local/etc@$(sysconftodir)@g" \
		$(sysconftodir)/init.d/hitaskd  $(sysconftodir)/init.d/hitask
