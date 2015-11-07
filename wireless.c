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

#include <sys/socket.h>
#include <net/if.h>

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
};

struct config {
	TAILQ_HEAD(, network) networks;
};

struct config *
init_config() {
	struct config *cnf = calloc(1, sizeof(*cnf));

	struct network *nw;
	TAILQ_INIT(&cnf->networks);

	nw = calloc(1, sizeof(*nw));
	nw->nwid = strdup("foobar");
	TAILQ_INSERT_TAIL(&cnf->networks, nw, networks);

	nw = calloc(1, sizeof(*nw));
	nw->nwid = strdup("c3pb");
	TAILQ_INSERT_TAIL(&cnf->networks, nw, networks);

	nw = calloc(1, sizeof(*nw));
	nw->nwid = strdup("bier");
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
scan(const char *ifname, struct ieee80211_nodereq *nr, int nrlen) {
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

int
main(void) {
	struct config *cnf;
	struct ieee80211_nodereq nr[SCANSZ];
	char temp[IEEE80211_NWID_LEN + 1];
	int numnodes, i, len;

	cnf = init_config();
	temp[IEEE80211_NWID_LEN] = 0x00;
	fprintf(stderr, "%llu: device=" DEVICE "\n", time(NULL));

	memset(nr, 0x00, ARRAY_SIZE(nr));
	numnodes = scan(DEVICE, nr, ARRAY_SIZE(nr));

	if (numnodes == 0) {
		fprintf(stderr, "%llu: no networks found\n", time(NULL));
	} else {
		struct network *nw = select_network(cnf, nr, numnodes);
		fprintf(stderr, "%llu: configuration %p\n", time(NULL), (void*) nw);
	}

	fprintf(stderr, "%llu: got %d scan results\n", time(NULL), numnodes);
	for (i = 0; i < numnodes; i++) {
		len = nr[i].nr_nwid_len;
		if (len > IEEE80211_NWID_LEN)
			len = IEEE80211_NWID_LEN;
		memcpy(temp, nr[i].nr_nwid, len);
		temp[len] = 0x00;
		fprintf(stderr, "%llu: nwid=\"%s\"\trssi=%d\n", time(NULL), temp, nr[i].nr_rssi);
	}

	return 0;
}
