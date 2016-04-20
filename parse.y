/* Based on OpenBGPD's parse.y */
/*
 * Copyright (c) 2016 Gregor Best <gbe@unobtanium.de>
 * Copyright (c) 2002, 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

%{
#include <sys/socket.h>
#include <net/if.h>

#include <ctype.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>

#include "config.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	int			 lineno;
	int			 errors;
} *file, *topfile;
struct file	*pushfile(const char *);
int		 popfile(void);
int		 yyparse(void);
int		 yylex(void);
int		 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
int		 kw_cmp(const void *, const void *);
int		 lookup(char *);
int		 lgetc(int);
int		 lungetc(int);
int		 findeol(void);

TAILQ_HEAD(symhead, sym)	 symhead = TAILQ_HEAD_INITIALIZER(symhead);
struct sym {
	TAILQ_ENTRY(sym)	 entry;
	int			 used;
	char			*nam;
	char			*val;
};
int		 symset(const char *, const char *);
char		*symget(const char *);

struct network	*new_network(enum network_type, char *);

static struct config	*conf;
%}

%token	T_DEVICE
%token	T_VERBOSE T_DEBUG
%token	T_NET_OPEN T_NET_WPA T_NET_8021X
%token	T_NETWORK T_EQ
%token	T_ERROR T_INCLUDE
%token	<v.string>	T_STRING
%%

grammar		: /* empty */
		| grammar '\n'
		| grammar include '\n'
		| grammar varset '\n'
		| grammar verbose '\n'
		| grammar debug '\n'
		| grammar network '\n'
		| grammar device '\n'
		| grammar error '\n'	{ file->errors++; }
		;

varset		: T_STRING T_EQ T_STRING {
			printf("%s = \"%s\"\n", $1, $3);
			if (symset($1, $3) == -1)
				err(1, "cannot store variable");
			free($1);
			free($3);
		}
		;

include		: T_INCLUDE T_STRING {
			struct file	*nfile;

			if ((nfile = pushfile($2)) == NULL) {
				yyerror("failed to include file %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			file = nfile;
			lungetc('\n');
		}
		;

device		: T_DEVICE T_STRING {
			if (strlen($2) > IFNAMSIZ) {
				char *tmp;
				asprintf(&tmp,"Device name '%s' too long "
				         "(maximum: %d, is: %ld)",
				         $2, IFNAMSIZ, strlen($2));
				yyerror(tmp);
				free(tmp);
				YYERROR;
			}
			conf->device = $2;
		}
		;

network		: open | wpa | enterprise;

open		: T_NET_OPEN T_STRING {
			struct network *nw = new_network(NW_OPEN, $2);
			TAILQ_INSERT_TAIL(&conf->networks, nw, networks);
		}
		;

enterprise	: T_NET_8021X T_STRING {
			struct network *nw = new_network(NW_8021X, $2);
			TAILQ_INSERT_TAIL(&conf->networks, nw, networks);
		}
		;

wpa		: T_NET_WPA T_STRING T_STRING {
			struct network *nw = new_network(NW_WPA2, $2);
			nw->wpakey = $3;
			TAILQ_INSERT_TAIL(&conf->networks, nw, networks);
		}
		;

verbose		: T_VERBOSE { conf->verbose = 1; }
		;

debug		: T_DEBUG { conf->debug = 1; }
		;
%%

struct keywords {
	const char	*k_name;
	int		 k_val;
};

int
yyerror(const char *fmt, ...)
{
	va_list	 ap;
	char	*msg;

	file->errors++;
	va_start(ap, fmt);
	if (vasprintf(&msg, fmt, ap) == -1)
		err(1, "yyerror vasprintf");
	va_end(ap);
	errx(1, "%s:%d: %s", file->name, yylval.lineno, msg);
	free(msg);
	return (0);
}

int
kw_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct keywords *)e)->k_name));
}

int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{ "802.1x",	T_NET_8021X },
		{ "=",		T_EQ },
		{ "debug",	T_DEBUG },
		{ "device",	T_DEVICE },
		{ "include",	T_INCLUDE },
		{ "network",	T_NETWORK },
		{ "open",	T_NET_OPEN },
		{ "verbose",	T_VERBOSE },
		{ "wpa",	T_NET_WPA },
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kw_cmp);

	if (p)
		return (p->k_val);
	else
		return (T_STRING);
}

#define MAXPUSHBACK	128

u_char	pushback_buffer[MAXPUSHBACK];
int	pushback_index = 0;

