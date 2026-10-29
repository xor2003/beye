# BIEW ↔ Hiew 8.x parity

Reference: `HIEW8.HLP` (from hiew32demo), hiew.ru. BIEW = the GNU sibling of
Hiew — same author lineage, same UI model, different key defaults and
gaps. Primary target: 64-bit Linux.

## Already matching

| Hiew | BIEW |
|---|---|
| Text/Hex/Binary/Disasm modes | `plugins/{textmode,hexmode,binmode,disasm}.c` |
| Enter = cycle modes | `QuickSelectMode()` on `KE_ENTER` |
| F5 = Goto (abs/rel/%/VA) | same dialog |
| F7 = Search, Shift-F7 = repeat | same |
| F8 = format header | `detectedFormat->showHdr` |
| F10/Esc = quit | same |
| Alt-F* = format actions | `detectedFormat->action[]` |
| Ctrl-F* = mode actions | `activeMode->action[]` |
| Calculator | `addons/tools/eval.c` |
| User-defined names (F12/Alt-F12) | `udn` in `bin_util.c`, `dialogs.c` |
| Call/jump navigation | `codeguid.c` (`GidGetGoAddress`) |
| >4 GB files | `__filesize_t` (64-bit) |
| Progress bars | `pwnd` percent windows |
| Assembler | external NASM/YASM (ini `UseExternalProgs`) |
| Hiew F-key layout | **new**: ini `[Biew]<Setup> HiewKeys=yes` |

## TODO — worth doing (used features)

| Feature | Hiew key | Where it lands |
|---|---|---|
| Marked-block ops (mark/copy/move/fill/insert/delete/read/write) | `*`, `[]`, Ins, F2/Ctrl-F2/Alt-F2, Shift-F2/5/6 | `plugins/hexmode.c` — biggest real gap |
| Search & replace | Ctrl-F3 | `search.c` replace path |
| Bookmarks | Alt-0..8, `+`/`-`, BkSp | offset table + ini persist |
| Search direction toggle | Alt-F7 | `SF_REVERSE` exists, needs a key |
| String extraction | Alt-F6 | scan + list UI |
| Per-mode F-keys beyond misc | F6 refs/linefeed, F8 xlat, F2 hex-write | needs mode-hook plumbing |
| **Fixup highlighting (MZ/PE)** | — | relocation parsing + overlay in hex/disasm |
| **MZ header table editing** | — | MZ struct field editor (PE editing explicitly out of scope) |

## Won't do (complex × rarely used)

| Feature | Reason |
|---|---|
| HEM external modules | Win32 PE/LE plugin ABI, ~no module authors; BIEW has compiled-in plugins |
| Built-in x86-64/AVX assembler | full encoder; NASM shell-out covers it |
| Physical/logical drive editing | risky, niche; `/dev/sdX` opens read-only already |
| Keyboard macros (Ctrl-., Ctrl-0..8) | record/playback layer; power-user niche |
| PE table editing | viewer use-case; hex byte editing suffices (MZ kept, PE dropped) |

## Hiew keymap (`HiewKeys=yes`)

| Key | Hiew | BIEW mapping |
|---|---|---|
| F1 | context help | `activeMode->help()` |
| F2 | wrap (text) / write-block (hex) | `misc_action` text only |
| F3 | edit (hex/dis/bin) | `misc_action` non-text |
| F4 | mode select | `SelectMode()` |
| F9 | files | `FileUtils()` |
| Shift-F1 | — | `About()` |
| Shift-F9 | — | `Setup()` |

Known deviations: F6 stays ReRead (Hiew: refs/linefeed), F2 in hex is a
no-op (Hiew: write block — comes with block ops), Ctrl-F*/Alt-F* tables
are BIEW's own.
