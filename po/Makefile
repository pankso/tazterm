# po/Makefile — modèle lxtaz : le .pot est généré, jamais commité.
GETTEXT_PACKAGE ?= tazterm
PREFIX ?= /usr
LOCALEDIR ?= $(PREFIX)/share/locale

LINGUAS := $(shell cat LINGUAS)
POFILES := $(LINGUAS:%=%.po)
MOFILES := $(LINGUAS:%=%.mo)

all: $(MOFILES)

%.mo: %.po
	msgfmt --check -o $@ $<

install: all
	for l in $(LINGUAS); do \
		install -Dm644 $$l.mo \
			$(DESTDIR)$(LOCALEDIR)/$$l/LC_MESSAGES/$(GETTEXT_PACKAGE).mo; \
	done

update-pot:
	xgettext --keyword=_ --keyword=N_ --from-code=UTF-8 \
		--package-name=$(GETTEXT_PACKAGE) -o $(GETTEXT_PACKAGE).pot \
		-f POTFILES.in --directory=..

check: all

clean:
	rm -f $(MOFILES) $(GETTEXT_PACKAGE).pot

.PHONY: all install update-pot check clean
