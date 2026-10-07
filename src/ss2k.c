/*
 * ss2k.c — HID++ transport, discovery and typed feature operations for the
 * Logitech G PRO X 2 SUPERSTRIKE. Shared by the CLI (superstrikectl) and the
 * GUI (via libss2k.so). Never exits the process; see ss2k.h for the return
 * convention.
 */
#define _GNU_SOURCE
#include "ss2k.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <poll.h>
#include <time.h>
#include <ctype.h>
#include <sys/file.h>

const char *ss2k_hex(const uint8_t *b, size_t n)
{
	static char buf[256];
	size_t o = 0;
	buf[0] = 0;
	for (size_t i = 0; i < n && o < sizeof(buf) - 4; i++)
		o += (size_t)snprintf(buf + o, sizeof(buf) - o, "%02x ", b[i]);
	return buf;
}

const char *ss2k_strerror(int rc)
{
	static char buf[64];
	if (rc < 0) {
		if (rc == -ETIMEDOUT)
			return "no answer (mouse asleep or out of range?)";
		return strerror(-rc);
	}
	if (rc & SS2K_E_HIDPP10) {
		snprintf(buf, sizeof(buf), "receiver error 0x%02x (device unreachable?)", rc & 0xff);
		return buf;
	}
	switch (rc) {
	case 0: return "ok";
	case 1: return "unknown";
	case 2: return "invalid argument";
	case 3: return "out of range";
	case 4: return "hardware error";
	case 5: return "logitech internal";
	case 6: return "invalid feature index";
	case 7: return "invalid function";
	case 8: return "busy";
	case 9: return "unsupported";
	default:
		snprintf(buf, sizeof(buf), "HID++ error 0x%02x", rc);
		return buf;
	}
}

/* ================================================================ transport */
/*
 * Software ids 2..15. 0 marks device notifications (the kernel would parse a
 * swid-0 reply as an event, e.g. a fake battery update) and 1 is the kernel
 * hid-logitech-hidpp driver's own id, so both are avoided.
 */
static uint8_t sw_id_seq;

static uint8_t next_swid(void)
{
	sw_id_seq = (uint8_t)(sw_id_seq >= 15 ? 2 : (sw_id_seq < 2 ? 2 : sw_id_seq + 1));
	return sw_id_seq;
}

static uint64_t mono_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

/* Discard whatever is already queued. Never waits: the mouse streams input
 * reports continuously while it moves, so a timed drain could spin forever. */
static void drain_fd(int fd)
{
	uint8_t buf[64];
	while (read(fd, buf, sizeof(buf)) > 0)
		;
}

static int hidpp_xfer(struct ss2k_dev *d, const uint8_t *req, size_t flen,
		     uint8_t *out, size_t *olen, int timeout_ms)
{
	int dbg = getenv("SS2K_DEBUG") != NULL;
	drain_fd(d->fd);
	ssize_t w = write(d->fd, req, flen);
	if (w < 0)
		return -errno;
	if (dbg)
		fprintf(stderr, "tx %2zu: %s\n", flen, ss2k_hex(req, flen));

	uint64_t deadline = mono_ms() + (uint64_t)timeout_ms;
	struct pollfd p = { .fd = d->fd, .events = POLLIN };
	for (;;) {
		uint64_t now = mono_ms();
		if (now >= deadline)
			return -ETIMEDOUT;
		int r = poll(&p, 1, (int)(deadline - now));
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return r < 0 ? -errno : -ETIMEDOUT;
		if (p.revents & (POLLERR | POLLHUP))
			return -ENODEV;
		uint8_t rsp[64];
		ssize_t got = read(d->fd, rsp, sizeof(rsp));
		if (got < 0 && errno == ENODEV)
			return -ENODEV;
		if (got < 7)
			continue;
		if (rsp[0] != SS2K_RID_SHORT && rsp[0] != SS2K_RID_LONG)
			continue;
		/* dev_index 0 = not learned yet (first ping) */
		if (d->dev_index && rsp[1] != d->dev_index)
			continue;
		/* HID++ 2.0 error: [rid][dev][0xff][feat][fn|sw][code] */
		if (rsp[2] == 0xff && rsp[3] == req[2] && rsp[4] == req[3]) {
			if (dbg)
				fprintf(stderr, "rx err: %s\n", ss2k_hex(rsp, (size_t)got));
			return rsp[5] ? rsp[5] : SS2K_E_UNKNOWN;
		}
		/* HID++ 1.0 error from the receiver: [10][dev][8f][feat][fn|sw][code] */
		if (rsp[2] == 0x8f && rsp[3] == req[2] && rsp[4] == req[3]) {
			if (dbg)
				fprintf(stderr, "rx err: %s\n", ss2k_hex(rsp, (size_t)got));
			return SS2K_E_HIDPP10 | rsp[5];
		}
		if (rsp[2] != req[2] || rsp[3] != req[3])
			continue;
		if (dbg)
			fprintf(stderr, "rx   : %s\n", ss2k_hex(rsp, (size_t)got));
		if (!d->dev_index)
			d->dev_index = rsp[1];
		size_t avail = (size_t)got - 4;
		if (avail > 16)
			avail = 16;
		if (out && olen) {
			size_t cp = avail < *olen ? avail : *olen;
			memcpy(out, rsp + 4, cp);
			*olen = cp;
		}
		return SS2K_OK;
	}
}

