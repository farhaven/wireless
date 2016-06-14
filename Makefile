PROG=wireless
MAN =wireless.8
SRCS=wireless.c parse.y
BINDIR=/usr/local/bin

CFLAGS += -Wall -Werror -pedantic
CFLAGS += -std=c99
CFLAGS += -g -O0

all: $(PROG) README

README: $(MAN)
	mandoc -Tascii $(MAN) | perl -e 'while (<>) { s/.\x08//g; print }' > $@

.include <bsd.prog.mk>
