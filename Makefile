CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: ollama-dock
ollama-dock: src/ollama-dock.c
	$(CC) $(CFLAGS) -o $@ $<
clean:
	rm -f ollama-dock
install: ollama-dock
	install -Dm755 ollama-dock $(DESTDIR)/usr/local/bin/ollama-dock
.PHONY: all clean install
