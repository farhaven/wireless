#include <assert.h>
#include <err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/queue.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <sys/types.h>
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

/* from ifconfig.c */
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

	assert(nrlen > 0);

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err(1, "socket");

	switch ((child = fork())) {
		case -1:
			err(1, "fork");
		case 0:
			fclose(stdout);
			fclose(stderr);
			execlp("ifconfig", "ifconfig", cnf->device, "up", NULL);
			err(1, "execlp");
		default:
			waitpid(child, NULL, 0);
	}


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

	if (close(s) != 0)
		err(1, "close");

	qsort(nr, na.na_nodes, sizeof(*nr), rssicmp);

	return na.na_nodes;
}

void
configure_network(struct config *cnf, struct network *nw) {
	pid_t child;
	struct ether_addr ea;
	char *bssid;
	char *params[12]; /* Maximum number of ifconfig parameters */

	if (cnf->debug) {
		fprintf(stderr, "%s: %p\n", __func__, (void*) nw);
	}

	if (!nw) {
		return;
	}

	/* clear wireless settings */
	switch ((child = fork())) {
		case -1:
			err(1, "fork");
		case 0:
			/* inside child */
			execlp("ifconfig", "ifconfig", cnf->device, "-wpa",
			       "-wpakey", "-nwid", "-bssid", "-chan", NULL);
			err(1, "execlp");
		default:
			/* parent */
			waitpid(child, NULL, 0);
	}

	switch ((child = fork())) {
		case -1:
			err(1, "fork");
		case 0:
			/* inside child */
			memcpy(&ea.ether_addr_octet, nw->bssid, sizeof(ea.ether_addr_octet));
			bssid = ether_ntoa(&ea);

			/* Common parameters for all configuration options */
			params[0] = "ifconfig";
			params[1] = cnf->device;
			params[2] = "nwid";
			params[3] = nw->nwid;
			params[4] = "bssid";
			params[5] = bssid;

			/* three options: open wifi, wpa/wpa2 or 802.1X */
			if (nw->type == NW_OPEN) {
				params[6] = NULL;
			} else if (nw->type == NW_WPA2) {
				params[6] = "wpa";
				params[7] = "wpakey";
				params[8] = nw->wpakey;
				params[9] = "wpaakms";
				params[10] = "psk";
				params[11] = NULL;
			} else {
				params[6] = "wpa";
				params[7] = "wpaakms";
				params[8] = "802.1x";
				params[9] = NULL;
			}

			if (cnf->verbose) {
				fprintf(stderr, "configuring wireless network %s\n", nw->nwid);
			}

			execv("/sbin/ifconfig", params);
			err(1, "execv");
		default:
			waitpid(child, NULL, 0);
	}

	if (nw->type == NW_8021X) {
		switch ((child = fork())) {
			case -1:
				err(1, "fork");
			case 0:
				/* inside child */
				execlp("/usr/local/sbin/wpa_cli", "wpa_cli", "reassoc", NULL);
				err(1, "execlp");
			default:
				waitpid(child, NULL, 0);
		}
	}
}

void
write_nwlist(struct config *cnf, struct ieee80211_nodereq *nr, int numnodes) {
	int i;
	FILE *fh;

	/* Write out /tmp/nw-aps */
	if ((fh = fopen("/tmp/nw-aps", "w")) == NULL)
		err(1, "open");

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

		memcpy(&ea.ether_addr_octet, nr[i].nr_bssid,
		       sizeof(ea.ether_addr_octet));

		enc = nr[i].nr_capinfo & IEEE80211_CAPINFO_PRIVACY;

		known = 0;
		TAILQ_FOREACH(n, &cnf->networks, networks) {
			if (!strncmp(n->nwid, (char*) nr[i].nr_nwid, IEEE80211_NWID_LEN)) {
				known = 1;
				break;
			}
		}

		/* bssid signal strength enc? nwid */
		fprintf(fh, "%s\t%d\t%s\t%sknown\t%s\n", ether_ntoa(&ea), nr[i].nr_rssi,
		        enc? "enc": "open", known?"": "un", nwid);
	}

	fclose(fh);
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

	return 0;
}
