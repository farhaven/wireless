PROG=wireless
MAN =
SRCS=wireless.c

CFLAGS += -Wall -Werror -pedantic
CFLAGS += -std=c99
CFLAGS += -g

.include <bsd.prog.mk>
