/*
 * superstrikectl.c — command line front end for the Logitech G PRO X 2
 * SUPERSTRIKE driver library (ss2k.c). Protocol notes: docs/PROTOCOL.md.
 *
 * build:  make   (or: cc -O2 -Wall -std=c11 -o superstrikectl superstrikectl.c ss2k.c)
 */
#define _GNU_SOURCE
#include "ss2k.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>

static struct ss2k_dev dev_g = { .fd = -1 };

static void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "superstrikectl: ");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(2);
}

static void check(int rc, const char *what)
{
	if (rc)
		die("%s failed: %s", what, ss2k_strerror(rc));
}

/* ================================================================ options */
static int  opt_sensor = 0;
static int  opt_button = -1;
static int  opt_act = -1, opt_rt = -1, opt_rt_on = -1, opt_hap = -1;
static int  opt_dx = -1, opt_dy = -1, opt_lod = -1;
static int  opt_rate = -1;
static int  opt_wire = -1;
static int  opt_stage = -1;
static int  opt_seconds = 0;
static int  opt_yes = 0;
static int  opt_sector = -1;
static const char *opt_hidraw;

static void require_yes(void)
{
	if (!opt_yes) {
		fprintf(stderr, "refusing to write; pass --yes to acknowledge a live device change\n");
		exit(3);
	}
}

static const char *surface_lbl(int m)
{
	switch (m) {
	case SS2K_SURFACE_AUTO: return "auto";
	case SS2K_SURFACE_ON: return "on";
	case SS2K_SURFACE_OFF: return "off";
	default: return "unknown";
	}
}

static const char *mode_lbl(int m)
{
	switch (m) {
	case SS2K_MODE_ONBOARD: return "onboard (profiles in control)";
	case SS2K_MODE_HOST: return "host (software in control)";
	default: return "unknown";
	}
}

static const char *lod_lbl(int l)
{
	switch (l) {
	case 0: return "unused";
	case 1: return "low";
	case 2: return "medium";
	case 3: return "high";
	default: return "?";
	}
}

/* ================================================================ profile decode */
static const char *btn_behavior(const uint8_t *e, char *buf, size_t bn)
{
	uint16_t v = (uint16_t)((e[2] << 8) | e[3]);
	if (e[0] == 0xff) {
		snprintf(buf, bn, "default");
		return buf;
	}
	switch (e[0] & 0xf0) {
	case 0x00:
		snprintf(buf, bn, "macro(page %u offset %u)", e[1], e[3]);
		break;
	case 0x80:
		if (e[1] == 0x01) {
			static const char *names[] = { "L", "R", "M", "Back", "Fwd" };
			size_t o = 0;
			buf[0] = 0;
			o += (size_t)snprintf(buf + o, bn - o, "button(");
			for (int i = 0, first = 1; i < 16; i++) {
				if (!(v & (1u << i)))
					continue;
				if (!first)
					o += (size_t)snprintf(buf + o, bn - o, "+");
				if (i < 5)
					o += (size_t)snprintf(buf + o, bn - o, "%s", names[i]);
				else
					o += (size_t)snprintf(buf + o, bn - o, "B%d", i + 1);
				first = 0;
			}
			snprintf(buf + o, bn - o, ")");
		} else if (e[1] == 0x02) {
			snprintf(buf, bn, "key(mods=%02x usage=%02x)", e[2], e[3]);
		} else if (e[1] == 0x03) {
			snprintf(buf, bn, "consumer(%04x)", v);
		} else {
			snprintf(buf, bn, "none");
		}
		break;
	case 0x90:
		/* [0x9X][function id][ff][data] */
		switch (e[1]) {
		case 0x01: snprintf(buf, bn, "fn:wheel-mode"); break;
		case 0x03: snprintf(buf, bn, "fn:dpi-up"); break;
		case 0x04: snprintf(buf, bn, "fn:dpi-down"); break;
		case 0x05: snprintf(buf, bn, "fn:dpi-cycle"); break;
		case 0x06: snprintf(buf, bn, "fn:dpi-default"); break;
		case 0x07: snprintf(buf, bn, "fn:dpi-shift"); break;
		case 0x08: snprintf(buf, bn, "fn:profile-next"); break;
		case 0x09: snprintf(buf, bn, "fn:profile-prev"); break;
		case 0x0a: snprintf(buf, bn, "fn:profile-cycle"); break;
		case 0x0b: snprintf(buf, bn, "fn:g-shift"); break;
		case 0x0c: snprintf(buf, bn, "fn:battery-status"); break;
		case 0x0d: snprintf(buf, bn, "fn:profile-select(%u)", e[3]); break;
		default: snprintf(buf, bn, "fn:0x%02x(%u)", e[1], e[3]); break;
		}
		break;
	default:
		snprintf(buf, bn, "raw %02x %02x %02x %02x", e[0], e[1], e[2], e[3]);
	}
	return buf;
}

