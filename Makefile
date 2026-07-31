PREFIX ?= /usr/local
CC = clang

PKG_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 libcurl)
PKG_LIBS   := $(shell pkg-config --libs gtk+-3.0 libcurl)

CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wno-unused-parameter $(PKG_CFLAGS)
LDFLAGS += $(PKG_LIBS) -lpthread

SRC := src/main.c src/ui.c src/store.c src/integrate.c src/desktop.c \
       src/update.c src/net.c src/util.c vendor/cJSON.c
OBJ := $(SRC:.c=.o)
BIN := appache

.PHONY: all clean install uninstall

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -Dm644 data/appache.desktop $(DESTDIR)$(PREFIX)/share/applications/appache.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -f $(DESTDIR)$(PREFIX)/share/applications/appache.desktop
