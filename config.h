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
