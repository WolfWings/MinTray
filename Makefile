CFLAGS=-Wall -pedantic -Os -s -std=gnu11
LDFLAGS=-Wl,--build-id=none
LDLIBS=-lX11

default: minbat

clean:
	$(RM) minbat
