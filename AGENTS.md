# biew — agent notes

Binary vIEW: a Hiew-like hex/text/disasm viewer-editor. Source lives in
`src/` (upstream CVS layout). Primary target: 64-bit Linux (x86-64).
History: SourceForge CVS (beye) imported to git — see commits back to 2001.

## Build

```sh
cd src
./configure            # autodetects arch/os; writes config.mak
make -j8
./biew <file>
```

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
