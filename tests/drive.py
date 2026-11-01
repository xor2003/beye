#!/usr/bin/env python3
"""PTY-driven TUI test harness for biew.

Spawns a binary under a pseudo-terminal, feeds key bytes, scrapes the
resulting screen with a minimal vt100 emulator and asserts expected
text.  Every case gets a FRESH session, so a case that exits or leaves a
dialog open cannot cascade into later failures.  The SAME case list runs
against every binary given with --bin, so builds can be compared
directly.

The native build requires ncurses (no direct-ANSI path exists), so it
is driven in two passes with a real terminfo: "xt" (TERM=xterm-256color,
xterm CSI modified-F keys) and "kon" (KONSOLE_VERSION set so biew swaps
in konsole-256color -> Konsole SS3 modified-F1-F4 keys decode).  The PTY
answers the few terminal queries ncurses emits at init (cursor position
and any others), the same replies a real terminal would send.

Usage:
    drive.py --bin /path/to/biew [--bin ...] [--filter REGEX] [--list]

Note: the modifier-legend switch (Shift/Ctrl/Alt held alone) cannot be
exercised from a PTY — terminals never encode bare modifiers.  That path
is driven by the Linux-console keymap (on_console) or kitty-keyboard
events, so it is not part of this matrix.
"""

import fcntl
import os
import re
import select
import struct
import sys
import termios
import time

# ---------------------------------------------------------------- keys ----

ESC = b"\x1b"

KEYS = {
    "esc":    ESC,
    "enter":  b"\r",
    "tab":    b"\t",
    "space":  b" ",
    "up":     b"\x1b[A",
    "down":   b"\x1b[B",
    "right":  b"\x1b[C",
    "left":   b"\x1b[D",
    "home":   b"\x1b[H",
    "end":    b"\x1b[F",
    "pgup":   b"\x1b[5~",
    "pgdn":   b"\x1b[6~",
    "ins":    b"\x1b[2~",
    "del":    b"\x1b[3~",
    "bs":     b"\x7f",
}

_FNUM = {1: "OP", 2: "OQ", 3: "OR", 4: "OS",
         5: "[15~", 6: "[17~", 7: "[18~", 8: "[19~",
         9: "[20~", 10: "[21~", 11: "[23~", 12: "[24~"}
_FMODNUM = {5: 15, 6: 17, 7: 18, 8: 19, 9: 20, 10: 21, 11: 23, 12: 24}

for _n in range(1, 13):
    KEYS["f%d" % _n] = ESC + _FNUM[_n].encode()
    if _n <= 4:
        _fin = b"PQRS"[_n - 1:_n]
        KEYS["sf%d" % _n]  = b"\x1b[1;2" + _fin       # xterm modifier form
        KEYS["cf%d" % _n]  = b"\x1b[1;5" + _fin
        KEYS["af%d" % _n]  = b"\x1b[1;3" + _fin       # xterm Alt form
        KEYS["ma%d" % _n]  = b"\x1b\x1bO" + _fin      # meta prefix
        KEYS["osf%d" % _n] = b"\x1bO2" + _fin         # SS3-Shift form
        KEYS["ocf%d" % _n] = b"\x1bO5" + _fin         # SS3-Ctrl form (Konsole)
    else:
        _num = str(_FMODNUM[_n]).encode()
        KEYS["sf%d" % _n] = b"\x1b[%s;2~" % _num
        KEYS["cf%d" % _n] = b"\x1b[%s;5~" % _num
        KEYS["af%d" % _n] = b"\x1b[%s;3~" % _num
        KEYS["ma%d" % _n] = b"\x1b\x1b[%s~" % _num
for _c in "abcdefghijklmnopqrstuvwxyz0123456789":
    KEYS[_c] = _c.encode()
    KEYS["a-" + _c] = ESC + _c.encode()               # Alt+char = ESC char

