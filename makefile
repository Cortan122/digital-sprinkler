LDLIBS=-lm -lcrypto -lz
WARNINGS=-Wall -Wextra -Wno-parentheses -Wno-unknown-pragmas -Wno-sign-compare -Wno-deprecated-declarations -Werror=vla
CFLAGS=-fdollars-in-identifiers -funsigned-char -O2 $(WARNINGS) -I. '-D__DIR__="$(shell realpath .)"'

all: sprinkler
sprinkler: sprinkler.o util.o git.o
sprinkler.o: stb_ds.h
util.o: util.h
git.o: git.h

clean:
	rm -f *.o sprinkler stb_ds.h

stb_ds.h:
	curl --silent -O https://raw.githubusercontent.com/nothings/stb/master/stb_ds.h
