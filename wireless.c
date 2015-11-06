#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include <sys/socket.h>
#include <net/if.h>

#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>

#define DEVICE "iwn0"
#define SCANSZ 64

/* from ifconfig.c */
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

	fprintf(stderr, "%llu: %s, len=%d\n", time(NULL), __func__, na.na_nodes);

	return na.na_nodes;
}

int
main(void) {
	struct ieee80211_nodereq nr[SCANSZ];
	char temp[IEEE80211_NWID_LEN + 1];
	int numnodes, i, len;

	temp[IEEE80211_NWID_LEN] = 0x00;
	fprintf(stderr, "%llu: device=" DEVICE "\n", time(NULL));

	memset(nr, 0x00, sizeof(nr) / sizeof(*nr));
	numnodes = scan(DEVICE, nr, sizeof(nr) / sizeof(*nr));

	fprintf(stderr, "%llu: got %d scan results\n", time(NULL), numnodes);
	for (i = 0; i < numnodes; i++) {
		len = nr[i].nr_nwid_len;
		if (len > IEEE80211_NWID_LEN)
			len = IEEE80211_NWID_LEN;
		memcpy(temp, nr[i].nr_nwid, IEEE80211_NWID_LEN);
		fprintf(stderr, "%llu: nwid=\"%s\"\n", time(NULL), temp);
	}

	return 0;
}