int ss2k_call(struct ss2k_dev *d, uint8_t fidx, uint8_t fn,
	      const uint8_t *params, size_t plen,
	      uint8_t *out, size_t *olen, int long_wanted)
{
	if (d->fd < 0)
		return -ENODEV;
	if (plen > 16 || fn > 15)
		return -EINVAL;

	/* requests with more than 3 params must use the long report */
	int use_long = long_wanted || plen > 3;
	size_t flen = use_long ? SS2K_LONG_LEN : SS2K_SHORT_LEN;
	uint8_t req[SS2K_LONG_LEN] = { 0 };
	req[0] = use_long ? SS2K_RID_LONG : SS2K_RID_SHORT;
	req[1] = d->dev_index ? d->dev_index : (d->wired ? 0xff : 0x01);
	req[2] = fidx;
	if (plen)
		memcpy(req + 4, params, plen);

	/* Serialise with other processes on this node (GUI + tray + CLI): software
	 * ids only disambiguate within one process. Advisory, never fatal. */
	int locked = flock(d->fd, LOCK_EX) == 0;

	/* the radio link can drop a frame while waking; retry once */
	int rc = -ETIMEDOUT;
	for (int attempt = 0; attempt < 2; attempt++) {
		req[3] = (uint8_t)((fn << 4) | next_swid());
		size_t ol = olen ? *olen : 0;
		rc = hidpp_xfer(d, req, flen, out, olen ? &ol : NULL, 700);
		if (rc == SS2K_OK && olen)
			*olen = ol;
		if (rc != -ETIMEDOUT && rc != SS2K_E_BUSY)
			break;
		usleep(30 * 1000);
	}
	if (locked)
		flock(d->fd, LOCK_UN);
	return rc;
}

int ss2k_ping(struct ss2k_dev *d)
{
	uint8_t out[16];
	size_t ol = sizeof(out);
	int rc = ss2k_call(d, 0x00, 0x01, (const uint8_t[]) { 0, 0, 0x5a }, 3, out, &ol, 0);
	if (rc)
		return rc;
	d->proto_major = out[0];
	d->proto_minor = out[1];   /* live: 04 02 = HID++ 4.2 */
	return 0;
}

/* ================================================================ discovery */
static int read_hid_id(const char *hidraw, unsigned *vid, unsigned *pid)
{
	char path[300], line[256];
	snprintf(path, sizeof(path), "/sys/class/hidraw/%s/device/uevent", hidraw);
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	int ok = -1;
	while (fgets(line, sizeof(line), f)) {
		unsigned bus;
		if (sscanf(line, "HID_ID=%x:%x:%x", &bus, vid, pid) == 3) {
			ok = 0;
			break;
		}
	}
	fclose(f);
	return ok;
}

static int try_node(struct ss2k_dev *d, const char *path, int wired)
{
	d->fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (d->fd < 0)
		return -errno;
	snprintf(d->path, sizeof(d->path), "%s", path);
	d->wired = wired;
	d->dev_index = 0;
	/* a sleeping mouse can take a few hundred ms to come back on the radio */
	int rc = ss2k_ping(d);
	for (int i = 0; i < 2 && (rc == -ETIMEDOUT || (rc & SS2K_E_HIDPP10)); i++) {
		usleep(250 * 1000);
		rc = ss2k_ping(d);
	}
	if (rc) {
		close(d->fd);
		d->fd = -1;
	}
	return rc;
}

