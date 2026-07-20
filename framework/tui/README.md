# Terminal UI framework

`framework.tui` is a Python-free, host-C checkpoint for terminal interfaces in
Kofun programs. It provides display-width-aware progress, table, tree, and log
renderers plus a session wrapper that selects an interactive or append-only
output path.

```sh
framework/tui/build.sh examples/tui_dashboard.kofun build/tui-dashboard
build/tui-dashboard
```

The Kofun declarations in `api.kofun` are the stable scalar-handle boundary for
the current C ABI. The public C header also exposes aggregate render functions
for callers that need arbitrary row/node/log arrays. All renderers write into a
caller-owned buffer; they neither print nor allocate.

## Terminal behavior

`kofun_tui_begin` enables repainting only when stdout is a TTY, `TERM` is not
`dumb`, and none of `CI`, `NO_COLOR`, or the `no_tui` argument disables it.
Every disabled case uses newline-terminated, append-only output with no escape
sequences. Interactive color degrades in this order:

1. `COLORTERM=truecolor` or `24bit`: 24-bit RGB;
2. `TERM=*256color`: indexed 256-color;
3. a normal color terminal: ANSI 16-color;
4. `COLORS=0` or a mono terminal: no color.

`NO_COLOR` intentionally disables repainting as well as color, preserving the
existing archived-log contract. UTF-8 locale detection independently controls
Unicode spinners, bars, ellipses, and tree connectors.

The session registers a minimal `SIGWINCH` handler. The next component render
reads `TIOCGWINSZ` and recomputes its layout; `kofun_tui_set_size` is the
deterministic override for tests and embedded applications. Tables change from
columns to labeled records at narrow widths, logs drop sequence numbers below
32 columns, and every component clips at grapheme-cluster boundaries. RTL text
is wrapped in FSI/PDI isolates so a cell cannot reorder its separators.

## Unicode and bounds

The width engine decodes UTF-8 itself: CJK and emoji occupy two cells,
combining/default-ignorable scalars occupy zero, and emoji ZWJ, flag, keycap,
and variation-selector sequences are treated as one cluster. Invalid UTF-8 is
rendered as a one-cell replacement. Rendering is bounded to 16 table columns,
256 rows, depth 32, 4,096 input bytes per text, and a 512-by-256 viewport. A
short destination buffer is always NUL-terminated and reports `truncated`.

Run the deterministic contracts and live cost guard with:

```sh
make tui-framework
```

## Checkpoint limitations

- Automatic resize is applied on the next application render; this library
  does not own an event loop or call application code from a signal handler.
- Session objects are single-thread confined, and the process-wide SIGWINCH
  registration expects sessions to be opened/closed on that same thread.
- The current scalar Kofun ABI offers a two-column table convenience function
  and one-node/one-entry calls. The public C aggregate API has the full bounded
  collections until Kofun's C ABI can pass slices.
- Width follows modern terminal practice rather than locale-dependent East
  Asian Ambiguous width. Terminals configured to show ambiguous characters as
  double-width require an application-specific policy in a later revision.