# kitty keyboard protocol (CSI u) encodings — used in the "kit" profile
# where the harness answers biew's \x1b[?u probe and all input is CSI u.
_KF = {i: 57363 + i for i in range(1, 13)}
KEYS.update({
    "kesc":   b"\x1b[27u",
    "kenter": b"\x1b[13u",
    "ktab":   b"\x1b[9u",
    "kbs":    b"\x1b[127u",
    "kup":    b"\x1b[57352u",
    "kdown":  b"\x1b[57353u",
    "kleft":  b"\x1b[57350u",
    "kright": b"\x1b[57351u",
    "khome":  b"\x1b[57356u",
    "kend":   b"\x1b[57357u",
    "kpgup":  b"\x1b[57354u",
    "kpgdn":  b"\x1b[57355u",
    "kins":   b"\x1b[57348u",
    "kdel":   b"\x1b[57349u",
    # bare modifier press (:1) / release (:3) — the legend-switch events
    "ksh_dn": b"\x1b[57441;1:1u", "ksh_up": b"\x1b[57441;1:3u",
    "kct_dn": b"\x1b[57442;1:1u", "kct_up": b"\x1b[57442;1:3u",
    "kal_dn": b"\x1b[57443;1:1u", "kal_up": b"\x1b[57443;1:3u",
    "krel_a": b"\x1b[97;1:3u",                            # 'a' release — ignored
    "k_a":    b"\x1b[97u",                                # plain 'a'
    "k_A":    b"\x1b[97:65;2u",                           # shifted 'a' → 'A'
    "kc_a":   b"\x1b[97;5u",                              # ctrl-a
    "ka_o":   b"\x1b[111;3u",                             # alt-o
})
for _n in range(1, 13):
    _k = str(_KF[_n]).encode()
    KEYS["kf%d" % _n]  = b"\x1b[" + _k + b"u"
    KEYS["ksf%d" % _n] = b"\x1b[" + _k + b";2u"       # shift
    KEYS["kcf%d" % _n] = b"\x1b[" + _k + b";5u"       # ctrl
    KEYS["kaf%d" % _n] = b"\x1b[" + _k + b";3u"       # alt

# SGR mouse (1006): press 'M' / release 'm', 1-based coords.  The legend
# row is y=25 and each prompt button is 8 columns wide, so button n
# occupies x = (n-1)*8+1 .. (n-1)*8+8 (F10 = everything >= 73).
for _i in range(10):
    _x = _i * 8 + 4                                     # inside button i+1
    KEYS["mpf%d" % (_i + 1)] = b"\x1b[<0;%d;25M" % _x   # press on legend
    KEYS["mrf%d" % (_i + 1)] = b"\x1b[<0;%d;25m" % _x   # release on legend
KEYS["mroff"] = b"\x1b[<0;36;15m"    # release above the legend (drag off)
KEYS["mpdlg"] = b"\x1b[<0;40;2M"     # press/release outside a centered box
KEYS["mrdlg"] = b"\x1b[<0;40;2m"
KEYS["mwup"]  = b"\x1b[<64;40;12M"   # wheel up / down mid-screen
KEYS["mwdn"]  = b"\x1b[<65;40;12M"

# ------------------------------------------------------- vt100 scraper ----

DECSG = {"q": "─", "x": "│", "j": "┘", "k": "┐", "l": "┌", "m": "└",
         "t": "├", "u": "┤", "v": "┴", "w": "┬", "n": "┼", "a": "▒",
         "h": "░", "f": "°", "g": "±", "o": "■", "i": "◘", "`": "◆"}

