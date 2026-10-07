"""ss2k_binding — Python access to libss2k.so, shared by superstrike-gui
(GTK4) and superstrike-tray (GTK3). Device calls block; run them on a Worker.
"""
import ctypes
import ctypes.util
import os
import queue
import shutil
import sys
import threading

from gi.repository import GLib

RATE_HZ = [125, 250, 500, 1000, 2000, 4000, 8000]
LOD_NAMES = ["Low", "Medium", "High"]           # wire values 1..3
SURFACE_NAMES = ["Auto", "On", "Off"]           # wire values 0..2
MODE_ONBOARD, MODE_HOST = 1, 2
ENODEV, EACCES, ETIMEDOUT = 19, 13, 110

# ============================================================== library binding
c_int, c_int_p = ctypes.c_int, ctypes.POINTER(ctypes.c_int)
c_u16_p = ctypes.POINTER(ctypes.c_uint16)


def _ints(*names):
    return [(n, c_int) for n in names]


class Battery(ctypes.Structure):
    _fields_ = _ints("percent", "level", "charging", "external_power")


class DpiSensor(ctypes.Structure):
    _fields_ = _ints("index", "has_y", "x_cur", "x_def", "y_cur", "y_def", "lod", "min", "max")


class HitsBtn(ctypes.Structure):
    _fields_ = _ints("actuation", "rapid", "rapid_enabled", "haptics")


class OnboardInfo(ctypes.Structure):
    _fields_ = _ints("memory_model", "profile_format", "macro_format", "profile_count",
                     "profile_count_oob", "button_count", "sector_count", "sector_size")


Sector = ctypes.c_ubyte * 256
Chunk = ctypes.c_ubyte * 16


def load_library():
    here = os.path.dirname(os.path.realpath(__file__))
    candidates = [
        os.environ.get("SS2K_LIB"),
        os.path.join(here, "..", "src", "libss2k.so"),          # source tree
        os.path.join(here, "..", "..", "lib", "libss2k.so"),    # $PREFIX/share/superstrike
        ctypes.util.find_library("ss2k"),
    ]
    for path in candidates:
        if path and (os.path.exists(path) or not os.path.dirname(path)):
            try:
                lib = ctypes.CDLL(path)
                break
            except OSError:
                continue
    else:
        raise OSError("libss2k.so not found; run `make` in the project root")

    p = ctypes.c_void_p
    sigs = {
        "ss2k_new": ([], p),
        "ss2k_free": ([p], None),
        "ss2k_connect": ([p, ctypes.c_char_p], c_int),
        "ss2k_close": ([p], None),
        "ss2k_strerror": ([c_int], ctypes.c_char_p),
        "ss2k_info": ([p, c_int], ctypes.c_char_p),
        "ss2k_is_wired": ([p], c_int),
        "ss2k_get_battery": ([p, ctypes.POINTER(Battery)], c_int),
        "ss2k_charging_label": ([c_int], ctypes.c_char_p),
        "ss2k_dpi_query": ([p, c_int, ctypes.POINTER(DpiSensor)], c_int),
        "ss2k_dpi_ranges": ([p, c_int, c_int_p, c_int], c_int),
        "ss2k_dpi_set": ([p, c_int, c_int, c_int, c_int], c_int),
        "ss2k_hits_caps": ([p, c_int_p, c_int_p, c_int_p, c_int_p], c_int),
        "ss2k_hits_get": ([p, c_int, ctypes.POINTER(HitsBtn)], c_int),
        "ss2k_hits_set": ([p, c_int, c_int, c_int, c_int, c_int], c_int),
        "ss2k_rate_supported": ([p, c_u16_p], c_int),
        "ss2k_rate_get": ([p, c_int_p], c_int),
        "ss2k_rate_set": ([p, c_int], c_int),
        "ss2k_onboard_info": ([p, ctypes.POINTER(OnboardInfo)], c_int),
        "ss2k_onboard_mode_get": ([p, c_int_p], c_int),
        "ss2k_onboard_mode_set": ([p, c_int], c_int),
        "ss2k_profile_current": ([p, c_int_p], c_int),
        "ss2k_profile_activate": ([p, c_int], c_int),
        "ss2k_dpi_stage_get": ([p, c_int_p], c_int),
        "ss2k_dpi_stage_set": ([p, c_int], c_int),
        "ss2k_profile_read": ([p, c_int, Sector, c_int_p], c_int),
        "ss2k_profile_read_chunk": ([p, c_int, c_int, Chunk], c_int),
        "ss2k_sector_crc_ok": ([Sector, c_int], c_int),
        "ss2k_surface_get": ([p, c_int_p, c_int_p], c_int),
        "ss2k_surface_set": ([p, c_int], c_int),
        "ss2k_bhop_get": ([p, c_int_p], c_int),
        "ss2k_bhop_set": ([p, c_int], c_int),
    }
    for name, (args, res) in sigs.items():
        fn = getattr(lib, name)
        fn.argtypes, fn.restype = args, res
    return lib