static int autodetect(struct ss2k_dev *d)
{
	DIR *dir = opendir("/sys/class/hidraw");
	if (!dir)
		return -errno;
	int best = -ENODEV;
	struct dirent *e;
	/* two passes: paired-over-receiver first, then wired */
	for (int pass = 0; pass < 2 && d->fd < 0; pass++) {
		rewinddir(dir);
		while ((e = readdir(dir))) {
			if (strncmp(e->d_name, "hidraw", 6))
				continue;
			unsigned vid, pid;
			if (read_hid_id(e->d_name, &vid, &pid) || vid != SS2K_LOGI_VID)
				continue;
			if (pid != (pass ? SS2K_WIRED_PID : SS2K_DEV_PID))
				continue;
			char dev[300];
			snprintf(dev, sizeof(dev), "/dev/%s", e->d_name);
			int rc = try_node(d, dev, pass);
			if (!rc) {
				best = 0;
				break;
			}
			/* remember the most informative failure (EACCES beats ENODEV) */
			if (best == -ENODEV || rc == -EACCES)
				best = rc;
		}
	}
	closedir(dir);
	return best;
}

static int fs_walk(struct ss2k_dev *d)
{
	uint8_t out[16];
	size_t ol = sizeof(out);
	const uint8_t q[2] = { FID_FEATURE_SET >> 8, FID_FEATURE_SET & 0xff };
	int rc = ss2k_call(d, 0x00, 0x00, q, 2, out, &ol, 0);
	if (rc)
		return rc;
	uint8_t fs = out[0];
	if (!fs)
		return -ENOTSUP;
	ol = sizeof(out);
	if ((rc = ss2k_call(d, fs, 0x00, NULL, 0, out, &ol, 0)))
		return rc;
	int count = out[0];      /* excludes root, so indexes run 0..count */
	struct ss2k_features *F = &d->feats;
	memset(F, 0, sizeof(*F));
	for (int i = 0; i <= count && i < 64; i++) {
		uint8_t pi = (uint8_t)i;
		ol = sizeof(out);
		memset(out, 0, sizeof(out));
		if ((rc = ss2k_call(d, fs, 0x01, &pi, 1, out, &ol, 0)))
			return rc;
		/* [fid_hi][fid_lo][flags][version]; ordinal == index */
		uint16_t fid = (uint16_t)((out[0] << 8) | out[1]);
		F->ids[F->count++] = fid;
		uint8_t idx = (uint8_t)i;
		switch (fid) {
		case FID_FEATURE_SET: F->idx_featureset = idx; break;
		case FID_DEVICE_INFO: F->idx_devinfo = idx; break;
		case FID_DEVICE_NAME: F->idx_name = idx; break;
		case FID_CONFIG_CHANGE: F->idx_config = idx; break;
		case FID_UNIFIED_BATT: F->idx_batt = idx; break;
		case FID_EXT_ADJ_DPI: F->idx_dpi = idx; break;
		case FID_XY_STATS: F->idx_xystats = idx; break;
		case FID_WHEEL_STATS: F->idx_wheelstats = idx; break;
		case FID_MODE_STATUS: F->idx_modestatus = idx; break;
		case FID_ANALOG_BUTTONS: F->idx_hits = idx; break;
		case FID_EXT_ADJ_RATE: F->idx_rate = idx; break;
		case FID_ONBOARD_PROFILES: F->idx_onboard = idx; break;
		case FID_BUTTON_SPY: F->idx_spy = idx; break;
		case FID_FORCE_PAIRING: F->idx_forcepair = idx; break;
		case FID_ADC_MEASURE: F->idx_adc = idx; break;
		case FID_BUNNY_HOP: F->idx_bhop = idx; break;
		default: break;
		}
	}
	return 0;
}

static void ascii_fix(char *s)
{
	for (; *s; s++)
		if (!isprint((unsigned char)*s))
			*s = '.';
}

/* DeviceInfo fn1 entry: [type][prefix 3 ASCII][number BCD][revision BCD][build BE16] */
static void fw_string(const uint8_t *o, char *dst, size_t n)
{
	char pfx[4] = { (char)o[1], (char)o[2], (char)o[3], 0 };
	ascii_fix(pfx);
	snprintf(dst, n, "%s %02x.%02x.B%04x", pfx, o[4], o[5], (o[6] << 8) | o[7]);
}

