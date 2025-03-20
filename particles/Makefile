.PHONY: all run clean

CC := emcc

CFLAGS := -Iinclude --preload-file assets
LDLIBS := -lm

ASSETS := $(wildcard assets/*)
SOURCES := $(wildcard src/*)

all: index.html

index.html: $(ASSETS) $(SOURCES)
	 $(CC) $(SOURCES) $(CFLAGS) -o $@ $(LDLIBS)

run: index.html
	emrun $<

clean:
	rm -f index.html index.js index.wasm index.data
