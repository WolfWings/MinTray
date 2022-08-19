CFLAGS=-Wall -pedantic -Os -s -std=gnu11
LDFLAGS=-Wl,--build-id=none
LDLIBS=-lX11

BINARIES=minbat mincpu

default: $(BINARIES)

minbat: minbat.o systray.o

mincpu: mincpu.o systray.o

clean:
	$(RM) $(BINARIES) *.o
