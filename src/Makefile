OBJECTS=gboyemu.o z80.o mmu.o rom.o gpu.o interrupt.o joypad.o serial.o divider.o timer.o sound.o square.o blip_buf.o lfsr.o
GBOYEMU=gboyemu
SUBDIRS=wx
CC=gcc

OPTIMIZE=-O3 -DNDEBUG
DEBUG=-O0 -g

CFLAGS+=$(DEBUG) -Werror -Wall $(shell pkg-config --cflags sdl)
LDFLAGS+=$(shell pkg-config --libs sdl)

all: $(GBOYEMU)

$(GBOYEMU): $(OBJECTS)
	$(CC) -o $@ $+ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(GBOYEMU)