class VtScreen:
    """Bare-minimum vt100: CSI cursor/erase, SO/SI DEC-graphics, OSC."""

    def __init__(self, rows=25, cols=80):
        self.rows, self.cols = rows, cols
        self.g = [[" "] * cols for _ in range(rows)]
        self.r = self.c = 0
        self.gs1 = False

    def _put(self, ch):
        if self.gs1 and ch in DECSG:
            ch = DECSG[ch]
        if 0 <= self.r < self.rows and 0 <= self.c < self.cols:
            self.g[self.r][self.c] = ch
        self.c += 1

    def feed(self, data):
        s = data.decode("utf-8", "replace")
        i, n = 0, len(s)
        while i < n:
            ch = s[i]
            if ch == "\x1b":
                m = re.match(r"\x1b[P_X][^\x1b]*\x1b\\", s[i:])
                if m:                                   # DCS/APC ... ST
                    i += m.end()
                    continue
                m = re.match(r"\x1b\[([0-9;?]*)([A-Za-z@`~])", s[i:])
                if m:
                    i += m.end()
                    self._csi(*m.groups())
                    continue
                m = re.match(r"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)", s[i:])
                if m:                                   # OSC ... BEL/ST
                    i += m.end()
                    continue
                m = re.match(r"\x1b[()#][0-9A-Ba-z]", s[i:])
                if m:                                   # charset select
                    i += m.end()
                    continue
                m = re.match(r"\x1b[=>MEHc78<]", s[i:])
                if m:
                    i += m.end()
                    continue
                i += 1
                continue
            if ch == "\x0e":    self.gs1 = True;  i += 1; continue
            if ch == "\x0f":    self.gs1 = False; i += 1; continue
            if ch == "\r":      self.c = 0;       i += 1; continue
            if ch == "\n":
                self.r += 1
                if self.r >= self.rows:
                    self.g.pop(0)
                    self.g.append([" "] * self.cols)
                    self.r = self.rows - 1
                i += 1; continue
            if ch == "\b":      self.c = max(0, self.c - 1); i += 1; continue
            if ch in "\x07\x1a\x00\x01\x02\x03\x04\x05\x06\x08\x0b\x0c\x0d":
                i += 1; continue
            self._put(ch)
            i += 1

    def _csi(self, params, final):
        p = params.split(";") if params else []
        try:
            a = int(p[0]) if p and p[0] else 0
            b = int(p[1]) if len(p) > 1 and p[1] else 0
        except ValueError:
            a = b = 0
        if final in "Hf":
            self.r = max(0, (a or 1) - 1)
            self.c = max(0, (b or 1) - 1)
        elif final == "A": self.r = max(0, self.r - (a or 1))
        elif final == "B": self.r = min(self.rows - 1, self.r + (a or 1))
        elif final == "C": self.c = min(self.cols - 1, self.c + (a or 1))
        elif final == "D": self.c = max(0, self.c - (a or 1))
        elif final == "G": self.c = max(0, (a or 1) - 1)
        elif final == "d": self.r = max(0, (a or 1) - 1)
        elif final == "J":
            if a == 2:
                self.g = [[" "] * self.cols for _ in range(self.rows)]
            elif a == 0:
                for x in range(self.c, self.cols): self.g[self.r][x] = " "
                for y in range(self.r + 1, self.rows):
                    self.g[y] = [" "] * self.cols
        elif final == "K":
            for x in range(self.c, self.cols): self.g[self.r][x] = " "
        elif final == "L":
            for _ in range(a or 1):
                self.g.insert(self.r, [" "] * self.cols)
                self.g.pop()
        elif final == "M":
            for _ in range(a or 1):
                self.g.pop(self.r)
                self.g.append([" "] * self.cols)

    def text(self):
        return "\n".join("".join(r).rstrip() for r in self.g)

    def row(self, n):                                   # 0-based
        return "".join(self.g[n]).rstrip()

    def legend(self):
        return self.row(self.rows - 1)


# -------------------------------------------------------------- driver ----

# ncurses sends a cursor-position query (u7) during init and may probe a
# few more capabilities; emulate the replies a real terminal would send
# so nothing stalls and no reply leaks into the key stream.
QFIX = [
    (re.compile(rb"\x1b\[c"),        b"\x1b[?1;2c"),      # DA1
    (re.compile(rb"\x1b\[>c"),       b"\x1b[>1;0;0c"),    # DA2
    (re.compile(rb"\x1b\[=c"),       b"\x1b[=0;0c"),      # DA3
    (re.compile(rb"\x1b\[>[0-9;]*q"),b"\x1b[>0;0q"),      # XTSMCRTSTYLE
    (re.compile(rb"\x1b\[\?1;1;0S"), b"\x1b[?1;0;0S"),    # sixel queries
    (re.compile(rb"\x1b\[\?2;1;0S"), b"\x1b[?2;0;0S"),
    (re.compile(rb"\x1b\[\?3;1;0S"), b"\x1b[?3;0;0S"),
    (re.compile(rb"\x1b\[14t"),      b"\x1b[4;400;640t"), # text area px
    (re.compile(rb"\x1b\[16t"),      b"\x1b[6;16;8t"),    # cell size px
    (re.compile(rb"\x1b\[18t"),      b"\x1b[8;25;80t"),   # text area chars
    (re.compile(rb"\x1b\[6n"),       b"\x1b[1;1R"),       # cursor pos
    (re.compile(rb"\x1b\[5n"),       b"\x1b[0n"),         # status ok
]
# queries whose reply must echo a parameter from the request
QDYN = [
    (re.compile(rb"\x1b\[\?([0-9;]+)\$p"),
     lambda m: b"\x1b[?" + m.group(1) + b";0$y"),          # DECRQM -> off
    (re.compile(rb"\x1b\]4;([0-9]+);\?\x1b\\"),
     lambda m: b"\x1b]4;" + m.group(1) + b";rgb:0000/0000/0000\x1b\\"),
    (re.compile(rb"\x1b\]1[012];\?\x1b\\"),
     lambda m: m.group(0)[:-3] + b"rgb:ffff/ffff/ffff\x1b\\"),
    (re.compile(rb"\x1bP\+q[0-9a-fA-F]+\x1b\\"),
     lambda m: b"\x1bP1+r\x1b\\"),                         # XTGETTCAP
]

