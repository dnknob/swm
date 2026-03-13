CC      = cc
CFLAGS  = -std=c99 -pedantic \
          -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
          -D_POSIX_C_SOURCE=200809L
LDFLAGS =
LIBS    = -lX11

PREFIX    = /usr/local
BINDIR    = $(PREFIX)/bin
MANDIR    = $(PREFIX)/share/man/man1
CONFDIR   = $(HOME)/.config/swm

TARGET  = swm
SRCS    = swm.c mng.c util.c
OBJS    = $(SRCS:.c=.o)
HDRS    = swm.h mng.h util.h

.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: debug
debug: CFLAGS += -g3 -O0 -DDEBUG \
                 -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: $(TARGET)

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: install
install: $(TARGET)
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)
	install -d $(MANDIR)
	install -m 644 swm.1 $(MANDIR)/swm.1
	install -d $(CONFDIR)
	@if [ ! -f $(CONFDIR)/swm.conf ]; then \
	    install -m 644 swm.conf $(CONFDIR)/swm.conf; \
	    echo "Installed default config to $(CONFDIR)/swm.conf"; \
	else \
	    echo "Config already exists at $(CONFDIR)/swm.conf — not overwriting"; \
	fi

.PHONY: uninstall
uninstall:
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(MANDIR)/swm.1
	@echo "Config at $(CONFDIR)/swm.conf left intact (remove manually)."

.PHONY: xephyr
xephyr: $(TARGET)
	Xephyr -ac -screen 1280x720 -br :1 &
	sleep 0.5
	DISPLAY=:1 xterm &
	sleep 0.2
	DISPLAY=:1 ./$(TARGET) $(CONFDIR)/swm.conf