static void read_identity(struct ss2k_dev *d)
{
	uint8_t out[16];
	size_t ol;
	if (d->feats.idx_name) {
		ol = sizeof(out);
		int len = 0;
		if (!ss2k_call(d, d->feats.idx_name, 0x00, NULL, 0, out, &ol, 0) && ol)
			len = out[0] > 120 ? 120 : out[0];
		for (int off = 0; off < len; off += 16) {
			uint8_t p = (uint8_t)off;
			ol = sizeof(out);
			memset(out, 0, sizeof(out));
			if (ss2k_call(d, d->feats.idx_name, 0x01, &p, 1, out, &ol, 0)) {
				len = off;
				break;
			}
			int take = (len - off) > 16 ? 16 : (len - off);
			memcpy(d->name + off, out, (size_t)take);
		}
		d->name[len] = 0;
		ascii_fix(d->name);
	}
	if (d->feats.idx_devinfo) {
		/* fn0: [entities][unitId 4][transport 2][modelId 6][extModel][caps] */
		ol = sizeof(out);
		if (!ss2k_call(d, d->feats.idx_devinfo, 0x00, NULL, 0, out, &ol, 0) && ol >= 5)
			snprintf(d->serial, sizeof(d->serial), "%02x-%02x-%02x-%02x",
				 out[1], out[2], out[3], out[4]);
		ol = sizeof(out);
		memset(out, 0, sizeof(out));
		if (!ss2k_call(d, d->feats.idx_devinfo, 0x02, NULL, 0, out, &ol, 0) && ol >= 12) {
			memcpy(d->model, out, 12);
			d->model[12] = 0;
			ascii_fix(d->model);
		}
		for (uint8_t ent = 0; ent < 2; ent++) {
			ol = sizeof(out);
			memset(out, 0, sizeof(out));
			if (ss2k_call(d, d->feats.idx_devinfo, 0x01, &ent, 1, out, &ol, 0) || ol < 8)
				continue;
			/* type 0 = main application, 1 = bootloader */
			if (out[0] == 0)
				fw_string(out, d->fw_main, sizeof(d->fw_main));
			else if (out[0] == 1)
				fw_string(out, d->fw_boot, sizeof(d->fw_boot));
		}
	}
}

struct ss2k_dev *ss2k_new(void)
{
	struct ss2k_dev *d = calloc(1, sizeof(*d));
	if (d)
		d->fd = -1;
	return d;
}

void ss2k_free(struct ss2k_dev *d)
{
	if (!d)
		return;
	ss2k_close(d);
	free(d);
}

int ss2k_connect(struct ss2k_dev *d, const char *path)
{
	if (d->fd >= 0)
		close(d->fd);
	memset(d, 0, sizeof(*d));
	d->fd = -1;
	int rc;
	if (path) {
		unsigned vid = 0, pid = 0;
		const char *base = strrchr(path, '/');
		read_hid_id(base ? base + 1 : path, &vid, &pid);
		rc = try_node(d, path, pid == SS2K_WIRED_PID);
	} else {
		rc = autodetect(d);
	}
	if (rc)
		return rc;
	if ((rc = fs_walk(d))) {
		ss2k_close(d);
		return rc;
	}
	read_identity(d);
	return 0;
}

void ss2k_close(struct ss2k_dev *d)
{
	if (d && d->fd >= 0) {
		close(d->fd);
		d->fd = -1;
	}
}

const char *ss2k_info(const struct ss2k_dev *d, int key)
{
	switch (key) {
	case SS2K_INFO_PATH: return d->path;
	case SS2K_INFO_NAME: return d->name;
	case SS2K_INFO_MODEL: return d->model;
	case SS2K_INFO_SERIAL: return d->serial;
	case SS2K_INFO_FW_MAIN: return d->fw_main;
	case SS2K_INFO_FW_BOOT: return d->fw_boot;
	default: return "";
	}
}

int ss2k_is_wired(const struct ss2k_dev *d) { return d->wired; }

int ss2k_has_feature(const struct ss2k_dev *d, uint16_t fid)
{
	for (int i = 0; i < d->feats.count; i++)
		if (d->feats.ids[i] == fid)
			return 1;
	return 0;
}

#define NEED(idx) do { if (!(idx)) return -ENOTSUP; } while (0)

/* ================================================================ battery */
int ss2k_get_battery(struct ss2k_dev *d, struct ss2k_battery *b)
{
	NEED(d->feats.idx_batt);
	uint8_t out[16] = { 0 };
	size_t ol = sizeof(out);
	/* fn0 = capabilities, fn1 = status [soc%][level][charging][ext power] */
	int rc = ss2k_call(d, d->feats.idx_batt, 0x01, NULL, 0, out, &ol, 0);
	if (rc)
		return rc;
	b->percent = out[0];
	b->level = out[1];
	b->charging = out[2];
	b->external_power = out[3];
	return 0;
}

const char *ss2k_charging_label(int s)
{
	switch (s) {
	case 0: return "discharging";
	case 1: return "charging";
	case 2: return "charging (slow)";
	case 3: return "charged";
	case 4: return "charging error";
	default: return "unknown";
	}
}

/* ================================================================ DPI */
static int be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }

/*
 * Walks the paged fn2 list: BE16 values, 0x0000 terminates; a value whose top
 * three bits are set is a range marker (step in the low 13 bits, next word is
 * the range end, starting from the previous value). Calls visit() per
 * singular value / range so callers can check membership or bounds.
 */
typedef int (*dpi_visit_fn)(void *ctx, int start, int end, int step);

