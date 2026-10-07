/*
 * ss2k — userspace driver library for the Logitech G PRO X 2 SUPERSTRIKE
 * gaming mouse (046d:40bd, receiver 046d:c54d, wired 046d:c0a8).
 *
 * Reverse engineered protocol (validated live against real hardware):
 *   - HID++ 4.2 over hidraw; short frames 0x10 (7B) / long frames 0x11 (20B)
 *   - Frame: [rid][devIndex][featureIndex][(func<<4)|swId][params...]
 *   - Error: [rid][devIndex][0xFF][featureIndex][(func<<4)|swId][code]
 *   - Features are resolved at runtime (indexes are per-device); see
 *     docs/PROTOCOL.md for the full table.
 *
 * Return convention for every int-returning call:
 *   0 ok, >0 HID++ 2.0 error code (enum ss2k_err), <0 -errno.
 *
 * Copyright 2026 — Released under GPL-2.0-or-later for interop with Linux.
 * No warranty.
 */
#ifndef SS2K_H
#define SS2K_H

#include <stdint.h>
#include <stddef.h>

#define SS2K_LOGI_VID       0x046d
#define SS2K_DEV_PID        0x40bd
#define SS2K_RECV_PID       0xc54d
#define SS2K_WIRED_PID      0xc0a8

#define SS2K_SHORT_LEN      7
#define SS2K_LONG_LEN       20
#define SS2K_RID_SHORT      0x10
#define SS2K_RID_LONG       0x11

/* profile sectors are 255 bytes on this device (0x8100 fn0 sector_size) */
#define SS2K_SECTOR_MAX     256

/* HID++ 2.0 error codes */
enum ss2k_err {
	SS2K_OK = 0, SS2K_E_UNKNOWN = 1, SS2K_E_INVALID_ARG = 2,
	SS2K_E_OUT_OF_RANGE = 3, SS2K_E_HW = 4, SS2K_E_LOGI = 5,
	SS2K_E_INVALID_FEAT_IDX = 6, SS2K_E_INVALID_FN = 7, SS2K_E_BUSY = 8,
	SS2K_E_UNSUPPORTED = 9,
	SS2K_E_HIDPP10 = 0x100,     /* receiver HID++ 1.0 error (code in low byte) */
};

/* feature ids */
#define FID_ROOT            0x0000
#define FID_FEATURE_SET     0x0001
#define FID_DEVICE_INFO     0x0003
#define FID_DEVICE_NAME     0x0005
#define FID_CONFIG_CHANGE   0x0020
#define FID_UNIFIED_BATT    0x1004
#define FID_ANALOG_BUTTONS  0x1b0c
#define FID_FORCE_PAIRING   0x1500
#define FID_ADC_MEASURE     0x1e00
#define FID_EXT_ADJ_DPI     0x2202
#define FID_XY_STATS        0x2250
#define FID_WHEEL_STATS     0x2251
#define FID_EXT_ADJ_RATE    0x8061
#define FID_MODE_STATUS     0x8090
#define FID_BUNNY_HOP       0x80e0
#define FID_ONBOARD_PROFILES 0x8100
#define FID_BUTTON_SPY      0x8110

struct ss2k_features {
	uint8_t idx_featureset, idx_devinfo, idx_name, idx_config, idx_batt;
	uint8_t idx_dpi, idx_xystats, idx_wheelstats, idx_modestatus;
	uint8_t idx_hits, idx_rate, idx_onboard, idx_spy, idx_forcepair;
	uint8_t idx_adc, idx_bhop;
	int count;
	uint16_t ids[64];
};

struct ss2k_dev {
	int fd;
	char path[256];
	char name[128];
	uint8_t dev_index;        /* HID++ address: 0x01 via receiver, 0xff wired */
	int wired;
	struct ss2k_features feats;
	char serial[16];
	char model[32];
	char fw_main[32];
	char fw_boot[32];
	int proto_major, proto_minor;
};

/* ---- transport ---- */
struct ss2k_dev *ss2k_new(void);
void ss2k_free(struct ss2k_dev *d);
/* open + ping + feature enumeration + identity; path NULL = autodetect */
int  ss2k_connect(struct ss2k_dev *d, const char *hidraw_path);
void ss2k_close(struct ss2k_dev *d);
int  ss2k_ping(struct ss2k_dev *d);
int  ss2k_call(struct ss2k_dev *d, uint8_t fidx, uint8_t func,
	       const uint8_t *params, size_t plen,
	       uint8_t *out, size_t *olen, int long_wanted);
const char *ss2k_strerror(int rc);
const char *ss2k_hex(const uint8_t *b, size_t n);       /* static buf */

/* ---- identity (for bindings that cannot see struct ss2k_dev) ---- */
enum ss2k_info_key {
	SS2K_INFO_PATH, SS2K_INFO_NAME, SS2K_INFO_MODEL, SS2K_INFO_SERIAL,
	SS2K_INFO_FW_MAIN, SS2K_INFO_FW_BOOT,
};
const char *ss2k_info(const struct ss2k_dev *d, int key);
int  ss2k_is_wired(const struct ss2k_dev *d);
int  ss2k_has_feature(const struct ss2k_dev *d, uint16_t fid);