int
lgetc(int quotec)
{
	int	c, next;

	if (pushback_index)
		return (pushback_buffer[--pushback_index]);

	if (quotec) {
		if ((c = getc(file->stream)) == EOF) {
			yyerror("reached end of file while parsing "
			        "quoted string");
			if (file == topfile || popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = getc(file->stream)) == '\\') {
		next = getc(file->stream);
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}

	while (c == EOF) {
		if (file == topfile || popfile() == EOF)
			return (EOF);
		c = getc(file->stream);
	}
	return (c);
}

int
lungetc(int c)
{
	if (c == EOF)
		return (EOF);
	if (pushback_index < MAXPUSHBACK-1)
		return (pushback_buffer[pushback_index++] = c);
	else
		return (EOF);
}

int
findeol(void)
{
	int	c;

	/* skip to either EOF or the first real EOL */
	while (1) {
		if (pushback_index)
			c = pushback_buffer[--pushback_index];
		else
			c = lgetc(0);
		if (c == '\n') {
			file->lineno++;
			break;
		}
		if (c == EOF)
			break;
	}
	return (T_ERROR);
}

int
yylex(void)
{
	char	 buf[8096];
	char	*p, *val;
	int	 quotec, next, c;
	int	 token;

	p = buf;
	while ((c = lgetc(0)) == ' ' || c == '\t')
		; /* nothing */

	yylval.lineno = file->lineno;
	if (c == '#')
		while ((c = lgetc(0)) != '\n' && c != EOF)
			; /* nothing */
	if (c == '$') {
		while (1) {
			if ((c = lgetc(0)) == EOF)
				return (0);

			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			if (isalnum(c) || c == '_') {
				*p++ = c;
				continue;
			}
			*p = '\0';
			lungetc(c);
			break;
		}
		val = symget(buf);
		if (val == NULL) {
			yyerror("macro '%s' not defined", buf);
			return (findeol());
		}
		yylval.v.string = strdup(val);
		if (yylval.v.string == NULL)
			err(1, "strdup");
		return T_STRING;
	}

	switch (c) {
	case '\'':
	case '"':
		quotec = c;
		while (1) {
			if ((c = lgetc(quotec)) == EOF)
				return (0);
			if (c == '\n') {
				file->lineno++;
				continue;
			} else if (c == '\\') {
				if ((next = lgetc(quotec)) == EOF)
					return (0);
				if (next == quotec || c == ' ' || c == '\t')
					c = next;
				else if (next == '\n') {
					file->lineno++;
					continue;
				} else
					lungetc(next);
			} else if (c == quotec) {
				*p = '\0';
				break;
			} else if (c == '\0') {
				yyerror("syntax error");
				return (findeol());
			}
			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			*p++ = c;
		}
		yylval.v.string = strdup(buf);
		if (yylval.v.string == NULL)
			err(1, "yylex: strdup");
		return (T_STRING);
	}

#define allowed_in_string(x) \
	(isalnum(x) || ispunct(x))

	if (allowed_in_string(c)) {
		do {
			*p++ = c;
			if ((unsigned)(p - buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && (allowed_in_string(c)));
		lungetc(c);
		*p = '\0';
		if ((token = lookup(buf)) == T_STRING) {
			yylval.v.string = strdup(buf);
			if (yylval.v.string == NULL)
				err(1, "yylex: strdup");
		}
		return (token);
	}
	if (c == '\n') {
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == EOF)
		return (0);
	return (c);
}

struct file *
pushfile(const char *name)
{
	struct file	*nfile;

	if ((nfile = calloc(1, sizeof(struct file))) == NULL) {
		warn("malloc");
		return (NULL);
	}
	if ((nfile->name = strdup(name)) == NULL) {
		warn("malloc");
		free(nfile);
		return (NULL);
	}
	if ((nfile->stream = fopen(nfile->name, "r")) == NULL) {
		warn("%s", nfile->name);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	nfile->lineno = 1;
	TAILQ_INSERT_TAIL(&files, nfile, entry);
	return (nfile);
}

int
popfile(void)
{
	struct file	*prev;

	if ((prev = TAILQ_PREV(file, files, entry)) != NULL)
		prev->errors += file->errors;

	TAILQ_REMOVE(&files, file, entry);
	fclose(file->stream);
	free(file->name);
	free(file);
	file = prev;
	return (file ? 0 : EOF);
}

struct config *
new_config() {
	struct config *cnf = calloc(1, sizeof(*cnf));

	if (cnf == NULL) {
		return NULL;
	}
	TAILQ_INIT(&cnf->networks);

	return cnf;
}

struct network *
new_network(enum network_type type, char *nwid) {
	struct network *nw = calloc(1, sizeof(*nw));
	if (nw == NULL) {
		err(1, NULL);
	}

	nw->type = type;
	nw->nwid = nwid;
	return nw;
}

struct config *
parse_config(char *filename)
{
	struct sym	*sym, *next;
	int		errors = 0;

	conf = new_config();

	if ((file = pushfile(filename)) == NULL) {
		free(conf);
		return NULL;
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	for (sym = TAILQ_FIRST(&symhead); sym != NULL; sym = next) {
		next = TAILQ_NEXT(sym, entry);
		if (!sym->used)
			fprintf(stderr, "warning: macro \"%s\" not used\n",
			        sym->nam);
		free(sym->nam);
		free(sym->val);
		TAILQ_REMOVE(&symhead, sym, entry);
		free(sym);
	}

	if (errors) {
		/* Handle errors */
		return NULL;
	}

	return conf;
}

int
symset(const char *nam, const char *val)
{
	struct sym	*sym;

	for (sym = TAILQ_FIRST(&symhead); sym && strcmp(nam, sym->nam);
	     sym = TAILQ_NEXT(sym, entry))
		;	/* nothing */

	if (sym != NULL) {
		free(sym->nam);
		free(sym->val);
		TAILQ_REMOVE(&symhead, sym, entry);
		free(sym);
	}
	if ((sym = calloc(1, sizeof(*sym))) == NULL)
		return (-1);

	sym->nam = strdup(nam);
	if (sym->nam == NULL) {
		free(sym);
		return (-1);
	}
	sym->val = strdup(val);
	if (sym->val == NULL) {
		free(sym->nam);
		free(sym);
		return (-1);
	}
	TAILQ_INSERT_TAIL(&symhead, sym, entry);
	return (0);
}

char *
symget(const char *nam)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry)
		if (strcmp(nam, sym->nam) == 0) {
			sym->used = 1;
			return (sym->val);
		}
	return (NULL);
}