static int dpi_list_walk(struct ss2k_dev *d, int sensor, int axis, dpi_visit_fn visit, void *ctx)
{
	uint8_t buf[48];
	int have = 0, last = 0;
	for (int page = 0; page < 16; page++) {
		uint8_t out[16];
		size_t ol = sizeof(out);
		uint8_t pr[3] = { (uint8_t)sensor, (uint8_t)axis, (uint8_t)page };
		int rc = ss2k_call(d, d->feats.idx_dpi, 0x02, pr, 3, out, &ol, 0);
		if (rc)
			return rc;
		if (ol < 4)
			return -EPROTO;
		/* reply: [sensor][axis][page][data...] */
		memcpy(buf + have, out + 3, ol - 3);
		have += (int)ol - 3;
		int idx = 0;
		while (have - idx >= 2) {
			int v = be16(buf + idx);
			if (v == 0)
				return 0;           /* terminator */
			if ((v >> 13) == 7) {
				if (have - idx < 4)
					break;      /* marker split across pages */
				int step = v & 0x1fff, end = be16(buf + idx + 2);
				idx += 4;
				if (step && end > last) {
					if (visit(ctx, last + step, end, step))
						return 0;
					last = end;
				}
			} else {
				idx += 2;
				if (visit(ctx, v, v, 1))
					return 0;
				last = v;
			}
		}
		memmove(buf, buf + idx, (size_t)(have - idx));
		have -= idx;
	}
	return 0;
}

struct dpi_bounds { int min, max; };

static int bounds_visit(void *ctx, int start, int end, int step)
{
	struct dpi_bounds *b = ctx;
	(void)step;
	if (!b->min || start < b->min) b->min = start;
	if (end > b->max) b->max = end;
	return 0;
}

struct dpi_member { int dpi, found; };

static int member_visit(void *ctx, int start, int end, int step)
{
	struct dpi_member *m = ctx;
	if (m->dpi >= start && m->dpi <= end && (m->dpi - start) % step == 0)
		return m->found = 1;
	return 0;
}

int ss2k_dpi_query(struct ss2k_dev *d, int sensor, struct ss2k_dpi_sensor *s)
{
	NEED(d->feats.idx_dpi);
	memset(s, 0, sizeof(*s));
	s->index = sensor;
	uint8_t o[16];
	size_t ol = sizeof(o);
	uint8_t p0 = (uint8_t)sensor;
	/* fn1 caps: [sensor][dpi levels][flags: bit0 Y axis, ...] */
	int rc = ss2k_call(d, d->feats.idx_dpi, 0x01, &p0, 1, o, &ol, 0);
	if (rc)
		return rc;
	s->has_y = ol >= 3 ? (o[2] & 1) : 0;
	struct dpi_bounds b = { 0, 0 };
	if ((rc = dpi_list_walk(d, sensor, 0, bounds_visit, &b)))
		return rc;
	s->min = b.min;
	s->max = b.max;
	memset(o, 0, sizeof(o));
	ol = sizeof(o);
	/* fn5: [sensor][curX][defX][curY][defY][lod] (BE16) */
	if ((rc = ss2k_call(d, d->feats.idx_dpi, 0x05, &p0, 1, o, &ol, 0)))
		return rc;
	s->x_cur = be16(o + 1);
	s->x_def = be16(o + 3);
	s->y_cur = be16(o + 5);
	s->y_def = be16(o + 7);
	s->lod = o[9];
	return 0;
}

struct dpi_collect { int *out; int max, n; };

static int collect_visit(void *ctx, int start, int end, int step)
{
	struct dpi_collect *c = ctx;
	if (c->n >= c->max)
		return 1;
	c->out[3 * c->n] = start;
	c->out[3 * c->n + 1] = end;
	c->out[3 * c->n + 2] = step;
	c->n++;
	return 0;
}

int ss2k_dpi_ranges(struct ss2k_dev *d, int sensor, int *triples, int max)
{
	NEED(d->feats.idx_dpi);
	struct dpi_collect c = { triples, max, 0 };
	int rc = dpi_list_walk(d, sensor, 0, collect_visit, &c);
	return rc ? (rc > 0 ? -EIO : rc) : c.n;
}

int ss2k_dpi_valid(struct ss2k_dev *d, int sensor, int dpi)
{
	struct dpi_member m = { dpi, 0 };
	if (!d->feats.idx_dpi || dpi_list_walk(d, sensor, 0, member_visit, &m))
		return 0;
	return m.found;
}

