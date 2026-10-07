CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
DATADIR ?= $(PREFIX)/share
UDEVDIR ?= /etc/udev/rules.d

all: src/superstrikectl src/libss2k.so

src/superstrikectl: src/superstrikectl.c src/ss2k.c src/ss2k.h
	$(CC) $(CFLAGS) -o $@ src/superstrikectl.c src/ss2k.c

src/libss2k.so: src/ss2k.c src/ss2k.h
	$(CC) $(CFLAGS) -fPIC -shared -o $@ src/ss2k.c

install: all
	install -Dm755 src/superstrikectl $(DESTDIR)$(BINDIR)/superstrikectl
	install -Dm755 src/libss2k.so $(DESTDIR)$(LIBDIR)/libss2k.so
	install -Dm755 gui/superstrike-gui $(DESTDIR)$(BINDIR)/superstrike-gui
	install -Dm755 gui/superstrike-tray $(DESTDIR)$(BINDIR)/superstrike-tray
	install -Dm644 gui/ss2k_binding.py $(DESTDIR)$(DATADIR)/superstrike/ss2k_binding.py
	install -Dm644 gui/superstrike-gui.desktop $(DESTDIR)$(DATADIR)/applications/superstrike-gui.desktop
	install -Dm644 gui/superstrike-tray.desktop $(DESTDIR)$(DATADIR)/applications/superstrike-tray.desktop
	install -Dm644 udev/99-superstrike.rules $(DESTDIR)$(UDEVDIR)/99-superstrike.rules
	@echo "udev rule installed; reload with: sudo udevadm control --reload && sudo udevadm trigger"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/superstrikectl $(DESTDIR)$(BINDIR)/superstrike-gui \
	      $(DESTDIR)$(BINDIR)/superstrike-tray $(DESTDIR)$(LIBDIR)/libss2k.so \
	      $(DESTDIR)$(DATADIR)/superstrike/ss2k_binding.py \
	      $(DESTDIR)$(DATADIR)/applications/superstrike-gui.desktop \
	      $(DESTDIR)$(DATADIR)/applications/superstrike-tray.desktop \
	      $(DESTDIR)$(UDEVDIR)/99-superstrike.rules

clean:
	rm -f src/superstrikectl src/libss2k.so

.PHONY: all install uninstall clean
