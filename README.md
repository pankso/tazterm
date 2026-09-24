# TazTerm

A light GTK3/VTE terminal with split panes, made for working with AI coding agents. Written in C, one binary, about 24 MB of RAM for a window, no daemon, no cloud, no account.

TazTerm is the terminal of [SliTaz GNU/Linux](https://www.slitaz.org), a tiny distribution with 20 years of experience in doing more with less. It is developed and used every day on SliTaz itself, to build packages, ISOs and the tools of the distribution, with agents such as Claude Code, opencode and navette running in split panes next to the shells.

## Why

Coding agents run in a terminal, and the first thing they ask is "paste me the error". TazTerm lets them look for themselves, safely:

- **The agent reads your panes.** `tazterm ctl read` gives an agent the output of the pane you were just in, `tazterm ctl read -l` the exact last command with its exit code. No copy and paste.
- **Read-only by design.** An agent can look at your panes, never type into them. Terminal output comes from anywhere (logs, `curl`, a cloned README) and can carry prompt injections: writing to a shell stays a human action.
- **Secrets stay home.** Private keys, API tokens and `password=` values are masked as `[REDACTED]` before anything reaches an agent.
- **You know who needs you.** Each pane has a status line (working, waiting, exit code, running for 2m). A pane that rings the bell, or a long command that ends while you look elsewhere, gets an orange outline and the window an urgency hint.
- **Light.** C, GTK3 and VTE, nothing else. It runs on the old 32-bit machines SliTaz supports and leaves the RAM to local models on newer ones.

## Features

- Split panes in one window (side by side, stacked, keyboard navigation), window-wide zoom, fullscreen, search
- Per-pane status line: id, role (shell, agent, command), foreground process, directory, activity
- Command blocks for bash: every command with its output, exit code and duration; jump from prompt to prompt
- `tazterm ctl`: `ls`, `read`, `read -l`, `blocks`, `wait`, `wait --idle`, `events`, `notify`, `guide`, with JSON output (`-j`)
- Agent panes: detects claude, opencode and navette (also when started by hand in a shell), opens one in a split, sends a selection or the last failed command to the agent you used last (never submitted: you add your question). An agent that quits leaves its pane: Enter starts it again. `agent=` takes arguments (`claude --continue`)
- Click on a red `✗ exit N` in a status line: the failed command goes to the agent
- Safe paste: control characters stripped, bracketed paste handled by VTE, confirmation before a multi-line paste into a shell that would run every line
- Ctrl+click on URLs and on `file:line[:col]` (compilers, `grep -n`, tracebacks) to open your terminal editor at that line
- Confirmation before closing a pane or the window where a program still runs
- xterm-compatible command line: `-e`, `-T`, `-geometry`, `-hold`, `--class`
- `make check`: unit tests, GUI scenarios on Xvfb, end-to-end tests of the control socket

## tazterm ctl

Every pane exports `TERM_PROGRAM=tazterm`, `TAZTERM_PANE` and `TAZTERM_SOCKET`. Any agent able to run a shell command can use it:

```sh
tazterm ctl ls              # panes: id, role, state, activity, process, cwd, title
tazterm ctl read            # last 200 lines of the pane you came from
tazterm ctl read -l         # its last command: command, output, [exit N, 3s]
tazterm ctl blocks          # recent commands with exit codes and durations
tazterm ctl wait            # wait for the next command there to end, exit with its status
tazterm ctl wait --idle -p 3  # wait for pane 3 to go quiet, ring or exit
tazterm ctl events          # live stream: block, bell, notify, open, exit, close
tazterm ctl notify "done"   # outline the agent's pane, set the urgency hint
tazterm ctl guide           # how an agent should use all this (markdown)
```

`wait` lets an agent say "run `make` in your pane" and get the result without running anything itself. `wait --idle` and `events` let an agent follow other agents working in the next panes: an orchestrator knows when a turn ends without polling. Add `-j` to `ls`, `read`, `blocks`, `wait` or `events` for JSON, secrets already masked.

To teach your agent, add one line to your `CLAUDE.md` or `AGENTS.md`:

```
If TERM_PROGRAM=tazterm, run `tazterm ctl guide` once: it explains how to read the user's panes instead of asking them to paste output.
```

## Security model

- One Unix socket per window, in `$XDG_RUNTIME_DIR/tazterm/` (else `~/.cache/tazterm/`, else `/tmp/tazterm-UID/` when the path would be too long), directory 0700 checked for owner and symlinks, peer uid verified, removed on exit.
- Read-only protocol, one request line, async I/O with timeouts: a stuck client never freezes the window.
- Command block marks carry a random per-pane token: `cat` of a crafted file cannot forge blocks or exit codes.
- Paste: C0 and C1 controls stripped, invalid UTF-8 repaired, 256 KiB cap; VTE adds bracketed paste only when the application asked for it.
- Nothing written to disk: captures go to the clipboard or to the agent, never to `/tmp`.
- OSC 7 only for local paths, `/proc` first; OSC 8 hyperlinks and window resize requests are not honored.

## Keys

| Keys | Action |
|------|--------|
| Ctrl+Shift+C / V | Copy / paste |
| Ctrl+Shift+E / O | Split side by side / stacked |
| Ctrl+Shift+W / Q | Close pane / window (asks when a program runs) |
| Alt+Arrows | Move to the neighbor pane |
| Ctrl+Shift+Up / Down | Previous / next prompt (bash) |
| Ctrl+Shift+F | Find |
| Ctrl+Shift+A | Open an agent split |
| Ctrl+Shift+T | Send the selection to the agent |
| Ctrl+Shift+X | Send the last command (or last lines) to the agent |
| Ctrl+Shift+S | Copy the scrollback |
| Ctrl+click | Open a URL or `file:line` |
| Ctrl+Plus / Minus / 0, F11 | Zoom, fullscreen |

## Configuration

`~/.config/tazterm/tazterm.conf` is created on first run with every key and a comment:

```ini
[terminal]
font=Monospace 12
# auto: bash when installed (command blocks), else /bin/sh
shell=auto
scrollback_lines=10000
status_bar=true
confirm_close=true
# alert when a command this long (seconds) ends out of sight, 0 = never
notify_after=30
#editor=nano

[ai]
# auto | opencode | claude | navette, or with arguments: claude --continue
agent=auto
explain_lines=200
capture_lines=2000
redact=true
```

Command blocks need bash: busybox ash has no prompt hook. With `shell=auto`, tazterm starts bash with its own `--rcfile`, which sources your `~/.bashrc` first.

## Build

Requirements: GTK 3.22 or newer, VTE 0.56 or newer (`vte-2.91`), pkg-config, gettext.

```sh
make                 # binary in src/tazterm
make test            # unit tests only, no display (run on every cook)
make check           # all tests, needs Xvfb (uses display :7 unless TAZTERM_TEST_DISPLAY)
sudo make install    # PREFIX=/usr by default
```

On SliTaz: `sudo spk-add gtk+3-dev vte291-dev pkg-config gettext-tools`, or use the package from the wok.

TazTerm is developed and tested on SliTaz (GTK 3.22, VTE 0.56). It should build on any distribution that ships `vte-2.91` (Debian and Ubuntu: `libvte-2.91-dev libgtk-3-dev`, Arch: `vte3`); reports and fixes for other systems are welcome.

## License

BSD, see [COPYING](COPYING).

Engineer: Christophe Lincoln <pankso@slitaz.org>. Coding assistants: OpenCode & Claude.
