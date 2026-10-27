#!/usr/bin/env python3
"""Decode the -wex-adlp-edp WEXPanelTrace ring from an `ioreg -l -d1` dump."""
import re, struct, sys

NAMES = {1: "attr", 4: "probe", 5: "AUXFAIL", 6: "enablePort", 7: "disablePort", 8: "trainPat",
         9: "clkOn", 10: "clkOff", 11: "DDC", 20: "connChg>", 21: "connChg<", 22: "online>",
         23: "online<", 24: "connProbe", 25: "curMode", 26: "linkBW", 27: "setupTim", 28: "rateSel", 29: "rateCap", 30: "bootAdopt"}
ALL = (1 << 64) - 1


def fb(v):
    if v == ALL:
        return "-"
    return "fb%d on=%d e1=%d e2=%d e4=%d e6=%d f=%x" % (
        v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff, (v >> 32) & 0xff, (v >> 40) & 0xff, v >> 48)


def link(v):
    return "-" if v == ALL else "buf=%08x func=%08x" % (v & 0xffffffff, v >> 32)


def describe(ev, a, b, c, d, e):
    if ev == 1:
        return "fb=%d attr=%s res=%x val=%x %s" % (a, b.to_bytes(4, "big").decode("latin1"), c, d, fb(e))
    if ev in (20, 22):
        return "%s %s" % (fb(a), link(b))
    if ev == 21:
        return "%s %s ret=%x" % (fb(a), link(b), c)
    if ev == 23:
        return "%s online=%x ret=%x port=%x %s" % (fb(a), b, c, d, link(e))
    if ev == 24:
        return "%s before=%d after=%d restored=%d ret=%x fbcount=%d e54=%d e55=%d e56=%d %s" % (
            fb(a), b & 0xff, (b >> 8) & 0xff, (b >> 16) & 1, c, d & 0xff, (d >> 8) & 0xff, (d >> 16) & 0xff,
            (d >> 24) & 0xff, link(e))
    if ev == 25:
        return "live=%dx%d modes=%d chosen=%x first.pclk=%d first=%dx%d" % (a >> 32, a & 0xffffffff, b, c, d, e >> 32, e & 0xffffffff)
    if ev == 26:
        return "dpcd@%x len=%d LINK_BW_SET %#x -> %#x (port rate %d)" % (a, b, c, d, e)
    if ev == 27:
        return "req=%dx%d pclk=%d out=%dx%d empty=%d fallback=%d curMode=%x" % (a >> 32, a & 0xffffffff, b, c >> 32, c & 0xffffffff, d & 1, (d >> 1) & 1, e)
    if ev == 28:
        return "LINK_BW_SET %#x -> LINK_RATE_SET index %d (res=%x) lanes=%#x" % (a, b, c, d)
    if ev == 29:
        return "DPCD %#x max rate %#x -> %#x" % (a, b, c)
    if ev == 4:
        return "ddi=%d res=%x mode=%d conn=%d %s" % (a, b, c, d, link(e))
    if ev == 11:
        return "block=%d res=%x ddi=%d %s" % (a, b, c, link(d))
    if ev == 5:
        return "n=%d res=%x cmd=%x ctl=%08x buf=%08x" % (a, b, c, d, e)
    if ev in (6, 7, 9, 10):
        return "ddi=%d %s" % (a, link(c))
    if ev == 8:
        return "pattern=%d %s" % (a, link(c))
    if ev == 30:
        return "status=%d adopted=%d %s" % (a, b, link(c))
    return str((a, b, c, d, e))


def main():
    text = open(sys.argv[1]).read()
    m = re.search(r'"WEXPanelTrace" = <([0-9a-f]*)>', text)
    if not m:
        print("no trace")
        return
    raw = bytes.fromhex(m.group(1))
    t0 = None
    noise = re.compile(r"attr=(thrm| gdc|pcnt|lddc|asns|hddc)")
    for i in range(len(raw) // 56):
        tm, ev, a, b, c, d, e = struct.unpack_from("<7Q", raw, i * 56)
        t0 = tm if t0 is None else t0
        line = describe(ev, a, b, c, d, e)
        if "-v" not in sys.argv and ev == 1 and noise.search(line):
            continue
        print("%8.3f %-11s %s" % ((tm - t0) / 1e9, NAMES.get(ev, str(ev)), line))
    live(text)


def live(text):
    for key in ("WEXPanelLiveFirst", "WEXPanelLiveLast"):
        m = re.search(r'"%s" = <([0-9a-f]*)>' % key, text)
        if not m:
            continue
        raw = bytes.fromhex(m.group(1))
        q = struct.unpack("<%dQ" % (len(raw) // 8), raw)
        print(key, " ".join("%x=%08x" % (v >> 32, v & 0xffffffff) for v in q[2:]))


if __name__ == "__main__":
    main()
