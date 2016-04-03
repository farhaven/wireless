%{
#include <err.h>
#include <stdio.h>

#include <sys/socket.h>
#include <net/if.h>
#include <sys/queue.h>

#include "config.h"

static struct file {
	FILE *stream;
	char *name;
	int errors;
} file;

extern FILE *yyin;
int yyparse(void);
int yylex(void);
int yyerror(const char *);

YYSTYPE yylval = { { NULL }, 1 };

struct config *conf;
%}

%token	T_DEVICE
%token	ERROR T_VERBOSE T_DEBUG
%token	T_NET_OPEN T_NET_WPA T_NET_8021X
%token	T_STRING

%type	<v.string> T_STRING

%type	<v.nw> open
%%

/* Grammar */
grammar	:
		| grammar '\n'
		| grammar verbose '\n'
		| grammar debug '\n'
		| grammar network '\n'
		| grammar device '\n'
		| grammar error '\n' { file.errors++; }
		;

device		: T_DEVICE T_STRING
		{
			if (strlen($2) > IFNAMSIZ) {
				char *tmp;
				asprintf(&tmp,"Device name '%s' too long "
				         "(maximum: %d, is: %ld)", $2, IFNAMSIZ,
				         strlen($2));
				yyerror(tmp);
				free(tmp);
				YYERROR;
			}
			conf->device = $2;
		}
		;

network		: open | wpa | enterprise;

open		: T_NET_OPEN T_STRING
		{
			struct network *nw = new_network(NW_OPEN, $2);
			TAILQ_INSERT_TAIL(&conf->networks, nw, networks);
		}
		;

enterprise	: T_NET_8021X T_STRING
		{
			struct network *nw = new_network(NW_8021X, $2);
			TAILQ_INSERT_TAIL(&conf->networks, nw, networks);
		}
		;

wpa		: T_NET_WPA T_STRING T_STRING
		{
			struct network *nw = new_network(NW_WPA2, $2);
			nw->wpakey = $3;
			TAILQ_INSERT_TAIL(&conf->networks, nw, networks);
		}
		;

verbose		: T_VERBOSE
		{
			conf->verbose = 1;
		}
		;

debug		: T_DEBUG
		{
			conf->debug = 1;
		}
		;
%%

struct network *
new_network(enum network_type type, char *nwid) {
	struct network *nw = calloc(1, sizeof(*nw));
	if (nw == NULL) {
		err(1, "calloc");
	}
	nw->type = type;
	nw->nwid = nwid;
	return nw;
}

int
yyerror(const char *msg) {
	file.errors++;
	fprintf(stderr, "%s:%d: %s\n", file.name, yylval.lineno, msg);
	return 0;
}

struct config *
parse_config(char *filename) {
	if ((file.name = strdup(filename)) == NULL) {
		warn("strdup");
		return NULL;
	}

	if ((file.stream = fopen(file.name, "r")) == NULL) {
		warn("open %s", file.name);
		free(file.name);
		return NULL;
	}

	if ((conf = calloc(1, sizeof(*conf))) == NULL) {
		err(1, "calloc");
	}
	TAILQ_INIT(&conf->networks);

	yyin = file.stream;
	yyparse();
	fclose(file.stream);
	free(file.name);

	if (file.errors == 0)
		return conf;
	return NULL;
}
