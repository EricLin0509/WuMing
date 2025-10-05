CC=gcc
CFLAGS=-Wall -Werror -g -lclamav
BIN=clamscanc
SRC=clamscanc.c manager.c watchdog.c

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(BIN)

install: $(BIN)
	cp $(BIN) /usr/bin
	chmod 755 /usr/bin/$(BIN)

uninstall:
	rm -f /usr/bin/$(BIN)
