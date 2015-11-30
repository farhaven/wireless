%option nounput
%{
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "y.tab.h"

extern YYSTYPE yylval;
char *extract_string(char*);
%}

%%
verbose		return T_VERBOSE;
device		return T_DEVICE;
open		return T_NET_OPEN;
wpa		return T_NET_WPA;
802.1x		return T_NET_8021X;
[[:alnum:][:punct:]]+		yylval.v.string=strdup(yytext); return T_STRING;
\"[[:print:]]+\"	yylval.v.string=extract_string(yytext); return T_STRING;
'[[:print:]]+'	yylval.v.string=extract_string(yytext); return T_STRING;
#.*\n		|
\n		yylval.lineno++; return '\n';
[ \t]		;
%%

char *
extract_string(char *s) {
	char *r = strdup(s + 1);
	r[strlen(r) - 1] = 0x00;
	return r;
}
