#!/usr/bin/env python3
"""PTY-driven TUI test harness for biew.

Spawns a binary under a pseudo-terminal, feeds key bytes, scrapes the
resulting screen with a minimal vt100 emulator and asserts expected
text.  Every case gets a FRESH session, so a case that exits or leaves a
dialog open cannot cascade into later failures.  The SAME case list runs
against every binary given with --bin, so behavioral divergence (cosmo
APE vs glibc build) is caught by direct comparison.

The vt100/raw-parser path is exercised by running the native build with
TERM=dumb (notcurses declines it) and the APE with TERM=vt100 (it refuses
'dumb' outright).  Invoke the APE as `sh biew.com`: on hosts where a
binfmt_misc MZ handler (e.g. kvikdos) is registered, a direct exec is
routed through that interpreter's polyglot hop and can be flaky.

Usage:
    drive.py --bin /path/to/biew [--bin ...] [--filter REGEX] [--list]
    drive.py --bin ./biew --bin "sh ./biew.com"  # compare native vs APE

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

class Sess:
    def __init__(self, argv, env):
        import pty
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

TESTFILE = os.environ.get("BIEW_TEST_FILE", "/tmp/t.com")

def cases():
    E = []

    def add(name, steps, expect=(), legend=(), absent=(), post=None,
            expect_exit=False):
        E.append(dict(name=name, steps=steps, expect=expect, legend=legend,
                      absent=absent, post=post, expect_exit=expect_exit))

    # -- startup / legend -------------------------------------------------
    add("startup", [],
        legend=["1Help", "5Goto", "10Quit"])

    # -- dialogs: open, verify, ESC back to main legend --------------------
    # t.com opens in disasm mode: F1 -> disasm help topic,
    # Shift-F1 -> disassembler select, Ctrl-F1 -> bitness select (Hiew)
    add("help-f1",       [("key", "f1", 0.8)],
        expect=["How to see"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("about-sf1",     [("key", "sf1", 0.8)],
        expect=["Select disassembler"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("about-sf1-ss3", [("key", "osf1", 0.8)],
        expect=["Select disassembler"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("cf1-bitness",      [("key", "cf1", 0.8)],
        expect=["Select bitness mode"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("cf1-bitness-kon",  [("key", "ocf1", 0.8)],
        expect=["Select bitness mode"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("goto-f5",       [("key", "f5", 0.8)],
        expect=["UsrNam"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("search-f7",     [("key", "f7", 0.8)],
        expect=["CasSen", "WrdOnl"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("fileutils-cf9", [("key", "cf9", 0.9)],
        expect=["File utilities"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("fileutils-af10",[("key", "af10", 0.9)],
        expect=["File utilities"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))
    add("setup-sf9",     [("key", "sf9", 0.9)],
        legend=["Consol", "Color", "Mouse"],
        post=([("key", "esc", 0.6)], ["1Help"], ()))

    # -- mode / navigation -------------------------------------------------
    add("mode-f4",       [("key", "f4", 0.7)], expect=["@@diff"])
    add("mode-enter",    [("key", "enter", 0.7)], expect=["@@diff"])
    add("cursor-down",   [("key", "down", 0.4), ("key", "down", 0.4)],
        expect=["@@diff"])
    add("cursor-pgdn",   [("key", "pgdn", 0.6)], expect=["@@diff"])
    add("nav-pgup-return",[("key", "pgdn", 0.5), ("key", "pgdn", 0.4),
                          ("key", "pgup", 0.4), ("key", "pgup", 0.4)],
        expect=["@@same"])

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


def term_for(cmd):
    """APE refuses TERM=dumb; native falls back to vt100 path on it."""
    if os.environ.get("BIEW_TEST_TERM"):
        return os.environ["BIEW_TEST_TERM"]
    return "vt100" if cmd.rstrip().endswith(".com") else "dumb"


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
        tag = cmd.split()[-1] if " " in cmd else os.path.basename(cmd)
        results[tag] = []
        argv = cmd.split() + [TESTFILE]
        env = dict(os.environ)
        env["TERM"] = term_for(cmd)
        for case in cases():
            name = case["name"]
            if filt and not filt.search(name):
                continue
            ok, msg = run_case(argv, env, case)
            for _ in range(2):                        # flaky-input retries
                if ok:
                    break
                ok, msg = run_case(argv, env, case)
            results[tag].append((name, ok))
            print("%-18s %-24s %s%s" % (
                tag, name, "PASS" if ok else "FAIL",
                ("  " + msg) if not ok else ""))
            sys.stdout.flush()

    print()
    fails = sum(1 for r in results.values() for (_, ok) in r if ok is False)
    total = sum(len(r) for r in results.values())
    print("%d/%d passed%s" % (
        total - fails, total,
        "  — %d FAILURES" % fails if fails else ""))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
