PROG=wireless
MAN =
SRCS=wireless.c parse.y conflex.l
BINDIR=/usr/local/bin

CFLAGS += -Wall -Werror -pedantic
CFLAGS += -std=c99
CFLAGS += -g -O0

LDADD += -lutil -lfl
DPADD += ${LIBUTIL}

.include <bsd.prog.mk>
