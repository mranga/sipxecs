include $(top_srcdir)/config/subdir.am

bin_SCRIPTS = \
	sipxdialog-config

$(top_builddir)/BUILDSTAMP:
	${MAKE} -C $(top_builddir) BUILDSTAMP

sipxdialog-config: $(top_srcdir)/config/sipX-config.in $(top_builddir)/BUILDSTAMP
	@echo "Generating BuildStamp"; \
	@BuildStamp="$(shell cat $(top_builddir)/BUILDSTAMP)" \
	; ${LocalizeSipXconfig} -e "s/@SIPX_BUILDSTAMP\@/$${BuildStamp}/" \
	    $(top_srcdir)/config/sipX-config.in \
	  > sipxdialog-config
	chmod +x sipxdialog-config

editscripts_IN = \
    sipxdialog.sh.in

editscripts :
	@for f in $(editscripts_IN); \
	do \
	  target=`basename $$f .in`; \
     $(LocalizeSipXconfig) $(srcdir)/$$f > $$target; \
	  $(INSTALL) -D -m 755 $$target $(DESTDIR)$(bindir)/$$target; \
	done

EXTRA_DIST = \
    $(bin_SCRIPTS) \
    $(editscripts_IN)

install-exec-hook : editscripts

.PHONY : editscripts