class Sess:
    def __init__(self, argv, env):
        import pty
        env = dict(env)
        env["BIEW_INI"] = _test_ini()
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            try:
                os.execvpe(argv[0], argv, env)
            except Exception:
                os._exit(127)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", 25, 80, 0, 0))
        os.set_blocking(self.fd, False)
        self.scr = VtScreen()
        self.buf = b""
        self._dead = False
        self._qseen = set()
        self.kitty = env.get("BIEW_TEST_KITTY") == "1"

    def _answer_queries(self):
        qs = list(QFIX) + list(QDYN)
        if self.kitty:
            # kitty kbd queries: ?u query, >u push, =flags u request —
            # a real xterm implements none of these and stays silent
            qs += [(re.compile(rb"\x1b\[\?u"), b"\x1b[?0u"),
                   (re.compile(rb"\x1b\[>u"),  b"\x1b[?0u"),
                   (re.compile(rb"\x1b\[=[0-9]+u"), b"\x1b[?0u")]
        for pat, rep in qs:
            for m in pat.finditer(self.buf):
                q = m.group(0)
                if q in self._qseen:
                    continue
                self._qseen.add(q)
                try:
                    os.write(self.fd, rep(m) if callable(rep) else rep)
                except OSError:
                    return

    def pump(self, settle=0.30):
        """Read output until quiet for `settle` seconds."""
        last = time.time()
        while True:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    d = os.read(self.fd, 65536)
                except OSError:
                    break
                if not d:
                    break
                self.buf += d
                self._answer_queries()
                self.scr.feed(d)
                last = time.time()
            elif time.time() - last >= settle:
                break

    def key(self, k):
        try:
            os.write(self.fd, k)
        except OSError:
            pass

    def alive(self):
        if self._dead:
            return False
        try:
            pid, _ = os.waitpid(self.pid, os.WNOHANG)
        except ChildProcessError:
            self._dead = True
            return False
        if pid != 0:
            self._dead = True
            return False
        return True

    def close(self):
        try:
            os.kill(self.pid, 9)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except ChildProcessError:
            pass
        try:
            os.close(self.fd)
        except OSError:
            pass


# ------------------------------------------------------------- cases ------
#
# Case fields:
#   steps       list of ("key", name[, settle])
#   expect      substrings that must ALL appear on the final screen;
#               "@@diff" = screen differs from startup, "@@same" = identical
#   legend      substrings that must ALL appear on the bottom (legend) row
#   absent      substrings that must NOT appear anywhere
#   post        optional second (steps, expect, legend) checked after the
#               first phase — used to verify dialogs close on ESC
#   expect_exit the process must be dead after `steps`
#   enc         key encoding the case exercises:
#               "xt" = xterm CSI modified keys, "kon" = Konsole SS3-modified;
#               None runs in both passes.

TESTFILE = os.environ.get("BIEW_TEST_FILE", "/tmp/t.com")

# Hermetic ini: biew persists mode state (< Browser > section) to its rc
# on clean exit, so an ambient ~/.biewrc silently changes the startup
# mode and help-topic between runs.  Give every session a private copy
# with the mode-state section stripped -> deterministic autodetect.
def _test_ini():
    src = os.environ.get("BIEW_INI") or os.path.expanduser("~/.biewrc")
    dst = "/tmp/biew-drive.ini"
    try:
        txt = open(src).read()
    except OSError:
        txt = ""
    # drop the whole < Browser > section (mode/submode/offset state):
    # without it biew autodetects the mode from file content on startup
    txt = re.sub(r"(?ms)^\s*< Browser >.*?(?=^\s*< |^\[ |\Z)", "", txt)
    with open(dst, "w") as f:
        f.write(txt)
    return dst

