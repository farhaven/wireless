PROG=wireless
MAN =
SRCS=wireless.c parse.y
BINDIR=/usr/local/bin

CFLAGS += -Wall -Werror -pedantic
CFLAGS += -std=c99
CFLAGS += -g -O0

LDADD += -lutil

.include <bsd.prog.mk>