int ss2k_dpi_set(struct ss2k_dev *d, int sensor, int dx, int dy, int lod)
{
	NEED(d->feats.idx_dpi);
	if (lod < 0) {
		struct ss2k_dpi_sensor cur;
		int rc = ss2k_dpi_query(d, sensor, &cur);
		if (rc)
			return rc;
		lod = cur.lod;
	}
	uint8_t p[6] = {
		(uint8_t)sensor, (uint8_t)(dx >> 8), (uint8_t)dx,
		(uint8_t)(dy >> 8), (uint8_t)dy, (uint8_t)lod,
	};
	/* applies in host mode; in onboard mode the profile stage governs */
	return ss2k_call(d, d->feats.idx_dpi, 0x06, p, 6, NULL, NULL, 1);
}

/* ================================================================ HITS */
int ss2k_hits_caps(struct ss2k_dev *d, int *count, int *max_act, int *max_rt, int *max_hap)
{
	NEED(d->feats.idx_hits);
	uint8_t out[16] = { 0 };
	size_t ol = sizeof(out);
	int rc = ss2k_call(d, d->feats.idx_hits, 0x00, NULL, 0, out, &ol, 0);
	if (rc)
		return rc;
	/* [flags][buttons][act<<2][rt<<2][hap<<2][?]; the firmware reports 3
	 * buttons but only 0 (left) and 1 (right) answer */
	if (count) *count = out[1] > 2 ? 2 : out[1];
	if (max_act) *max_act = out[2] >> 2;
	if (max_rt) *max_rt = out[3] >> 2;
	if (max_hap) *max_hap = out[4] >> 2;
	return 0;
}

static int hits_raw(struct ss2k_dev *d, int button, uint8_t raw[4])
{
	uint8_t out[16] = { 0 };
	size_t ol = sizeof(out);
	uint8_t p = (uint8_t)button;
	int rc = ss2k_call(d, d->feats.idx_hits, 0x02, &p, 1, out, &ol, 0);
	if (rc)
		return rc;
	if (ol < 4)
		return -EPROTO;
	memcpy(raw, out, 4);
	return 0;
}