/* ---- battery (0x1004) ---- */
struct ss2k_battery {
	int percent;          /* state of charge */
	int level;            /* bitmask: 1 critical, 2 low, 4 good, 8 full */
	int charging;         /* 0 discharging, 1 charging, 2 slow, 3 full, 4 error */
	int external_power;
};
int  ss2k_get_battery(struct ss2k_dev *d, struct ss2k_battery *b);
const char *ss2k_charging_label(int charging);

/* ---- DPI (0x2202) ---- */
struct ss2k_dpi_sensor {
	int index, has_y;
	int x_cur, x_def, y_cur, y_def, lod;  /* lod: 1 low, 2 medium, 3 high */
	int min, max;                         /* bounds of the supported list */
};
int  ss2k_dpi_query(struct ss2k_dev *d, int sensor, struct ss2k_dpi_sensor *out);
int  ss2k_dpi_valid(struct ss2k_dev *d, int sensor, int dpi); /* 1 if in the list */
/* fills up to max (start, end, step) triples; returns the count or <0 */
int  ss2k_dpi_ranges(struct ss2k_dev *d, int sensor, int *triples, int max);
int  ss2k_dpi_set(struct ss2k_dev *d, int sensor, int dpi_x, int dpi_y, int lod_or_negative_keep);

/* ---- HITS analog buttons (0x1B0C) ---- */
struct ss2k_hits_btn {
	int actuation;        /* 1..10 */
	int rapid;            /* rapid-trigger sensitivity 1..5 */
	int rapid_enabled;    /* rt byte bit0 */
	int haptics;          /* 0..5 */
};
int  ss2k_hits_caps(struct ss2k_dev *d, int *count, int *max_act, int *max_rt, int *max_hap);
int  ss2k_hits_get(struct ss2k_dev *d, int button, struct ss2k_hits_btn *b);
/* negative argument = keep current value */
int  ss2k_hits_set(struct ss2k_dev *d, int button, int act, int rt, int rt_enabled, int hap);

/* ---- report rate (0x8061); index 0..6 = 125..8000 Hz ---- */
int  ss2k_rate_lists(struct ss2k_dev *d, uint16_t *wired, uint16_t *wireless);
int  ss2k_rate_supported(struct ss2k_dev *d, uint16_t *mask); /* current link */
int  ss2k_rate_get(struct ss2k_dev *d, int *rate_idx);
int  ss2k_rate_set(struct ss2k_dev *d, int rate_idx);
extern const unsigned ss2k_rate_hz[7];
extern const char *ss2k_rate_label[7];

/* ---- onboard profiles (0x8100) ---- */
#define SS2K_MODE_ONBOARD 1
#define SS2K_MODE_HOST    2
struct ss2k_onboard_info {
	int memory_model, profile_format, macro_format;
	int profile_count, profile_count_oob, button_count;
	int sector_count, sector_size;
};
int  ss2k_onboard_info(struct ss2k_dev *d, struct ss2k_onboard_info *info);
int  ss2k_onboard_mode_get(struct ss2k_dev *d, int *mode);
int  ss2k_onboard_mode_set(struct ss2k_dev *d, int mode);
int  ss2k_profile_current(struct ss2k_dev *d, int *sector);
int  ss2k_profile_activate(struct ss2k_dev *d, int sector);
int  ss2k_dpi_stage_get(struct ss2k_dev *d, int *stage);
int  ss2k_dpi_stage_set(struct ss2k_dev *d, int stage);
int  ss2k_profile_read_chunk(struct ss2k_dev *d, int sector, int off, uint8_t out16[16]);
/* reads a whole sector (size from onboard info, 255 here); returns bytes read
 * in *len. Sector 0 is the RAM profile directory. */
int  ss2k_profile_read(struct ss2k_dev *d, int sector, uint8_t out[SS2K_SECTOR_MAX], int *len);
/* CRC-16/CCITT-FALSE over [0, len-2), stored big-endian in the last 2 bytes */
uint16_t ss2k_crc16(const uint8_t *p, size_t n);
int  ss2k_sector_crc_ok(const uint8_t *sec, int len);

/* ---- gaming surface mode (0x8090 modeStatus1 bits 1..2) ---- */
#define SS2K_SURFACE_AUTO 0
#define SS2K_SURFACE_ON   1
#define SS2K_SURFACE_OFF  2
int  ss2k_surface_get(struct ss2k_dev *d, int *mode, int *raw_status1);
int  ss2k_surface_set(struct ss2k_dev *d, int mode);

/* ---- bunny-hop scroll filter (0x80E0); window in 10 ms units, 0 = off ---- */
int  ss2k_bhop_get(struct ss2k_dev *d, int *window);
int  ss2k_bhop_set(struct ss2k_dev *d, int window);

/* ---- config change (0x0020) ---- */
int  ss2k_config_cookie(struct ss2k_dev *d, uint16_t *cookie);

#endif /* SS2K_H */
