# tools/ — headless GUI test helpers (X11/XTest + one ncurses probe)

Small X11 helpers used to drive and inspect tazterm on a virtual
display (Xvfb) during development; `make check` does not need them.
Build: `make` (needs `xorg-libXtst-dev` for XTest tools,
`ncurses-dev` for sizeloop, `gdk-pixbuf-dev` comes via gtk+3-dev).

| Tool      | Usage                                          |
|-----------|------------------------------------------------|
| xlist2    | `xlist2` — EWMH client list + geometry + state |
| xgeom2    | `xgeom2 SUBSTR` — frame geometry by name match |
| tazraise  | `tazraise SUBSTR` — raise+focus via EWMH       |
| tazactive | `tazactive` — print _NET_ACTIVE_WINDOW + name  |
| clicker2  | `clicker2 X Y [ms] [button]` — XTest click     |
| tazkey    | `tazkey combo MODS KEY` / `key KEY` / `type TEXT` |
| svg2png   | `svg2png IN.svg OUT.png SIZE` — icon raster    |
| sizeloop  | ncurses redraw probe (getmaxyx, never getch)   |

Rules: verify focus with `tazactive` before `tazkey`/`clicker2`
(keys go to the focused window, not to tazterm); escape `$` in type
text (`\$`); never `pkill -f` with the pattern on its own line.