class DeviceError(Exception):
    def __init__(self, what, rc, msg):
        super().__init__(f"{what}: {msg}")
        self.rc = rc


class Mouse:
    """Thin typed wrapper; every method runs on the worker thread."""

    def __init__(self, lib):
        self.lib = lib
        self.h = lib.ss2k_new()

    def _ck(self, rc, what):
        if rc:
            raise DeviceError(what, rc, self.lib.ss2k_strerror(rc).decode())

    def _int(self, fn, what):
        v = c_int()
        self._ck(fn(self.h, ctypes.byref(v)), what)
        return v.value

    def connect(self):
        self._ck(self.lib.ss2k_connect(self.h, None), "connect")

    def info(self, key):
        return self.lib.ss2k_info(self.h, key).decode(errors="replace")

    def battery(self):
        b = Battery()
        self._ck(self.lib.ss2k_get_battery(self.h, ctypes.byref(b)), "battery")
        return b

    def hits_count(self):
        n, a, r, h = c_int(), c_int(), c_int(), c_int()
        self._ck(self.lib.ss2k_hits_caps(self.h, *(ctypes.byref(x) for x in (n, a, r, h))), "HITS caps")
        return n.value

    def hits_get(self, btn):
        b = HitsBtn()
        self._ck(self.lib.ss2k_hits_get(self.h, btn, ctypes.byref(b)), "HITS read")
        return b

    def hits_set(self, btn, act, rt, rt_on, hap):
        self._ck(self.lib.ss2k_hits_set(self.h, btn, act, rt, rt_on, hap), "HITS write")

    def rate_mask(self):
        m = ctypes.c_uint16()
        self._ck(self.lib.ss2k_rate_supported(self.h, ctypes.byref(m)), "rate list")
        return m.value

    def rate_get(self):
        return self._int(self.lib.ss2k_rate_get, "rate read")

    def rate_set(self, idx):
        self._ck(self.lib.ss2k_rate_set(self.h, idx), "rate write")

    def dpi(self):
        s = DpiSensor()
        self._ck(self.lib.ss2k_dpi_query(self.h, 0, ctypes.byref(s)), "DPI read")
        return s

    def dpi_ranges(self):
        buf = (c_int * 96)()
        n = self.lib.ss2k_dpi_ranges(self.h, 0, buf, 32)
        if n < 0:
            self._ck(n, "DPI list")
        return [(buf[3 * i], buf[3 * i + 1], buf[3 * i + 2]) for i in range(n)]

    def dpi_set(self, dpi, lod):
        self._ck(self.lib.ss2k_dpi_set(self.h, 0, dpi, dpi, lod), "DPI write")

    def mode_get(self):
        return self._int(self.lib.ss2k_onboard_mode_get, "mode read")

    def mode_set(self, mode):
        self._ck(self.lib.ss2k_onboard_mode_set(self.h, mode), "mode write")

    def stage_get(self):
        return self._int(self.lib.ss2k_dpi_stage_get, "DPI stage read")

    def stage_set(self, stage):
        self._ck(self.lib.ss2k_dpi_stage_set(self.h, stage), "DPI stage write")

    def profile_current(self):
        return self._int(self.lib.ss2k_profile_current, "profile read")

    def profile_activate(self, sector):
        self._ck(self.lib.ss2k_profile_activate(self.h, sector), "profile activate")

    def sector(self, sector):
        buf, n = Sector(), c_int()
        self._ck(self.lib.ss2k_profile_read(self.h, sector, buf, ctypes.byref(n)), "sector read")
        return bytes(buf[: n.value]), bool(self.lib.ss2k_sector_crc_ok(buf, n.value))

    def chunk(self, sector, off):
        buf = Chunk()
        self._ck(self.lib.ss2k_profile_read_chunk(self.h, sector, off, buf), "sector read")
        return bytes(buf)

    def surface_get(self):
        m, raw = c_int(), c_int()
        self._ck(self.lib.ss2k_surface_get(self.h, ctypes.byref(m), ctypes.byref(raw)), "surface read")
        return m.value

    def surface_set(self, mode):
        self._ck(self.lib.ss2k_surface_set(self.h, mode), "surface write")

    def bhop_get(self):
        return self._int(self.lib.ss2k_bhop_get, "bunny-hop read")

    def bhop_set(self, window):
        self._ck(self.lib.ss2k_bhop_set(self.h, window), "bunny-hop write")