static void utf16_name(const uint8_t *p, size_t n, char *out, size_t on)
{
	size_t o = 0;
	for (size_t i = 0; i + 1 < n && o + 4 < on; i += 2) {
		unsigned c = p[i] | (p[i + 1] << 8);
		if (!c || c == 0xffff)
			break;
		if (c < 0x80) {
			out[o++] = (char)c;
		} else if (c < 0x800) {
			out[o++] = (char)(0xc0 | (c >> 6));
			out[o++] = (char)(0x80 | (c & 0x3f));
		} else {
			out[o++] = (char)(0xe0 | (c >> 12));
			out[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
			out[o++] = (char)(0x80 | (c & 0x3f));
		}
	}
	out[o] = 0;
}

static void profile_decode(const uint8_t *P, int len, int sector)
{
	printf("profile sector 0x%04x  (%d bytes, crc %s)\n", sector, len,
	       ss2k_sector_crc_ok(P, len) ? "ok" :
	       (P[len - 2] == 0xff && P[len - 1] == 0xff) ? "unset (factory)" : "MISMATCH");
	printf("  report rate:   wireless %u Hz, wired %u Hz  (idx %u/%u)\n",
	       ss2k_rate_hz[P[0] % 7], ss2k_rate_hz[P[1] % 7], P[0], P[1]);
	printf("  default stage: %u   shift stage: %u\n", P[2], P[3]);
	for (int s = 0; s < 5; s++) {
		const uint8_t *st = P + 4 + 5 * s;
		printf("    stage %d: %5u x %-5u lod=%-6s%s\n", s, st[0] | (st[1] << 8),
		       st[2] | (st[3] << 8), lod_lbl(st[4]), s == P[2] ? "  <= default" : "");
	}
	printf("  power mode %02x  angle snapping %02x  write counter %02x%02x\n",
	       P[0x21], P[0x22], P[0x23], P[0x24]);
	printf("  bunny-hop window: %u (%s)\n", P[0x25], P[0x25] ? "on" : "off");
	for (int b = 0; b < 2; b++) {
		const uint8_t *h = P + 0x26 + 3 * b;
		printf("  HITS %-5s: actuation %u  rapid-trigger %u (%s)  haptics %u\n",
		       b ? "right" : "left", h[0] >> 2, h[1] >> 2, (h[1] & 1) ? "on" : "off", h[2] >> 2);
	}
	printf("  power save after %us, off after %us\n",
	       P[0x2c] | (P[0x2d] << 8), P[0x2e] | (P[0x2f] << 8));
	char bb[96];
	printf("  buttons:\n");
	for (int i = 0; i < 16; i++) {
		const uint8_t *e = P + 0x30 + 4 * i;
		if (e[0] == 0xff && e[1] == 0xff)
			continue;
		printf("    %2d: %s\n", i, btn_behavior(e, bb, sizeof(bb)));
	}
	printf("  g-shift buttons:\n");
	for (int i = 0; i < 12; i++) {
		const uint8_t *e = P + 0x70 + 4 * i;
		if (e[0] == 0xff && e[1] == 0xff)
			continue;
		printf("    %2d: %s\n", i, btn_behavior(e, bb, sizeof(bb)));
	}
	char nm[100];
	utf16_name(P + 0xa0, 48, nm, sizeof(nm));
	printf("  name: \"%s\"\n", nm);
}

/* ================================================================ verbs */
static unsigned top_hz(uint16_t mask)
{
	unsigned hz = 0;
	for (int b = 0; b < 7; b++)
		if (mask & (1u << b))
			hz = ss2k_rate_hz[b];
	return hz;
}

static void cmd_status(void)
{
	struct ss2k_dev *d = &dev_g;
	printf("device:        %s\n", d->name[0] ? d->name : "(unnamed)");
	printf("hidraw:        %s  (%s, device index 0x%02x)\n", d->path,
	       d->wired ? "wired" : "receiver", d->dev_index);
	printf("protocol:      HID++ %d.%d\n", d->proto_major, d->proto_minor);
	if (d->model[0])
		printf("model/unit:    %s  unit-id %s\n", d->model, d->serial);
	if (d->fw_main[0])
		printf("firmware:      %s  (bootloader %s)\n", d->fw_main, d->fw_boot);
	printf("features:      %d\n", d->feats.count);

	struct ss2k_battery b;
	if (!ss2k_get_battery(d, &b))
		printf("battery:       %d%%  %s\n", b.percent, ss2k_charging_label(b.charging));

	int mode = -1;
	if (!ss2k_onboard_mode_get(d, &mode))
		printf("onboard mode:  %s\n", mode_lbl(mode));

	uint16_t wmask = 0, lmask = 0;
	int ri = -1;
	if (!ss2k_rate_lists(d, &wmask, &lmask) && !ss2k_rate_get(d, &ri) && ri >= 0 && ri < 7)
		printf("report rate:   %u Hz  (wired max %u Hz, lightspeed max %u Hz)\n",
		       ss2k_rate_hz[ri], top_hz(wmask), top_hz(lmask));

	struct ss2k_dpi_sensor sen;
	if (!ss2k_dpi_query(d, opt_sensor, &sen))
		printf("sensor dpi:    %d x %d  lod %s  (supported %d..%d)\n",
		       sen.x_cur, sen.y_cur, lod_lbl(sen.lod), sen.min, sen.max);
	int stage = -1;
	if (!ss2k_dpi_stage_get(d, &stage))
		printf("dpi stage:     %d\n", stage);

	int surf, raw;
	if (!ss2k_surface_get(d, &surf, &raw))
		printf("surface mode:  %s\n", surface_lbl(surf));
	int bw;
	if (!ss2k_bhop_get(d, &bw)) {
		if (bw)
			printf("bunny-hop:     on, window %d ms\n", bw * 10);
		else
			printf("bunny-hop:     off\n");
	}

	int c;
	if (!ss2k_hits_caps(d, &c, NULL, NULL, NULL)) {
		for (int i = 0; i < c; i++) {
			struct ss2k_hits_btn hb;
			if (!ss2k_hits_get(d, i, &hb))
				printf("HITS %-5s:    actuation %d/10  rapid-trigger %d/5 (%s)  haptics %d/5\n",
				       i ? "right" : "left", hb.actuation, hb.rapid,
				       hb.rapid_enabled ? "on" : "off", hb.haptics);
		}
	}
	int sect = -1;
	if (!ss2k_profile_current(d, &sect))
		printf("profile:       sector 0x%04x%s\n", sect,
		       mode == SS2K_MODE_HOST ? " (inactive: host mode)" : "");
}

static void cmd_probe(void)
{
	printf("# %d features on %s (protocol %d.%d)\n",
	       dev_g.feats.count, dev_g.path, dev_g.proto_major, dev_g.proto_minor);
	for (int i = 0; i < dev_g.feats.count; i++)
		printf("%2d  0x%04x\n", i, dev_g.feats.ids[i]);
}

static void cmd_profile(const char *mode, const char *pos)
{
	if (!strcmp(mode, "dir")) {
		uint8_t S[SS2K_SECTOR_MAX];
		int len = 0;
		check(ss2k_profile_read(&dev_g, 0x0000, S, &len), "directory read");
		int cur = -1;
		ss2k_profile_current(&dev_g, &cur);
		printf("profile directory (sector 0, crc %s):\n",
		       ss2k_sector_crc_ok(S, len) ? "ok" : "bad/unset");
		for (int i = 0; i * 4 + 4 <= len - 2; i++) {
			const uint8_t *e = S + 4 * i;
			if (e[0] == 0xff && e[1] == 0xff)
				break;
			int sec = (e[0] << 8) | e[1];
			printf("  slot %d: sector 0x%04x  %s%s\n", i + 1, sec,
			       e[2] ? "enabled " : "disabled", sec == cur ? "  <= current" : "");
		}
		return;
	}
	if (!strcmp(mode, "activate")) {
		require_yes();
		int sector = pos ? (int)strtol(pos, NULL, 0) : opt_sector;
		if (sector <= 0)
			die("profile activate needs a sector number >= 1 (see `profile dir`)");
		check(ss2k_profile_activate(&dev_g, sector), "activate");
		int cur = -1;
		ss2k_profile_current(&dev_g, &cur);
		printf("activated sector 0x%04x (current now 0x%04x)\n", sector, cur);
		return;
	}
	if (strcmp(mode, "info") && strcmp(mode, "dump"))
		die("profile: dir | info | dump | activate");
	int sector = pos ? (int)strtol(pos, NULL, 0) : opt_sector;
	if (sector < 0)
		check(ss2k_profile_current(&dev_g, &sector), "current profile query");
	uint8_t P[SS2K_SECTOR_MAX];
	int len = 0;
	check(ss2k_profile_read(&dev_g, sector, P, &len), "profile read");
	if (!strcmp(mode, "dump")) {
		for (int o = 0; o < len; o += 16)
			printf("%3d: %s\n", o, ss2k_hex(P + o, (size_t)(len - o < 16 ? len - o : 16)));
		return;
	}
	profile_decode(P, len, sector);
}

static uint64_t mono_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static void cmd_monitor(void)
{
	uint64_t start = mono_ms();
	uint64_t end = opt_seconds > 0 ? start + (uint64_t)opt_seconds * 1000 : ~0ull;
	fprintf(stderr, "monitoring %s(Ctrl-C to stop)\n", opt_seconds > 0 ? "" : "continuously ");
	struct pollfd p = { .fd = dev_g.fd, .events = POLLIN };
	uint8_t buf[64];
	unsigned long ninput = 0, nhidpp = 0;
	const struct ss2k_features *F = &dev_g.feats;
	while (mono_ms() < end) {
		if (poll(&p, 1, 200) <= 0)
			continue;
		ssize_t g = read(dev_g.fd, buf, sizeof(buf));
		if (g <= 0)
			continue;
		double ts = (double)(mono_ms() - start) / 1e3;
		if (buf[0] != 0x10 && buf[0] != 0x11) {
			if (ninput++ % 400 == 0)
				printf("[%8.3f] input report #%lu: %s\n", ts, ninput, ss2k_hex(buf, (size_t)g));
			continue;
		}
		nhidpp++;
		uint8_t fi = buf[2];
		const char *lbl = fi == 0x00 ? "root" : fi == 0xff ? "ERROR" :
			fi == F->idx_dpi ? "DPI" : fi == F->idx_hits ? "HITS" :
			fi == F->idx_spy ? "SPY" : fi == F->idx_xystats ? "XY" :
			fi == F->idx_wheelstats ? "WHEEL" : fi == F->idx_batt ? "BATT" :
			fi == F->idx_config ? "CFG" : fi == F->idx_modestatus ? "MODE" :
			fi == F->idx_rate ? "RATE" : fi == F->idx_onboard ? "ONBOARD" : "?";
		printf("[%8.3f] %-8s feat=%02x fn%x sw%x: %s\n", ts, lbl, fi, buf[3] >> 4,
		       buf[3] & 15, ss2k_hex(buf + 4, (size_t)g - 4));
		fflush(stdout);
	}
	fprintf(stderr, "done: %lu input reports, %lu HID++ frames\n", ninput, nhidpp);
}

static void cmd_hits(const char *mode)
{
	int c;
	check(ss2k_hits_caps(&dev_g, &c, NULL, NULL, NULL), "HITS caps");
	if (!strcmp(mode, "get")) {
		for (int btn = 0; btn < c; btn++) {
			struct ss2k_hits_btn hb;
			check(ss2k_hits_get(&dev_g, btn, &hb), "HITS read");
			printf("button %d (%s): actuation=%d/10 rapid-trigger=%d/5 (%s) haptics=%d/5\n",
			       btn, btn ? "right" : "left", hb.actuation, hb.rapid,
			       hb.rapid_enabled ? "on" : "off", hb.haptics);
		}
		return;
	}
	if (strcmp(mode, "set"))
		die("hits: get | set");
	require_yes();
	if ((opt_act >= 0 && (opt_act < 1 || opt_act > 10)) ||
	    (opt_rt >= 0 && (opt_rt < 1 || opt_rt > 5)) ||
	    (opt_hap >= 0 && opt_hap > 5) || (opt_rt_on > 1))
		die("ranges: --act 1..10, --rt 1..5, --rt-on 0|1, --hap 0..5");
	int first = opt_button < 0 ? 0 : opt_button;
	int last = opt_button < 0 ? c - 1 : opt_button;
	if (first >= c)
		die("button index %d unreachable (usable: 0..%d)", first, c - 1);
	for (int btn = first; btn <= last; btn++) {
		check(ss2k_hits_set(&dev_g, btn, opt_act, opt_rt, opt_rt_on, opt_hap), "HITS write");
		struct ss2k_hits_btn hb;
		check(ss2k_hits_get(&dev_g, btn, &hb), "HITS verify read");
		printf("button %d now: actuation=%d rapid-trigger=%d (%s) haptics=%d\n", btn,
		       hb.actuation, hb.rapid, hb.rapid_enabled ? "on" : "off", hb.haptics);
		if ((opt_act >= 0 && hb.actuation != opt_act) || (opt_rt >= 0 && hb.rapid != opt_rt) ||
		    (opt_rt_on >= 0 && hb.rapid_enabled != opt_rt_on) ||
		    (opt_hap >= 0 && hb.haptics != opt_hap)) {
			fprintf(stderr, "warning: read-back mismatch\n");
			exit(4);
		}
	}
}

static void cmd_dpi(const char *mode)
{
	struct ss2k_dpi_sensor s;
	check(ss2k_dpi_query(&dev_g, opt_sensor, &s), "dpi query");
	if (!strcmp(mode, "get")) {
		printf("sensor %d: %d x %d  (default %d x %d)  lod %s  supported %d..%d\n",
		       s.index, s.x_cur, s.y_cur, s.x_def, s.y_def, lod_lbl(s.lod), s.min, s.max);
		int stage;
		if (!ss2k_dpi_stage_get(&dev_g, &stage))
			printf("active profile stage: %d\n", stage);
		return;
	}
	if (!strcmp(mode, "stage")) {
		require_yes();
		if (opt_stage < 0 || opt_stage > 4)
			die("dpi stage --stage 0..4");
		check(ss2k_dpi_stage_set(&dev_g, opt_stage), "dpi stage set");
		printf("selected stage %d of the active profile\n", opt_stage);
		return;
	}
	if (strcmp(mode, "set"))
		die("dpi: get | set | stage");
	require_yes();
	int dx = opt_dx > 0 ? opt_dx : s.x_cur;
	int dy = opt_dy > 0 ? opt_dy : (opt_dx > 0 ? opt_dx : s.y_cur);
	if (!ss2k_dpi_valid(&dev_g, opt_sensor, dx) || !ss2k_dpi_valid(&dev_g, opt_sensor, dy))
		die("dpi %d x %d is not in the sensor's list (%d..%d, stepped)", dx, dy, s.min, s.max);
	if (opt_lod > 3)
		die("--lod 1..3 (low, medium, high)");
	int mode_now = 0;
	ss2k_onboard_mode_get(&dev_g, &mode_now);
	check(ss2k_dpi_set(&dev_g, opt_sensor, dx, dy, opt_lod), "dpi set");
	printf("set %d x %d\n", dx, dy);
	if (mode_now == SS2K_MODE_ONBOARD)
		printf("note: onboard mode is active, so the profile stage governs; switch with\n"
		       "      `mode host --yes` for the live setter to apply, or use `dpi stage`\n");
}

static void cmd_rate(const char *mode)
{
	if (!strcmp(mode, "get")) {
		uint16_t w, l, cur_mask;
		check(ss2k_rate_lists(&dev_g, &w, &l), "rate lists");
		printf("wired     :");
		for (int b = 0; b < 7; b++)
			if (w & (1u << b)) printf(" %u", ss2k_rate_hz[b]);
		printf(" Hz\nlightspeed:");
		for (int b = 0; b < 7; b++)
			if (l & (1u << b)) printf(" %u", ss2k_rate_hz[b]);
		printf(" Hz\n");
		if (!ss2k_rate_supported(&dev_g, &cur_mask))
			printf("this link : mask %#06x\n", cur_mask);
		int ri;
		check(ss2k_rate_get(&dev_g, &ri), "rate get");
		if (ri >= 0 && ri < 7)
			printf("current   : %u Hz (%s)  [getter may be stale after writes]\n",
			       ss2k_rate_hz[ri], ss2k_rate_label[ri]);
		return;
	}
	if (strcmp(mode, "set"))
		die("rate: get | set");
	require_yes();
	if (opt_rate < 0 || opt_rate > 6)
		die("--rate takes index 0..6 (0=125Hz .. 6=8000Hz)");
	uint16_t mask = 0x7f;
	if (!ss2k_rate_supported(&dev_g, &mask) && !(mask & (1u << opt_rate)))
		die("%u Hz is not supported on this link", ss2k_rate_hz[opt_rate]);
	check(ss2k_rate_set(&dev_g, opt_rate), "rate set");
	printf("set %u Hz on the current link (volatile; the profile byte persists)\n",
	       ss2k_rate_hz[opt_rate]);
}

static void cmd_surface(const char *mode, const char *pos)
{
	if (!strcmp(mode, "get")) {
		int m, raw;
		check(ss2k_surface_get(&dev_g, &m, &raw), "surface get");
		printf("surface mode: %s  (status byte 0x%02x)\n", surface_lbl(m), raw);
		return;
	}
	if (strcmp(mode, "set"))
		die("surface: get | set auto|on|off");
	require_yes();
	const char *want = pos ? pos : "";
	int m = !strcmp(want, "auto") ? SS2K_SURFACE_AUTO : !strcmp(want, "on") ? SS2K_SURFACE_ON :
		!strcmp(want, "off") ? SS2K_SURFACE_OFF : -1;
	if (m < 0)
		die("surface set auto|on|off");
	check(ss2k_surface_set(&dev_g, m), "surface set");
	int now;
	if (!ss2k_surface_get(&dev_g, &now, NULL))
		printf("now: %s\n", surface_lbl(now));
}

static void cmd_bhop(const char *mode)
{
	if (!strcmp(mode, "get")) {
		int w;
		check(ss2k_bhop_get(&dev_g, &w), "bhop get");
		if (w)
			printf("bunny-hop: on, window %d ms\n", w * 10);
		else
			printf("bunny-hop: off\n");
		return;
	}
	if (strcmp(mode, "set"))
		die("bhop: get | set --wire N");
	require_yes();
	if (opt_wire < 0 || (opt_wire != 0 && (opt_wire < 10 || opt_wire > 100)))
		die("--wire 0 (off) or 10..100 (=> 100..1000 ms)");
	check(ss2k_bhop_set(&dev_g, opt_wire), "bhop set");
	int now;
	if (!ss2k_bhop_get(&dev_g, &now))
		printf("now: %d (%d ms)\n", now, now * 10);
}

static void cmd_mode(const char *mode)
{
	if (!mode || !strcmp(mode, "get")) {
		int m;
		check(ss2k_onboard_mode_get(&dev_g, &m), "mode get");
		printf("%s\n", mode_lbl(m));
		return;
	}
	require_yes();
	int m = !strcmp(mode, "onboard") ? SS2K_MODE_ONBOARD : !strcmp(mode, "host") ? SS2K_MODE_HOST : -1;
	if (m < 0)
		die("mode: get | onboard | host");
	check(ss2k_onboard_mode_set(&dev_g, m), "mode set");
	printf("%s\n", mode_lbl(m));
}

static void usage(void)
{
	printf(
	"superstrikectl — Linux control for Logitech G PRO X 2 SUPERSTRIKE\n"
	"\n"
	"verbs:\n"
	"  status                     identity, battery, rate, dpi, HITS, profile\n"
	"  probe                      HID++ feature table\n"
	"  monitor                    live frame dump (--seconds N, Ctrl-C to stop)\n"
	"  battery                    battery percent + charging state\n"
	"  hits get | set             analog keys: actuation / rapid trigger / haptics\n"
	"  dpi get | set | stage      sensor DPI (live setter) or active profile stage\n"
	"  rate get | set             polling rate of the current link\n"
	"  surface get | set MODE     gaming-surface mode: auto | on | off\n"
	"  bhop get | set             bunny-hop scroll filter\n"
	"  mode [get|onboard|host]    onboard-profile vs host (software) control\n"
	"  profile dir|info|dump [SECTOR] | activate SECTOR\n"
	"  cookie                     config-change cookie\n"
	"\n"
	"options:\n"
	"  --yes                      acknowledge device writes (required for setters)\n"
	"  --hidraw DEV               explicit device node (default autodetect)\n"
	"  --button N                 HITS button 0=left 1=right (default both)\n"
	"  --act N --rt N --hap N     HITS values 1..10, 1..5, 0..5\n"
	"  --rt-on 0|1                rapid trigger off/on\n"
	"  --sensor N --x N --y N     DPI operands\n"
	"  --lod N                    lift-off distance 1=low 2=medium 3=high\n"
	"  --stage N                  DPI stage 0..4 of the active profile\n"
	"  --rate N                   0..6 = 125, 250, 500, 1000, 2000, 4000, 8000 Hz\n"
	"  --wire N                   bunny-hop window in 10 ms units (0=off, 10..100)\n"
	"  --sector N                 profile sector (default: current)\n"
	"  --seconds N                monitor duration\n"
	"\n"
	"examples:\n"
	"  superstrikectl hits set --button 0 --act 7 --rt 3 --rt-on 1 --hap 5 --yes\n"
	"  superstrikectl rate set --rate 6 --yes        # 8 kHz (LIGHTSPEED only)\n"
	"  superstrikectl dpi stage --stage 3 --yes\n"
	"  superstrikectl surface set on --yes\n"
	"  superstrikectl profile activate 1 --yes\n");
}

static int intarg(const char *s, const char *opt)
{
	char *end;
	long v = strtol(s, &end, 0);
	if (*s == 0 || *end)
		die("%s expects a number, got '%s'", opt, s);
	return (int)v;
}

int main(int argc, char **argv)
{
	const char *pos[3] = { 0 };
	int npos = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		int has = i + 1 < argc;
		if (a[0] != '-') {
			if (npos < 3)
				pos[npos++] = a;
			else
				die("unexpected argument '%s'", a);
		}
#define NUMOPT(name, var) else if (!strcmp(a, name) && has) var = intarg(argv[++i], name)
		NUMOPT("--button", opt_button);
		NUMOPT("--act", opt_act);
		NUMOPT("--rt", opt_rt);
		NUMOPT("--rt-on", opt_rt_on);
		NUMOPT("--hap", opt_hap);
		NUMOPT("--sensor", opt_sensor);
		NUMOPT("--x", opt_dx);
		NUMOPT("--y", opt_dy);
		NUMOPT("--lod", opt_lod);
		NUMOPT("--stage", opt_stage);
		NUMOPT("--rate", opt_rate);
		NUMOPT("--wire", opt_wire);
		NUMOPT("--sector", opt_sector);
		NUMOPT("--seconds", opt_seconds);
#undef NUMOPT
		else if (!strcmp(a, "--hidraw") && has) opt_hidraw = argv[++i];
		else if (!strcmp(a, "--yes")) opt_yes = 1;
		else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
		else {
			fprintf(stderr, "unknown or incomplete option '%s'\n\n", a);
			usage();
			return 2;
		}
	}
	const char *verb = pos[0];
	if (!verb || !strcmp(verb, "help")) {
		usage();
		return 0;
	}
	const char *sub = pos[1];

	int rc = ss2k_connect(&dev_g, opt_hidraw);
	if (rc == -ENODEV)
		die("no PRO X2 SUPERSTRIKE found (paired and awake? or pass --hidraw)");
	if (rc == -EACCES)
		die("permission denied on the hidraw node; install udev/99-superstrike.rules");
	check(rc, "connect");

	if (!strcmp(verb, "status")) {
		cmd_status();
	} else if (!strcmp(verb, "probe")) {
		cmd_probe();
	} else if (!strcmp(verb, "monitor")) {
		if (sub)
			opt_seconds = intarg(sub, "monitor");
		cmd_monitor();
	} else if (!strcmp(verb, "battery")) {
		struct ss2k_battery b;
		check(ss2k_get_battery(&dev_g, &b), "battery read");
		printf("%d%% %s\n", b.percent, ss2k_charging_label(b.charging));
	} else if (!strcmp(verb, "cookie")) {
		uint16_t ck;
		check(ss2k_config_cookie(&dev_g, &ck), "cookie read");
		printf("0x%04x\n", ck);
	} else if (!strcmp(verb, "hits")) {
		cmd_hits(sub ? sub : "get");
	} else if (!strcmp(verb, "dpi")) {
		cmd_dpi(sub ? sub : "get");
	} else if (!strcmp(verb, "rate")) {
		cmd_rate(sub ? sub : "get");
	} else if (!strcmp(verb, "surface")) {
		cmd_surface(sub ? sub : "get", pos[2]);
	} else if (!strcmp(verb, "bhop")) {
		cmd_bhop(sub ? sub : "get");
	} else if (!strcmp(verb, "mode")) {
		cmd_mode(sub);
	} else if (!strcmp(verb, "profile")) {
		cmd_profile(sub ? sub : "info", pos[2]);
	} else {
		fprintf(stderr, "unknown verb '%s'\n\n", verb);
		usage();
		return 2;
	}
	ss2k_close(&dev_g);
	return 0;
}