# Probe matrix: every F-key x modifier, sent sequentially in ONE session.
# After each key the runner classifies it (opens / moves / inert / exits)
# and, when a dialog/menu was opened (legend row changed), sends ESC and
# asserts the base legend row is fully restored.  f10 goes last: it quits.
PROBE_KEYS = (["f%d" % i for i in list(range(1, 10)) + [11, 12]] +
              ["sf%d" % i for i in range(1, 13)] +
              ["cf%d" % i for i in range(1, 13)] +
              ["af%d" % i for i in range(1, 11)] +
              ["f10"])

# mode-select step lists: F4's selection box opens at the remembered
# index (0 = Text in a fresh process), arrows clamp at the edges.
TEXT_STEPS   = [("key", "f4", 0.8), ("key", "enter", 0.9)]
DISASM_STEPS = [("key", "f4", 0.8), ("key", "down", 0.3), ("key", "down", 0.3),
                ("key", "down", 0.3), ("key", "enter", 0.9)]
# kitty-encoded equivalents (used by enc="kit" cases)
TEXT_STEPS_K   = [("key", "kf4", 0.8), ("key", "kenter", 0.9)]
DISASM_STEPS_K = [("key", "kf4", 0.8), ("key", "kdown", 0.3), ("key", "kdown", 0.3),
                  ("key", "kdown", 0.3), ("key", "kenter", 0.9)]


def probe_matrix(s, keys, settle=0.4):
    """Send each key in `keys`; classify and enforce the legend invariant.

    Returns (results, error): results maps key -> class where class is
    "opens" (legend changed, ESC restored it), "moves" (screen changed,
    legend unchanged — navigation), "inert" (no visible effect) or
    "exits" (key closed the app).  error is a failure string or None.
    """
    base_leg = s.scr.legend()
    base_scr = s.scr.text()
    res = {}
    for k in keys:
        if not s.alive():
            return res, "died before %s" % k
        s.key(KEYS[k])
        s.pump(settle)
        if not s.alive():
            res[k] = "exits"
            continue
        if s.scr.legend() != base_leg:
            # a dialog/menu opened: ESC must close it AND restore the legend
            s.key(ESC)
            s.pump(settle)
            if not s.alive():
                return res, "%s: ESC after dialog killed the app" % k
            if s.scr.legend() != base_leg:
                return res, "%s: legend stuck at %r after ESC" % (
                    k, s.scr.legend()[:60])
            res[k] = "opens"
        elif s.scr.text() != base_scr:
            res[k] = "moves"
        else:
            res[k] = "inert"
    return res, None


# coverage collected by probe cases, printed at the end of the run
PROBE_COVERAGE = {}