# ============================================================== profile decoding
def utf16_name(raw):
    chars = []
    for i in range(0, len(raw) - 1, 2):
        c = raw[i] | raw[i + 1] << 8
        if c in (0, 0xFFFF):
            break
        chars.append(chr(c))
    return "".join(chars)


def pretty_profile_name(name, slot):
    if not name or name == "PROFILE_NAME_DEFAULT":
        return f"Profile {slot}"
    return name


def button_label(e):
    if e[0] == 0xFF:
        return "Default"
    kind = e[0] & 0xF0
    if kind == 0x00:
        return "Macro"
    if kind == 0x80:
        if e[1] == 0x01:
            names = ["Left click", "Right click", "Middle click", "Back", "Forward"]
            mask = e[2] << 8 | e[3]
            hits = [names[i] if i < 5 else f"Button {i + 1}" for i in range(16) if mask >> i & 1]
            return " + ".join(hits) or "None"
        if e[1] == 0x02:
            return f"Key (usage 0x{e[3]:02x}, modifiers 0x{e[2]:02x})"
        if e[1] == 0x03:
            return f"Media key 0x{e[2] << 8 | e[3]:04x}"
        return "Disabled"
    if kind == 0x90:
        return {
            0x03: "DPI up", 0x04: "DPI down", 0x05: "DPI cycle", 0x06: "DPI default",
            0x07: "DPI shift", 0x08: "Next profile", 0x09: "Previous profile",
            0x0A: "Cycle profile", 0x0B: "G-Shift", 0x0C: "Battery status",
        }.get(e[1], f"Function 0x{e[1]:02x}")
    return "Unknown"


def decode_profile(p):
    stages = []
    for s in range(5):
        st = p[4 + 5 * s: 9 + 5 * s]
        stages.append((st[0] | st[1] << 8, st[2] | st[3] << 8, st[4]))
    buttons = []
    for i in range(16):
        e = p[0x30 + 4 * i: 0x34 + 4 * i]
        if not (e[0] == 0xFF and e[1] == 0xFF):
            buttons.append(button_label(e))
    hits = [(p[0x26 + 3 * b] >> 2, p[0x27 + 3 * b] >> 2, p[0x27 + 3 * b] & 1, p[0x28 + 3 * b] >> 2)
            for b in range(2)]
    return {
        "rate_wireless": p[0] % 7, "rate_wired": p[1] % 7,
        "default_stage": p[2], "stages": stages, "hits": hits,
        "bhop": p[0x25],
        "power_save": p[0x2C] | p[0x2D] << 8, "power_off": p[0x2E] | p[0x2F] << 8,
        "buttons": buttons, "name": utf16_name(p[0xA0:0xD0]),
    }