int ss2k_hits_get(struct ss2k_dev *d, int button, struct ss2k_hits_btn *b)
{
	NEED(d->feats.idx_hits);
	uint8_t r[4];
	int rc = hits_raw(d, button, r);
	if (rc)
		return rc;
	/* [btn][act<<2][rt<<2 | enable][hap<<2] */
	b->actuation = r[1] >> 2;
	b->rapid = r[2] >> 2;
	b->rapid_enabled = r[2] & 0x01;
	b->haptics = r[3] >> 2;
	return 0;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int ss2k_hits_set(struct ss2k_dev *d, int button, int act, int rt, int rt_en, int hap)
{
	NEED(d->feats.idx_hits);
	uint8_t r[4];
	int rc = hits_raw(d, button, r);
	if (rc)
		return rc;
	/* read-modify-write; the low two bits of act/hap are reserved */
	act = act < 0 ? r[1] >> 2 : clampi(act, 1, 10);
	rt = rt < 0 ? r[2] >> 2 : clampi(rt, 1, 5);
	rt_en = rt_en < 0 ? (r[2] & 1) : !!rt_en;
	hap = hap < 0 ? r[3] >> 2 : clampi(hap, 0, 5);
	uint8_t p[4] = {
		(uint8_t)button, (uint8_t)(act << 2),
		(uint8_t)((rt << 2) | rt_en), (uint8_t)(hap << 2),
	};
	return ss2k_call(d, d->feats.idx_hits, 0x01, p, 4, NULL, NULL, 1);
}

/* ================================================================ report rate */
const unsigned ss2k_rate_hz[7] = { 125, 250, 500, 1000, 2000, 4000, 8000 };
const char *ss2k_rate_label[7] = { "8ms", "4ms", "2ms", "1ms", "500us", "250us", "125us" };

static int rate_mask(struct ss2k_dev *d, uint8_t fn, int conn, uint16_t *mask)
{
	uint8_t out[16] = { 0 };
	size_t ol = sizeof(out);
	uint8_t c = (uint8_t)conn;
	int rc = ss2k_call(d, d->feats.idx_rate, fn, &c, 1, out, &ol, 0);
	if (rc)
		return rc;
	*mask = (uint16_t)be16(out);
	return 0;
}

int ss2k_rate_lists(struct ss2k_dev *d, uint16_t *wired, uint16_t *wireless)
{
	NEED(d->feats.idx_rate);
	/* fn0 [connection type] -> mask; 0 = wired, 1 = LIGHTSPEED */
	int rc = rate_mask(d, 0x00, 0, wired);
	return rc ? rc : rate_mask(d, 0x00, 1, wireless);
}

int ss2k_rate_supported(struct ss2k_dev *d, uint16_t *mask)
{
	NEED(d->feats.idx_rate);
	/* fn1 -> mask for the link carrying this request */
	return rate_mask(d, 0x01, 0, mask);
}

int ss2k_rate_get(struct ss2k_dev *d, int *rate_idx)
{
	NEED(d->feats.idx_rate);
	uint8_t out[16] = { 0 };
	size_t ol = sizeof(out);
	int rc = ss2k_call(d, d->feats.idx_rate, 0x02, NULL, 0, out, &ol, 0);
	if (rc)
		return rc;
	*rate_idx = out[0];
	return 0;
}

int ss2k_rate_set(struct ss2k_dev *d, int rate_idx)
{
	NEED(d->feats.idx_rate);
	if (rate_idx < 0 || rate_idx > 6)
		return -EINVAL;
	/* applies to the link carrying the request; volatile */
	uint8_t p = (uint8_t)rate_idx;
	return ss2k_call(d, d->feats.idx_rate, 0x03, &p, 1, NULL, NULL, 0);
}

/* ================================================================ onboard */
int ss2k_onboard_info(struct ss2k_dev *d, struct ss2k_onboard_info *i)
{
	NEED(d->feats.idx_onboard);
	uint8_t o[16] = { 0 };
	size_t ol = sizeof(o);
	int rc = ss2k_call(d, d->feats.idx_onboard, 0x00, NULL, 0, o, &ol, 0);
	if (rc)
		return rc;
	/* live: 01 08 01 05 01 05 10 00ff 0a 04 */
	i->memory_model = o[0];
	i->profile_format = o[1];
	i->macro_format = o[2];
	i->profile_count = o[3];
	i->profile_count_oob = o[4];
	i->button_count = o[5];
	i->sector_count = o[6];
	i->sector_size = be16(o + 7);
	return 0;
}

static int onboard_byte(struct ss2k_dev *d, uint8_t fn, int *v)
{
	NEED(d->feats.idx_onboard);
	uint8_t out[16] = { 0 };
	size_t ol = sizeof(out);
	int rc = ss2k_call(d, d->feats.idx_onboard, fn, NULL, 0, out, &ol, 0);
	if (rc)
		return rc;
	*v = out[0];
	return 0;
}

int ss2k_onboard_mode_get(struct ss2k_dev *d, int *mode) { return onboard_byte(d, 0x02, mode); }

int ss2k_onboard_mode_set(struct ss2k_dev *d, int mode)
{
	NEED(d->feats.idx_onboard);
	if (mode != SS2K_MODE_ONBOARD && mode != SS2K_MODE_HOST)
		return -EINVAL;
	uint8_t p = (uint8_t)mode;
	return ss2k_call(d, d->feats.idx_onboard, 0x01, &p, 1, NULL, NULL, 0);
}

int ss2k_profile_current(struct ss2k_dev *d, int *sector)
{
	NEED(d->feats.idx_onboard);
	uint8_t out[16] = { 0 };
	size_t ol = sizeof(out);
	int rc = ss2k_call(d, d->feats.idx_onboard, 0x04, NULL, 0, out, &ol, 0);
	if (rc)
		return rc;
	*sector = be16(out);
	return 0;
}

int ss2k_profile_activate(struct ss2k_dev *d, int sector)
{
	NEED(d->feats.idx_onboard);
	if (sector <= 0 || sector > 0xffff)
		return -EINVAL;     /* sector 0 is the directory, never a profile */
	uint8_t p[2] = { (uint8_t)(sector >> 8), (uint8_t)sector };
	int rc = -ETIMEDOUT;
	/* activation reloads the profile and can time out spuriously */
	for (int i = 0; i < 3; i++) {
		rc = ss2k_call(d, d->feats.idx_onboard, 0x03, p, 2, NULL, NULL, 0);
		if (rc != -ETIMEDOUT)
			break;
		usleep(150 * 1000);
	}
	return rc;
}

int ss2k_dpi_stage_get(struct ss2k_dev *d, int *stage) { return onboard_byte(d, 0x0b, stage); }

int ss2k_dpi_stage_set(struct ss2k_dev *d, int stage)
{
	NEED(d->feats.idx_onboard);
	if (stage < 0 || stage > 4)
		return -EINVAL;
	uint8_t p = (uint8_t)stage;
	return ss2k_call(d, d->feats.idx_onboard, 0x0c, &p, 1, NULL, NULL, 0);
}

int ss2k_profile_read_chunk(struct ss2k_dev *d, int sector, int off, uint8_t out16[16])
{
	NEED(d->feats.idx_onboard);
	uint8_t p[4] = {
		(uint8_t)(sector >> 8), (uint8_t)sector,
		(uint8_t)(off >> 8), (uint8_t)off,
	};
	int rc = -ETIMEDOUT;
	for (int att = 0; att < 4; att++) {
		uint8_t out[16] = { 0 };
		size_t ol = sizeof(out);
		rc = ss2k_call(d, d->feats.idx_onboard, 0x05, p, 4, out, &ol, 1);
		if (!rc) {
			memcpy(out16, out, 16);
			return 0;
		}
		if (rc != -ETIMEDOUT && rc != SS2K_E_BUSY)
			break;
		usleep(120 * 1000);
	}
	return rc;
}

int ss2k_profile_read(struct ss2k_dev *d, int sector, uint8_t out[SS2K_SECTOR_MAX], int *len)
{
	struct ss2k_onboard_info info;
	int rc = ss2k_onboard_info(d, &info);
	if (rc)
		return rc;
	int size = info.sector_size;
	if (size < 16 || size > SS2K_SECTOR_MAX)
		size = 255;
	memset(out, 0xff, SS2K_SECTOR_MAX);
	/* reads may not cross the sector end, so the last chunk is aligned to
	 * size-16 (offset 239 for the 255-byte sectors here) */
	for (int off = 0; off < size; off += 16) {
		int at = off + 16 > size ? size - 16 : off;
		uint8_t chunk[16];
		if ((rc = ss2k_profile_read_chunk(d, sector, at, chunk)))
			return rc;
		memcpy(out + at, chunk, 16);
	}
	if (len)
		*len = size;
	return 0;
}

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xffff, no reflect, no xorout) */
uint16_t ss2k_crc16(const uint8_t *p, size_t n)
{
	uint16_t crc = 0xffff;
	for (size_t i = 0; i < n; i++) {
		crc ^= (uint16_t)(p[i] << 8);
		for (int b = 0; b < 8; b++)
			crc = (uint16_t)((crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1));
	}
	return crc;
}