def cases():
    E = []

    def add(name, steps, expect=(), legend=(), absent=(), post=None,
            expect_exit=False, enc=None, probe=None, pre=()):
        E.append(dict(name=name, steps=steps, expect=expect, legend=legend,
                      absent=absent, post=post, expect_exit=expect_exit, pre=pre,
                      enc=enc, probe=probe))

    # -- startup / legend -------------------------------------------------
    add("startup", [],
        legend=["1Help", "5Goto", "10Quit"])

    # -- dialogs: open, verify, ESC back to main legend --------------------
    # t.com autodetects text mode: F1 -> mode help topic, Shift-F1 -> About;
    # disasm mode (DISASM_STEPS): Shift-F1 -> disassembler select,
    # Ctrl-F1 -> bitness select (Hiew bindings)
    add("help-f1",       [("key", "f1", 0.8)],
        expect=["Help for"], legend=["7Search"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("about-sf1",     [("key", "sf1", 0.8)],
        expect=["Binary Viewer"], legend=["1Licenc"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("dis-sel-sf1",   DISASM_STEPS + [("key", "sf1", 0.8)],
        expect=["Select disassembler"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("dis-sel-sf1-ss3", DISASM_STEPS + [("key", "osf1", 0.8)],
        expect=["Select disassembler"], enc="kon",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("cf1-bitness",      DISASM_STEPS + [("key", "cf1", 0.8)],
        expect=["Select bitness mode"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("cf1-bitness-kon",  DISASM_STEPS + [("key", "ocf1", 0.8)],
        expect=["Select bitness mode"], enc="kon",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("goto-f5",       [("key", "f5", 0.8)],
        expect=["UsrNam"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("search-f7",     [("key", "f7", 0.8)],
        expect=["CasSen", "WrdOnl"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("fileutils-cf9", [("key", "cf9", 0.9)],
        expect=["File utilities"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("fileutils-af10",[("key", "af10", 0.9)],
        expect=["File utilities"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("setup-sf9",     [("key", "sf9", 0.9)],
        legend=["Consol", "Color", "Mouse"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))

    # -- mode / navigation -------------------------------------------------
    add("mode-f4",       [("key", "f4", 0.7)],
        expect=["Select translation mode", "Text mode", "Disassembler"],
        legend=["10Es"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("mode-enter",    [("key", "enter", 0.7)], expect=["@@diff"])
    add("cursor-down",   [("key", "down", 0.4), ("key", "down", 0.4)],
        expect=["@@diff"])
    add("cursor-pgdn",   [("key", "pgdn", 0.6)], expect=["@@diff"])
    # pgdn/pgup must round-trip; binary mode (16-byte rows) makes it
    # byte-exact — text mode wraps lines, so a page is not reversible there
    add("nav-pgup-return",[("key", "pgdn", 0.5), ("key", "pgdn", 0.4),
                          ("key", "pgup", 0.4), ("key", "pgup", 0.4)],
        pre=[("key", "f4", 0.7), ("key", "down", 0.3), ("key", "enter", 0.9)],
        expect=["@@same"])

    # -- mode selection via the F4 menu (defsel starts at Text=0) ----------
    add("mode-sel-bin",  [("key", "f4", 0.6), ("key", "down", 0.3),
                          ("key", "enter", 0.8)],
        expect=["00000000: 00 01 02 03"])
    add("mode-sel-hex",  [("key", "f4", 0.6), ("key", "down", 0.3),
                          ("key", "down", 0.3), ("key", "enter", 0.8)],
        expect=["00000000:i0001"])
    add("mode-sel-dis",  DISASM_STEPS, expect=["00000000:i000"])
    # Enter = QuickSelectMode: four presses cycle all modes back to start
    add("mode-cycle-enter", [("key", "enter", 0.6), ("key", "enter", 0.6),
                             ("key", "enter", 0.6), ("key", "enter", 0.7)],
        expect=["@@same"])

    # -- menus: open, verify contents + the menu legend, ESC restores ------
    add("menu-files-cf9", [("key", "cf9", 0.9)],
        expect=["File information", "Save block as", "Endianify block"],
        legend=["10Es"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("menu-sysinfo-sf6", [("key", "sf6", 0.9)],
        expect=["Select tool", "ASCII table", "Input viewer"],
        legend=["10Es"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("whereami-sf5", [("key", "sf5", 0.9)],
        expect=["Current position information", "File  offset"],
        legend=["10Es"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("fmtactions-sf8", [("key", "sf8", 0.9)],
        legend=["2SrtNam", "3SrtOrd", "10Es"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("addname-sf12", [("key", "sf12", 0.9)],
        expect=["Name for", "offset"], legend=["10Es"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("setup-legend-sf9", [("key", "sf9", 0.9)],
        legend=["2Consol", "3Color", "4Mouse", "9ExtPrg", "10Es"], enc="xt",
        post=([("key", "esc", 0.6)], ["1Help"], ()))

    # -- menu navigation: arrows move selection, Enter picks a mode --------
    add("menu-mode-nav", [("key", "f4", 0.6), ("key", "down", 0.3),
                          ("key", "down", 0.3), ("key", "up", 0.3),
                          ("key", "enter", 0.8)],
        expect=["00000000: 00 01 02 03"])           # down,down,up -> Binary
    # FileUtils: open, select first item (File information) -> sub-dialog
    add("menu-fu-select", [("key", "cf9", 0.9), ("key", "enter", 0.9)],
        enc="xt", expect=["@@diff"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))

    # -- mouse: SGR clicks on the legend row fire the prompt buttons -----
    add("mclick-f1",  [("key", "mpf1", 0.4), ("key", "mrf1", 0.9)],
        expect=["Help for"], legend=["7Search"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("mclick-f5",  [("key", "mpf5", 0.4), ("key", "mrf5", 0.9)],
        expect=["Enter offset"], legend=["10Es"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    # press on the legend but release off it = drag away -> no action
    add("mclick-drag",[("key", "mpf5", 0.4), ("key", "mroff", 0.9)],
        expect=["@@same"], legend=["1Help"])
    # click outside an open viewer dismisses it (KE_ESCAPE path)
    add("mclick-dismiss", [("key", "f1", 0.9), ("key", "mpdlg", 0.4),
                           ("key", "mrdlg", 0.9)],
        legend=["1Help"], absent=["Help for"])
    # wheel events on the content area scroll like arrow keys
    add("mwheel",     [("key", "mwdn", 0.5), ("key", "mwdn", 0.4)],
        expect=["@@diff"])
    add("mclick-quit", [("key", "mpf10", 0.4), ("key", "mrf10", 0.8)],
        expect_exit=True)
    # same clicks through the kitty CSI u decoder's SGR path
    add("mclick-f1-kit",  [("key", "mpf1", 0.4), ("key", "mrf1", 0.9)],
        expect=["Help for"], legend=["7Search"], enc="kit",
        post=([("key", "kesc", 0.6)], ["1Help"], ()))
    add("mclick-drag-kit",[("key", "mpf5", 0.4), ("key", "mroff", 0.9)],
        expect=["@@same"], legend=["1Help"], enc="kit")

    # -- kitty keyboard protocol (CSI u) ---------------------------------
    # modifier-only press/release switches the legend, like on console
    add("kitty-shift-legend", [("key", "ksh_dn", 0.8)],
        expect=["About", "SysInf", "NavFmt"], enc="kit",
        post=([("key", "ksh_up", 0.6)], ["1Help"], ()))
    add("kitty-ctrl-legend",  [("key", "kct_dn", 0.8)],
        expect=["BinMod", "ReRead"], enc="kit",
        post=([("key", "kct_up", 0.6)], ["1Help"], ()))
    add("kitty-alt-legend",   [("key", "kal_dn", 0.8)],
        expect=["Reload", "Dir"], enc="kit",
        post=([("key", "kal_up", 0.6)], ["1Help"], ()))
    # modifier is still held while the F-key event arrives
    add("kitty-shift-f1", [("key", "ksh_dn", 0.5), ("key", "ksf1", 0.8)],
        expect=["Binary Viewer"], enc="kit",
        post=([("key", "ksh_up", 0.3), ("key", "kesc", 0.6)], ["1Help"], ()))
    add("kitty-cf1-bitness", DISASM_STEPS_K + [("key", "kcf1", 0.8)],
        expect=["Select bitness mode"], enc="kit",
        post=([("key", "kesc", 0.6)], ["1Help"], ()))
    # ordinary keys through CSI u
    add("kitty-esc-quit",  [("key", "kesc", 1.0)], expect_exit=True, enc="kit")
    add("kitty-enter",     [("key", "kenter", 0.7)], expect=["@@diff"], enc="kit")
    add("kitty-down",      [("key", "kdown", 0.4), ("key", "kdown", 0.4)],
        expect=["@@diff"], enc="kit")
    add("kitty-release",   [("key", "krel_a", 0.5)], expect=["@@same"], enc="kit")

    # -- comprehensive probe: every F-key x modifier in text + disasm modes
    # (the startup mode depends on ini LastMode, so pick it explicitly)
    add("probe-text",   TEXT_STEPS, probe=list(PROBE_KEYS), enc="xt")
    add("probe-disasm", DISASM_STEPS, probe=list(PROBE_KEYS), enc="xt")

    # -- clean exits -------------------------------------------------------
    add("quit-f10",  [("key", "f10", 0.5)], expect_exit=True)
    add("quit-esc",  [("key", "esc", 0.5)], expect_exit=True)
    return E


# --------------------------------------------------------------- run ------

def eval_screen(sess, expect, legend, absent, base):
    scr = sess.scr.text()
    leg = sess.scr.legend()
    missing = []
    for e in expect:
        if e == "@@diff":
            if scr == base:
                missing.append("@@diff")
        elif e == "@@same":
            if scr != base:
                missing.append("@@same")
        elif e not in scr:
            missing.append(e)
    for e in legend:
        if e not in leg:
            missing.append("legend:%s" % e)
    present = [a for a in absent if a in scr]
    return missing, present, scr, leg


def run_case(argv, env, case, wait=3.0):
    """Run one case in a fresh session.  Returns (ok, msg)."""
    s = Sess(argv, env)
    try:
        s.pump(1.2)                                   # initial render
        # wait for the app to finish init (its TCSAFLUSH drops early keys)
        for _ in range(20):
            if "1Help" in s.scr.legend() or not s.alive():
                break
            s.pump(0.1)
        time.sleep(0.3)
        s.pump(0.05)
        for step in case["pre"]:
            s.key(KEYS[step[1]])
            s.pump(step[2] if len(step) > 2 else 0.30)
        base = s.scr.text()
        for step in case["steps"]:
            s.key(KEYS[step[1]])
            s.pump(step[2] if len(step) > 2 else 0.30)
        # poll: app redraw can lag behind key delivery
        deadline = time.time() + wait
        while True:
            if case["expect_exit"]:
                missing, present, scr, leg = [], [], s.scr.text(), s.scr.legend()
                if s.alive():
                    missing.append("exit")
            else:
                missing, present, scr, leg = eval_screen(
                    s, case["expect"], case["legend"], case["absent"], base)
                if not s.alive():
                    missing.append("crashed")
            if (not missing and not present) or time.time() > deadline:
                break
            s.pump(0.15)
        if missing or present:
            return False, "missing=%r present=%r legend=%r" % (
                missing, present, leg[:72])
        if case["probe"]:
            res, err = probe_matrix(s, case["probe"])
            PROBE_COVERAGE[case["name"]] = res
            if err:
                return False, "probe: %s" % err
        if case["post"]:
            psteps, pexpect, plegend = case["post"]
            for step in psteps:
                s.key(KEYS[step[1]])
                s.pump(step[2] if len(step) > 2 else 0.30)
            missing, present, scr, leg = eval_screen(
                s, pexpect, plegend, (), base)
            if missing or present:
                return False, "post missing=%r legend=%r" % (missing, leg[:72])
        return True, ""
    finally:
        s.close()


def profiles_for(cmd):
    """(tag, env_overrides, enc) list — two passes: xterm-256color, then
    the KONSOLE_VERSION -> konsole-256color terminfo override pass."""
    if os.environ.get("BIEW_TEST_TERM"):
        return [("", {"TERM": os.environ["BIEW_TEST_TERM"]}, "xt")]
    return [("xt",  {"TERM": "xterm-256color"}, "xt"),
            ("kit", {"TERM": "xterm-256color", "BIEW_TEST_KITTY": "1"}, "kit"),
            ("kon", {"TERM": "xterm-256color", "KONSOLE_VERSION": "1"},
             "kon")]


def main():
    bins = []
    filt = None
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--bin":
            i += 1; bins.append(args[i])
        elif args[i] == "--filter":
            i += 1; filt = re.compile(args[i])
        elif args[i] == "--list":
            for c in cases():
                print(c["name"])
            return 0
        i += 1
    if not bins:
        print("need --bin", file=sys.stderr)
        return 2

    results = {}
    for cmd in bins:
        argv = cmd.split() + [TESTFILE]
        for ptag, penv, penc in profiles_for(cmd):
            tag = cmd.split()[-1] if " " in cmd else os.path.basename(cmd)
            if ptag:
                tag += ":" + ptag
            results[tag] = []
            env = dict(os.environ)
            env.update(penv)
            for case in cases():
                name = case["name"]
                if filt and not filt.search(name):
                    continue
                if (case["enc"] or "xt") != penc:
                    continue
                ok, msg = run_case(argv, env, case)
                for _ in range(2):                    # flaky-input retries
                    if ok:
                        break
                    ok, msg = run_case(argv, env, case)
                results[tag].append((name, ok))
                print("%-18s %-24s %s%s" % (
                    tag, name, "PASS" if ok else "FAIL",
                    ("  " + msg) if not ok else ""))
                sys.stdout.flush()

    if PROBE_COVERAGE:
        print()
        for cname, res in PROBE_COVERAGE.items():
            print("%s:" % cname)
            for klass in ("opens", "moves", "inert", "exits"):
                ks = [k for k, v in res.items() if v == klass]
                print("  %-6s %s" % (klass, " ".join(ks) or "-"))
    print()
    fails = sum(1 for r in results.values() for (_, ok) in r if ok is False)
    total = sum(len(r) for r in results.values())
    print("%d/%d passed%s" % (
        total - fails, total,
        "  — %d FAILURES" % fails if fails else ""))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
