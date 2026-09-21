# TazTerm top-level Makefile — dev loop (not packaging).
# Build: make
# Test in place: ./src/tazterm
# Install live: sudo make install  (same file set as receipt genpkg_rules)
# Remove: sudo make uninstall
PREFIX  ?= /usr
DESTDIR ?=

all:
	$(MAKE) -C src
	$(MAKE) -C po

install: all
	$(MAKE) -C src install DESTDIR=$(DESTDIR) PREFIX=$(PREFIX)
	$(MAKE) -C po install DESTDIR=$(DESTDIR) PREFIX=$(PREFIX)
	install -Dm644 data/tazterm.desktop $(DESTDIR)$(PREFIX)/share/applications/tazterm.desktop
	install -Dm644 data/icons/hicolor/scalable/apps/tazterm.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/tazterm.svg
	for s in 22 24 32 48; do \
		install -Dm644 data/icons/hicolor/$${s}x$${s}/apps/tazterm.png \
			$(DESTDIR)$(PREFIX)/share/icons/hicolor/$${s}x$${s}/apps/tazterm.png; \
	done
	gtk-update-icon-cache -q -t -f $(DESTDIR)$(PREFIX)/share/icons/hicolor 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tazterm
	rm -f $(DESTDIR)$(PREFIX)/share/applications/tazterm.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/tazterm.svg
	for s in 22 24 32 48; do \
		rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/$${s}x$${s}/apps/tazterm.png; \
	done
	for l in `cat po/LINGUAS`; do \
		rm -f $(DESTDIR)$(PREFIX)/share/locale/$$l/LC_MESSAGES/tazterm.mo; \
	done
	gtk-update-icon-cache -q -t -f $(DESTDIR)$(PREFIX)/share/icons/hicolor 2>/dev/null || true

clean:
	$(MAKE) -C src clean
	$(MAKE) -C po clean

.PHONY: all install uninstall clean
