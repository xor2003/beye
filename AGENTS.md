# biew — agent notes

Binary vIEW: a Hiew-like hex/text/disasm viewer-editor. Source lives in
`src/` (upstream CVS layout). Primary target: 64-bit Linux (x86-64).
History: SourceForge CVS (beye) imported to git — see commits back to 2001.

## Build

Primary build: native gcc + ncurses (this is the default):

```sh
cd src
./configure            # autodetects arch/os; writes config.mak
make -j8
./biew <file>
```

Install (user-local, no sudo — lands at `~/.local/bin/biew` on PATH):

```sh
make install PREFIX=$HOME/.local DATADIR=$HOME/.local/share/biew
```

Data files (hlp/skn/syntax/xlt) resolve via `~/.local/share/biew/` first —
`__get_rc_dir` prefers `$XDG_DATA_HOME/biew` over the compiled-in
`/usr/local/share/biew`.

## Console I/O architecture (native build)

All terminal I/O goes through **ncurses** (`SCREEN *biew_scr` created by
`__init_vio` in `generic/linux/vio.c`) — no direct ANSI code exists.

- `vio.c` renders each cell with `wattr_set`/`waddch`/`waddstr` over a
  shadow buffer; CP437 pseudographics map to UTF-8 (`nc_ps437`) or ACS
  (`_2ps`) on non-UTF-8 terminals; the 16-color VGA palette is installed
  via `init_extended_color`/`init_extended_pair` (272 pairs = 16 fg ×
  17 bg incl. transparent), falling back to nearest-256 then 8 colors.
- `keyboard.c` decodes input on the same screen: `keypad`+`nodelay`+
  `set_escdelay(25)` reassembles split sequences and delivers a lone
  ESC; `define_key` binds the full xterm modifier-F matrix plus both
  CSI and SS3 arrow/nav forms (curses' smkx may never reach the
  terminal, so normal-mode sequences must decode too).
- Konsole detection (`KONSOLE_VERSION`/`KONSOLE_DBUS_SERVICE` +
  `TERM=xterm-256color`) swaps TERM to `konsole-256color` before
  `newterm` so its SS3-modified F1–F4 encodings (`\EO5P` etc.) decode.
- `__init_vio` failure is fatal; `TERM` must resolve (os_dep.c maps
  xterm/screen/tmux/konsole/etc. prefixes to TERM_XTERM, else exits).
- Linux VTs use the `on_console`/vcsa path (kernel console API, not
  escape sequences; ncurses is not started there).

Sanitizer build: `make clean && make SANITIZE=address,undefined`.
Cross-compile/other archs: `./configure --target-arch=arm` etc.; archs
without a sysdep port fall back to `biewlib/sysdep/generic/` + `unix`/`linux`.

## Lint / static analysis

```sh
make compiledb     # compile_commands.json (87 TUs)
make cppcheck      # uses .cppcheck-suppressions
make clang-check   # needs compiledb first
make lint          # both
```

## Runtime setup

biew needs `~/.biewrc` (or `$BIEW_INI`) with `[ Biew ] < Setup >` keys —
`HelpName`, `SkinName`, `SyntaxName` pointing into `src/bin_rc/`
(`biew.hlp`, `skn/standard.skn`, `syntax/syntax.stx`), plus `Version`
matching `BIEW_VERSION` in `src/tstrings.h` or first run opens Setup.
`HiewKeys = yes` enables the Hiew 8.x F-key layout (see HIEW_PARITY.md).
`UseExternalProgs = yes` + `nasm` enables the assembler path.

## PTY smoke test

```sh
(sleep 2; printf '\x1b[21~') | TERM=xterm timeout 15 \
  script -qec "./biew /tmp/test.bin" /tmp/pty.txt
grep -cE "runtime error|AddressSanitizer" /tmp/pty.txt   # expect 0
```

Full interactive matrix (menus, mode/nav keys, dialogs open+close, clean
exits):

```sh
python3 tests/drive.py --bin src/biew
```

Runs two env profiles: `xt` (TERM=xterm-256color) and `kon`
(KONSOLE_VERSION=1 → konsole-256color terminfo swap).

## Repo layout

- `src/` — all code; `src/plugins/bin/*.c` = binary-format plugins,
  `src/plugins/{textmode,hexmode,binmode,disasm}.c` = view modes,
  `src/plugins/disasm/*/` = CPU disassemblers (ix86/arm/ppc/avr/java)
- `src/biewlib/sysdep/<arch>/<os>/` — platform layer; `generic/` = POSIX fallback
- `mainloop.c` — global key dispatch; `info_win.c` — prompt-bar labels;
  `reg_form.h` — `REGISTRY_MODE` (per-mode hooks incl. `misc_action` = F4/F3/F2)
- `bin_rc/` — help/skins/syntax/xlt resources; `doc/` — manuals (en/ru)
- `HIEW_PARITY.md` — feature-gap list vs Hiew 8.x

## Conventions

K&R-ish C89, `tBool`/`__filesize_t`/`tUIntPtr` typedefs from
`biewlib/types.h`; `__FASTCALL__`/`__NEAR__` macros; `UNUSED()` for dead
params. Don't add deps — everything is self-contained.
