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

#define ARRAY_SIZE(x) ((sizeof(x)) / sizeof(*x))
#define DEVICE "iwn0"
#define SCANSZ 512

struct network {
	TAILQ_ENTRY(network) networks;
	const char *nwid;
	const char *setup;
	char bssid[IEEE80211_ADDR_LEN];
	enum {
		NW_UNKNOWN,
		NW_OPEN,
		NW_WPA2,
		NW_8021X
	} type;
	char *wpakey;
};

struct config {
	TAILQ_HEAD(, network) networks;
	char *device;
};

struct config *
make_config() {
	struct config *cnf = calloc(1, sizeof(*cnf));

	cnf->device = strdup(DEVICE);

	struct network *nw;
	TAILQ_INIT(&cnf->networks);

	nw = calloc(1, sizeof(*nw));
	nw->nwid = strdup("foobar");
	nw->type = NW_OPEN;
	TAILQ_INSERT_TAIL(&cnf->networks, nw, networks);

	nw = calloc(1, sizeof(*nw));
	nw->nwid = strdup("c3pb");
	nw->type = NW_WPA2;
	nw->wpakey = strdup("chaoschaos");
	TAILQ_INSERT_TAIL(&cnf->networks, nw, networks);

	nw = calloc(1, sizeof(*nw));
	nw->nwid = strdup("bier");
	nw->type = NW_OPEN;
	TAILQ_INSERT_TAIL(&cnf->networks, nw, networks);

	return cnf;
}

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
scan(char *ifname, struct ieee80211_nodereq *nr, int nrlen) {
	struct ieee80211_nodereq_all na;
	struct ifreq ifr;
	int s;

	assert(nrlen > 0);

	fprintf(stderr, "%llu: %s, len=%d\n", time(NULL), __func__, nrlen);
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err(1, "socket");

	/* TODO: Make sure interface is up */

	memset(&ifr, 0x00, sizeof(ifr));
	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	if (ioctl(s, SIOCS80211SCAN, &ifr) != 0)
		err(1, "ioctl");

	memset(&na, 0x00, sizeof(na));
	memset(nr, 0x00, sizeof(nr) * nrlen);
	na.na_node = nr;
	na.na_size = nrlen * sizeof(*nr);
	(void) strlcpy(na.na_ifname, ifname, sizeof(na.na_ifname));

	if (ioctl(s, SIOCG80211ALLNODES, &na) != 0)
		err(1, "ioctl");

	if (close(s) != 0)
		err(1, "close");

	qsort(nr, na.na_nodes, sizeof(*nr), rssicmp);

	fprintf(stderr, "%llu: %s, len=%d\n", time(NULL), __func__, na.na_nodes);

	return na.na_nodes;
}

void
configure_network(struct config *cnf, struct network *nw) {
	pid_t child;

	fprintf(stderr, "%llu: configuration %p\n", time(NULL), (void*) nw);

	if (!nw) {
		return;
	}

	fprintf(stderr, "%llu: configuring network with nwid %s\n", time(NULL), nw ? nw->nwid: "(none)");

	/* clear wireless settings */
	switch ((child = fork())) {
		case -1:
			err(1, "fork");
		case 0:
			/* inside child */
			execlp("ifconfig", "ifconfig", cnf->device, "-wpa", "-wpakey", "-nwid", "-bssid", "-chan", NULL);
			err(1, "execlp");
		default:
			/* parent */
			waitpid(child, NULL, 0);
	}

	/* three options: open wifi, wpa/wpa2 or 802.1X */
	if ((child = fork()) == -1)
		err(1, "fork");

	if (child != 0) {
		waitpid(child, NULL, 0);
		return;
	}

	switch (nw->type) {
		case NW_OPEN:
			fprintf(stderr, "%llu: configuring nwid %s for open access\n", time(NULL), nw->nwid);
			execlp("ifconfig", "ifconfig", cnf->device, "nwid", nw->nwid, NULL);
			err(1, "execlp");
		case NW_WPA2:
			fprintf(stderr, "%llu: configuring nwid %s for wpa2 (key: %s)\n", time(NULL), nw->nwid, nw->wpakey);
			execlp("ifconfig", "ifconfig", cnf->device, "nwid", nw->nwid, "wpa", "wpakey", nw->wpakey, NULL);
			err(1, "execlp");
		case NW_8021X:
			fprintf(stderr, "%llu: configuring nwid %s for 802.1X\n", time(NULL), nw->nwid);
			execlp("ifconfig", "ifconfig", cnf->device, "nwid", nw->nwid, "wpa", "wpaakms", "802.1x", NULL);
			err(1, "execlp");
		default:
			errx(1, "unknown network type :(");
	}
}

int
main(void) {
	struct config *cnf;
	struct ieee80211_nodereq nr[SCANSZ];
	int numnodes, i;
	FILE *fh;

	cnf = make_config();
	fprintf(stderr, "%llu: device=%s\n", time(NULL), cnf->device);

	memset(nr, 0x00, ARRAY_SIZE(nr));
	numnodes = scan(cnf->device, nr, ARRAY_SIZE(nr));

	if (numnodes == 0) {
		fprintf(stderr, "%llu: no networks found\n", time(NULL));
	} else {
		struct network *nw = select_network(cnf, nr, numnodes);
		configure_network(cnf, nw);
	}

	/* Write out /tmp/nw-aps */
	if ((fh = fopen("/tmp/nw-aps", "w")) == NULL)
		err(1, "open");

	for (i = 0; i < numnodes; i++) {
		char nwid[IEEE80211_NWID_LEN + 1];
		struct ether_addr ea;
		int len, enc;

		nwid[IEEE80211_NWID_LEN] = 0x00;
		len = nr[i].nr_nwid_len;
		if (len > IEEE80211_NWID_LEN)
			len = IEEE80211_NWID_LEN;
		memcpy(nwid, nr[i].nr_nwid, len);
		nwid[len] = 0x00;

		memcpy(&ea.ether_addr_octet, nr[i].nr_bssid, sizeof(ea.ether_addr_octet));

		enc = nr[i].nr_capinfo & IEEE80211_CAPINFO_PRIVACY;

		/* bssid signal strength enc? nwid */
		fprintf(fh, "%s\t%d\t%s\t%s\n", ether_ntoa(&ea), nr[i].nr_rssi, enc? "enc": "", nwid);
	}

	fclose(fh);

	return 0;
}
