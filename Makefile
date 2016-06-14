PROG=wireless
MAN =wireless.8
SRCS=wireless.c parse.y
BINDIR=/usr/local/bin

CFLAGS += -Wall -Werror -pedantic
CFLAGS += -std=c99
CFLAGS += -g -O0

all: $(PROG) readme

readme: $(MAN)
	mandoc -Tutf8 $(MAN) > $@

.include <bsd.prog.mk>