int ss2k_sector_crc_ok(const uint8_t *sec, int len)
{
	if (len < 3)
		return 0;
	return ss2k_crc16(sec, (size_t)len - 2) == be16(sec + len - 2);
}

/* ================================================================ misc */
int ss2k_surface_get(struct ss2k_dev *d, int *mode, int *raw)
{
	NEED(d->feats.idx_modestatus);
	uint8_t out[16] = { 0 };
	size_t ol = sizeof(out);
	/* fn0 -> [modeStatus0][modeStatus1]; status1 bits 1..2 = surface
	 * (0 auto, 1 on, 2 off), bit 0 = LightForce switch mode */
	int rc = ss2k_call(d, d->feats.idx_modestatus, 0x00, NULL, 0, out, &ol, 0);
	if (rc)
		return rc;
	if (mode)
		*mode = (out[1] >> 1) & 0x03;
	if (raw)
		*raw = out[1];
	return 0;
}

int ss2k_surface_set(struct ss2k_dev *d, int mode)
{
	NEED(d->feats.idx_modestatus);
	if (mode < SS2K_SURFACE_AUTO || mode > SS2K_SURFACE_OFF)
		return -EINVAL;
	/* fn1 [status0][status1][changeMask0][changeMask1]: only bits 1..2 change */
	uint8_t p[4] = { 0x00, (uint8_t)(mode << 1), 0x00, 0x06 };
	return ss2k_call(d, d->feats.idx_modestatus, 0x01, p, 4, NULL, NULL, 1);
}

int ss2k_bhop_get(struct ss2k_dev *d, int *window)
{
	NEED(d->feats.idx_bhop);
	uint8_t out[16] = { 0 };
	size_t ol = sizeof(out);
	/* fn1 = get; fn2 is the setter (calling it as a getter clears the window) */
	int rc = ss2k_call(d, d->feats.idx_bhop, 0x01, NULL, 0, out, &ol, 0);
	if (rc)
		return rc;
	*window = out[0];
	return 0;
}

int ss2k_bhop_set(struct ss2k_dev *d, int window)
{
	NEED(d->feats.idx_bhop);
	if (window != 0 && (window < 10 || window > 100))
		return -EINVAL;
	uint8_t p = (uint8_t)window;
	return ss2k_call(d, d->feats.idx_bhop, 0x02, &p, 1, NULL, NULL, 0);
}

int ss2k_config_cookie(struct ss2k_dev *d, uint16_t *cookie)
{
	NEED(d->feats.idx_config);
	uint8_t out[16] = { 0 };
	size_t ol = sizeof(out);
	int rc = ss2k_call(d, d->feats.idx_config, 0x00, NULL, 0, out, &ol, 0);
	if (rc)
		return rc;
	*cookie = (uint16_t)be16(out);
	return 0;
}
