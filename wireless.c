/*
 * Copyright (c) 2015, 2016, 2017, Gregor Best <gbe@unobtanium.de>
 * Parts Copyright (c) 2013, 2014, OpenBSD project
 *
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
#include <assert.h>
#include <err.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/queue.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>

#include "config.h"

#define ARRAY_SIZE(x) ((sizeof(x)) / sizeof(*x))
#define SCANSZ 512

struct network *
select_network(struct config *cnf, struct ieee80211_nodereq *nr, int numr) {
	int i;

	for (i = 0; i < numr; i++) {
		struct network *n;

		TAILQ_FOREACH(n, &cnf->networks, networks) {
			if (!strncmp(n->nwid, (char*) nr[i].nr_nwid, IEEE80211_NWID_LEN)) {
				memcpy(n->bssid, nr[i].nr_bssid, IEEE80211_ADDR_LEN);
				return n;
			}
		}
	}

	return NULL;
}

/* This function comes from OpenBSD's /usr/src/sbin/ifconfig/ifconfig.c */
int
rssicmp(const void *a, const void *b) {
	const struct ieee80211_nodereq *n1 = a, *n2 = b;
	int r1 = n1->nr_rssi;
	int r2 = n2->nr_rssi;

	return r2 < r1 ? -1 : r2 > r1;
}

int
scan(struct config *cnf, struct ieee80211_nodereq *nr, int nrlen) {
	struct ieee80211_nodereq_all na;
	struct ifreq ifr;
	int s;
	pid_t child;
	char *params[4] = { "ifconfig", cnf->device, "up", NULL };

	assert(nrlen > 0);

	if (posix_spawn(&child, "/sbin/ifconfig", NULL, NULL, params, NULL) != 0) {
		err(1, "posix_spawn: ifconfig");
	}
	waitpid(child, NULL, 0);

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err(1, "socket");

	memset(&ifr, 0x00, sizeof(ifr));
	(void) strlcpy(ifr.ifr_name, cnf->device, sizeof(ifr.ifr_name));

	if (ioctl(s, SIOCS80211SCAN, &ifr) != 0)
		err(1, "ioctl");

	memset(&na, 0x00, sizeof(na));
	memset(nr, 0x00, sizeof(nr) * nrlen);
	na.na_node = nr;
	na.na_size = nrlen * sizeof(*nr);
	(void) strlcpy(na.na_ifname, cnf->device, sizeof(na.na_ifname));

	if (ioctl(s, SIOCG80211ALLNODES, &na) != 0)
		err(1, "ioctl");

	if (pledge("stdio proc exec rpath wpath cpath", NULL) == -1) {
		perror("pledge");
		exit(2);
	}

	if (close(s) != 0)
		err(1, "close");

	qsort(nr, na.na_nodes, sizeof(*nr), rssicmp);

	return na.na_nodes;
}

void
configure_network(struct config *cnf, struct network *nw) {
	pid_t child;
	struct ether_addr ea;
	char *params[13]; /* Maximum number of ifconfig/wpa_cli parameters */

	if (!nw) {
		return;
	}

	memcpy(&ea.ether_addr_octet, nw->bssid, sizeof(ea.ether_addr_octet));

	/* Common parameters for all configuration options */
	params[0] = "ifconfig";
	params[1] = cnf->device;
	params[2] = "nwid";
	params[3] = nw->nwid;
	params[4] = "bssid";
	params[5] = ether_ntoa(&ea);
	params[6] = "-chan"; /* Autoselect channel */

	/* three options: open wifi, wpa/wpa2 or 802.1X */
	switch (nw->type) {
		case NW_OPEN:
			params[7] = "-wpa";
			params[8] = "-wpakey";
			params[9] = NULL;
			break;
		case NW_WPA2:
		case NW_8021X:
			params[7] = "wpa";
			params[8] = "wpaakms";
			if (nw->type == NW_WPA2) {
				params[9] = "psk";
				params[10] = "wpakey";
				params[11] = nw->wpakey;
				params[12] = NULL;
			} else {
				params[9] = "802.1x";
				params[10] = NULL;
			}
			break;
		default:
			errx(1, "BUG: Unknown network type %d!\n", nw->type);
	}

	if (cnf->verbose) {
		fprintf(stderr, "configuring wireless network %s\n", nw->nwid);
	}

	if (posix_spawn(&child, "/sbin/ifconfig", NULL, NULL, params, NULL) != 0) {
		err(1, "posix_spawn: ifconfig");
	}
	waitpid(child, NULL, 0);

	if (nw->type == NW_8021X) {
		params[0] = "wpa_cli";
		params[1] = "reassoc";
		params[2] = NULL;

		if (posix_spawn(&child, "/usr/local/sbin/wpa_cli", NULL, NULL, params, NULL) != 0) {
			err(1, "posix_spawn: wpa_cli");
		}

		waitpid(child, NULL, 0);
	}
}

