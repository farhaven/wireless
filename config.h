/*
 * Copyright (c) 2015, 2016, 2017 Gregor Best <gbe@unobtanium.de>

 * Permission to use, copy, modify, and/or distribute this software for any purpose
 * with or without fee is hereby granted, provided that the above copyright notice
 * and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
 * THIS SOFTWARE.
 */

#include <sys/queue.h>

#include <net80211/ieee80211.h>

enum network_type {
	NW_UNKNOWN,
	NW_OPEN,
	NW_WPA2,
	NW_8021X
};

struct network {
	TAILQ_ENTRY(network) networks;
	char *nwid;
	char *wpakey;
	char bssid[IEEE80211_ADDR_LEN];
	enum network_type type;
};

struct config {
	TAILQ_HEAD(, network) networks;
	char *device;
	int debug;
	int verbose;
};

typedef struct {
	union {
		char *string;
		struct network *nw;
	} v;
	int lineno;
} YYSTYPE;

struct config *parse_config(char *fname);
void free_config(struct config *);
