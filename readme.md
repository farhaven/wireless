Wireless
========
This thing scans for and configures wireless networks on OpenBSD. It uses
ifconfig to do most of the heavy lifting and basically just interprets scan
results.

Usage
-----
Just run it once to scan for known wireless LANs and configure the earliest
network in the configuration file, for example:

    $ wireless
    ... wait a bit for scanning and configuration ...
    $ dhclient iwn0

The result of the scan is also written to the file `/tmp/nw-aps`. It contains
one line for each access point with the following information:

    BSSID RSSI encrypted? known? SSID

Configuration
-------------
This is an example configuration file:

    device iwn0
    verbose
    
    open   freifunk.paderborn.net
    802.1x eduroam
    wpa    "home network" thisismypassword

There are three kinds of networks, `open`, `wpa` and `802.1x`. All of these get
an SSID as their first parameter. `wpa` gets the password as the second
parameter. SSIDs and passwords which contain spaces can be enclosed in single or
double quotes. Regular string quoting rules apply:

    wpa "Hello\"foo" 'I'm a password!'

The priority of a network depends on the order of networks in the configuration
file. The earlier a network appears, the higher is its priority. If an SSID is
visible multiple times (such as in campus networks), the access point with the
strongest RSSI is chosen.

If the keyword `verbose` is given in the configuration file, the name of the
network that is being configured will be printed to the standard error stream.
