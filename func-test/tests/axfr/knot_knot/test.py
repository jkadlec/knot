#!/usr/bin/env python3

'''Test for AXFR from Knot to Knot'''

import dnstest

t = dnstest.DnsTest()

master = t.server("knot")
slave = t.server("knot")
zones = t.zone_rnd(10)

t.link(zones, master, slave)

t.start()

master.zones_wait(zones)
slave.zones_wait(zones)
t.xfr_diff(master, slave, zones)

t.stop()