void
write_nwlist(struct config *cnf, struct ieee80211_nodereq *nr, int numnodes) {
	int i, fd;
	char *tmpname;

	tmpname = strdup("/tmp/temp.XXXXXXXXXX");
	if (!tmpname) {
		err(1, NULL);
	}

	/* Write out /tmp/nw-aps */
	fd = mkstemp(tmpname);
	if (fd < 0) {
		err(1, "mkstemp");
	}

	for (i = 0; i < numnodes; i++) {
		char nwid[IEEE80211_NWID_LEN + 1];
		struct ether_addr ea;
		int len, enc, known;
		struct network *n;

		nwid[IEEE80211_NWID_LEN] = 0x00;
		len = nr[i].nr_nwid_len;
		if (len > IEEE80211_NWID_LEN)
			len = IEEE80211_NWID_LEN;
		memcpy(nwid, nr[i].nr_nwid, len);
		nwid[len] = 0x00;

		memcpy(&ea.ether_addr_octet, nr[i].nr_bssid, sizeof(ea.ether_addr_octet));

		enc = nr[i].nr_capinfo & IEEE80211_CAPINFO_PRIVACY;

		known = 0;
		TAILQ_FOREACH(n, &cnf->networks, networks) {
			if (!strncmp(n->nwid, (char*) nr[i].nr_nwid, IEEE80211_NWID_LEN)) {
				known = 1;
				break;
			}
		}

		/* bssid signal strength enc? known? nwid */
		dprintf(fd, "%s\t%d\t%s\t%sknown\t%s\n", ether_ntoa(&ea), nr[i].nr_rssi,
		        enc? "enc": "open", known? "": "un", nwid);
	}

	close(fd);

	if (rename(tmpname, "/tmp/nw-aps") == -1)
		err(1, "rename");

	free(tmpname);
}

void
free_config(struct config *cnf) {
	struct network *n, *tmp;

	TAILQ_FOREACH_SAFE(n, &cnf->networks, networks, tmp) {
		free(n->nwid);
		free(n->wpakey);
		free(n);
	}

	free(cnf->device);
	free(cnf);
}

int
main(int argc, char **argv) {
	struct config *cnf;
	struct ieee80211_nodereq nr[SCANSZ];
	int numnodes;

	if (argc == 2)
		cnf = parse_config(argv[1]);
	else
		cnf = parse_config("/etc/wireless.conf");

	if (!cnf) {
		return 1;
	}

	if (!cnf->device) {
		free_config(cnf);
		errx(1, "No device specified");
	}

	if (cnf->debug) {
		struct network *n;

		fprintf(stderr, "Configured networks:\n");

		TAILQ_FOREACH(n, &cnf->networks, networks) {
			fprintf(stderr, "\"%s\"", n->nwid);
			if (n->type == NW_WPA2)
				fprintf(stderr, " \"%s\"", n->wpakey);
			fprintf(stderr, "\n");
		}
	}

	memset(nr, 0x00, ARRAY_SIZE(nr));
	numnodes = scan(cnf, nr, ARRAY_SIZE(nr));

	configure_network(cnf, select_network(cnf, nr, numnodes));

	write_nwlist(cnf, nr, numnodes);

	free_config(cnf);

	return 0;
}
