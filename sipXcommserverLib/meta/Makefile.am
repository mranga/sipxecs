include $(top_srcdir)/config/subdir.am

schemas = \
	authrules.xsd \
	urlmap.xsd
processed_schemas = \
	registration.xsd

EXTRA_DATA = \
	$(schemas) \
	$(foreach xsd,$(processed_schemas),$(xsd).in)

# Pristine copies of sipX 3.0 configuration files.  When no longer
# supporting upgrade from 3.0, you can delete this file and respective
# installation target
SIPX30_CONFIG = @PACKAGE@-3.0.tgz
PREVIOUS_CONFIG = $(DESTDIR)@SIPX_VARDIR@/upgrade/@PACKAGE@-previous.tgz
EXTRA_DIST = \
	$(EXTRA_DATA) \
	$(SIPX30_CONFIG)

install-data-hook : install-schema $(PREVIOUS_CONFIG)

$(PREVIOUS_CONFIG) : $(srcdir)/$(SIPX30_CONFIG)
	$(INSTALL) -D -m 644 $(srcdir)/$(SIPX30_CONFIG) $(PREVIOUS_CONFIG)

.PHONY: install-schema
install-schema: $(foreach xsd,$(schemas) $(processed_schemas),$(DESTDIR)@SIPX_DATADIR@/schema/$(xsd))

# Unprocessed schemas will be in $(srcdir).
$(foreach xsd,$(schemas),$(DESTDIR)@SIPX_DATADIR@/schema/$(xsd)): $(DESTDIR)@SIPX_DATADIR@/schema/% : $(srcdir)/%
	$(INSTALL) -D -m 644 $< $@

# Processed schemas will be in ".".
$(foreach xsd,$(processed_schemas),$(DESTDIR)@SIPX_DATADIR@/schema/$(xsd)): $(DESTDIR)@SIPX_DATADIR@/schema/% : %
	$(INSTALL) -D -m 644 $< $@

$(foreach xsd,$(processed_schemas),$(xsd)): % : $(srcdir)/%.in
	@echo "Localize $< -> $@"
	@$(LocalizeSipXconfig) $< > $@