def read_state(m):
    """Everything the panel shows, in one worker-thread pass."""
    st = {
        "name": m.info(1), "model": m.info(2), "serial": m.info(3),
        "fw": m.info(4), "boot": m.info(5), "path": m.info(0),
        "wired": bool(m.lib.ss2k_is_wired(m.h)),
    }

    def opt(key, fn):
        try:
            st[key] = fn()
        except DeviceError as e:
            if e.rc in (-ENODEV,):
                raise
            st[key] = None

    opt("battery", m.battery)
    opt("mode", m.mode_get)
    opt("rate_mask", m.rate_mask)
    opt("rate", m.rate_get)
    opt("dpi", m.dpi)
    opt("dpi_ranges", m.dpi_ranges)
    opt("stage", m.stage_get)
    opt("surface", m.surface_get)
    opt("bhop", m.bhop_get)
    opt("hits_count", m.hits_count)
    st["hits"] = [m.hits_get(b) for b in range(st["hits_count"] or 0)]
    opt("current", m.profile_current)

    profiles = []
    try:
        directory, _ = m.sector(0)
        for i in range(0, len(directory) - 5, 4):
            e = directory[i:i + 4]
            if e[0] == 0xFF and e[1] == 0xFF:
                break
            sec = e[0] << 8 | e[1]
            raw = b"".join(m.chunk(sec, off) for off in (0xA0, 0xB0, 0xC0))
            profiles.append({"slot": i // 4 + 1, "sector": sec, "enabled": bool(e[2]),
                             "name": utf16_name(raw)})
    except DeviceError:
        pass
    st["profiles"] = profiles
    st["profile"] = None
    if st["current"]:
        try:
            data, crc_ok = m.sector(st["current"])
            st["profile"] = decode_profile(data)
            st["profile"]["crc_ok"] = crc_ok
        except DeviceError:
            pass
    return st


# ============================================================== worker
class Worker:
    """Serialises device access on one thread; callbacks land on the GTK loop."""

    def __init__(self):
        self.q = queue.Queue()
        threading.Thread(target=self._run, daemon=True).start()

    def submit(self, fn, done=None, fail=None):
        self.q.put((fn, done, fail))

    def _run(self):
        while True:
            fn, done, fail = self.q.get()
            try:
                result = fn()
            except Exception as e:  # noqa: BLE001 — surfaced to the UI
                if fail:
                    GLib.idle_add(fail, e)
                continue
            if done:
                GLib.idle_add(done, result)


# ============================================================== autostart
AUTOSTART_FILE = "superstrike-tray.desktop"


def autostart_path():
    config = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(config, "autostart", AUTOSTART_FILE)


def tray_executable():
    """superstrike-tray next to the running script (source tree or $PREFIX/bin)."""
    sibling = os.path.join(os.path.dirname(os.path.realpath(sys.argv[0])), "superstrike-tray")
    if os.access(sibling, os.X_OK):
        return sibling
    return shutil.which("superstrike-tray") or "superstrike-tray"


def autostart_enabled():
    try:
        with open(autostart_path()) as f:
            text = f.read()
    except OSError:
        return False
    return "Hidden=true" not in text and "X-GNOME-Autostart-enabled=false" not in text


def set_autostart(enabled):
    path = autostart_path()
    if not enabled:
        try:
            os.remove(path)
        except FileNotFoundError:
            pass
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("[Desktop Entry]\n"
                "Type=Application\n"
                "Name=SUPERSTRIKE Tray\n"
                "Comment=Quick controls for the Logitech G PRO X2 SUPERSTRIKE\n"
                f"Exec={tray_executable()}\n"
                "Icon=input-mouse\n"
                "Terminal=false\n"
                "X-GNOME-Autostart-enabled=true\n")


def start_tray():
    """Launch the tray now (it is single-instance, so a second launch is a no-op)."""
    try:
        GLib.spawn_async([tray_executable()], flags=GLib.SpawnFlags.SEARCH_PATH)
    except GLib.Error as e:
        print(f"could not start the tray: {e.message}", file=sys.stderr)
