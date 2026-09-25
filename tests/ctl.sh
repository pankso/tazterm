#!/bin/sh
#
# TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
# Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
#
# Engineer: Christophe Lincoln <pankso@slitaz.org>
# Coding assistants: OpenCode & Claude
#
# ctl.sh — End to end with the real binary: -e/-hold, tazterm ctl,
# redaction, errors, socket cleanup. Needs $DISPLAY, $BIN, and a
# private XDG_CONFIG_HOME / XDG_CACHE_HOME (set by run.sh).

fails=0
pass() { echo "PASS ctl ($1)"; }
fail() { echo "FAIL ctl ($1)"; fails=$((fails + 1)); }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }

sock() { echo "$XDG_CACHE_HOME/tazterm/$1.sock"; }
ctl() { TAZTERM_SOCKET=$(sock $pid) "$BIN" ctl "$@"; }

# Pane filled by a command: no typing needed.
"$BIN" -geometry 80x24 -T CtlTest -e sh -c \
	'seq 300; echo password=hunter2secret; sleep 30' >/dev/null 2>&1 &
pid=$!
sleep 2

check "socket 0600 in a 0700 dir" \
	'[ -S "$(sock $pid)" ] &&
	 ls -ld "$XDG_CACHE_HOME/tazterm" | grep -q "^drwx------" &&
	 ls -l "$(sock $pid)" | grep -q "^srwx------"'
check "ls lists the pane" 'ctl ls | grep -q "^1	cmd	active"'
check "read -n 3" '[ "$(ctl read -n 3 | head -2 | tr "\n" " ")" = "299 300 " ]'
check "secret redacted" 'ctl read -n 1 | grep -q "password=\[REDACTED\]"'
check "redaction noted" 'ctl read -n 1 | grep -q "1 secret(s) redacted"'
check "unknown pane fails" '! ctl read -p 99 2>/dev/null'
check "blocks need bash" 'ctl read -l 2>&1 | grep -q "bash panes only"'
check "guide without socket" \
	'env -u TAZTERM_SOCKET "$BIN" ctl guide | grep -q "read-only"'
check "discovery finds the only window" \
	'env -u TAZTERM_SOCKET "$BIN" ctl ls | grep -q "^1	"'


# JSON answers: valid, redaction counted (jq when installed).
if command -v jq >/dev/null; then
	check "ls --json" '[ "$(ctl ls -j | jq -r ".[0].role")" = cmd ]'
	check "read --json" \
		'[ "$(ctl read -j -n 1 | jq -r ".redacted")" = 1 ]'
fi
check "read --json masks" 'ctl read -j -n 1 | grep -q "\[REDACTED\]"'

# wait --idle: the pane is quiet (sleep 30).
check "wait --idle returns when quiet" \
	'ctl wait --idle -s 1 -t 5 | grep -q "^idle [0-9]*s"'

# events: notify reaches a listener, text and JSON.
ctl events >"$XDG_CACHE_HOME/ev" 2>&1 &
evpid=$!
ctl events -j >"$XDG_CACHE_HOME/evj" 2>&1 &
evjpid=$!
sleep 1
ctl notify "build done"
sleep 1
kill $evpid $evjpid
check "events: notify" 'grep -q "^notify 0 build done$" "$XDG_CACHE_HOME/ev"'
check "events --json: notify" \
	'grep -q "^{\"event\":\"notify\",\"pane\":0,\"text\":\"build done\"}$" "$XDG_CACHE_HOME/evj"'

kill $pid
sleep 1
check "socket removed on SIGTERM" '[ ! -e "$(sock $pid)" ]'

# wait --idle returns on a bell, events see it and the exit.
"$BIN" -hold -e sh -c 'sleep 2; printf "\a"; sleep 1; exit 4' \
	>/dev/null 2>&1 &
pid=$!
sleep 1
ctl events >"$XDG_CACHE_HOME/ev" 2>&1 &
evpid=$!
check "wait --idle returns on bell" \
	'ctl wait --idle -s 20 -t 10 | grep -q "^bell$"'
check "wait --idle on a held pane: exited" \
	'sleep 2; ctl wait --idle -t 5 | grep -q "^exited 4$"'
kill $evpid
check "events: bell and exit" \
	'grep -q "^bell 1$" "$XDG_CACHE_HOME/ev" &&
	 grep -q "^exit 1 4$" "$XDG_CACHE_HOME/ev"'
kill $pid
sleep 1

# --hold keeps the pane and reports the exit code.
"$BIN" -hold -e sh -c 'echo held; exit 3' >/dev/null 2>&1 &
pid=$!
sleep 2
check "hold keeps output" 'ctl read | grep -q "^held"'
check "hold reports exit 3" 'ctl ls | grep -q "exited 3"'
kill $pid
sleep 1

# Deep XDG_CACHE_HOME: no truncated socket, short /tmp fallback.
deep=$XDG_CACHE_HOME/a-rather-long-directory-name/another-long-one/and-more/still-more-to-pass-108
mkdir -p "$deep"
XDG_CACHE_HOME=$deep "$BIN" -e sleep 30 >/dev/null 2>&1 &
pid=$!
sleep 2
check "long path: socket in /tmp/tazterm-UID" \
	'[ -S /tmp/tazterm-$(id -u)/$pid.sock ] &&
	 XDG_CACHE_HOME=$deep "$BIN" ctl ls | grep -q "^1	"'
check "long path: nothing truncated" \
	'[ -z "$(find "$deep" -type s)" ]'
kill $pid
sleep 1

# Fresh config: shell=auto picks bash when installed.
if command -v bash >/dev/null; then
	"$BIN" >/dev/null 2>&1 &
	pid=$!
	sleep 2
	check "shell=auto gives bash" 'ctl ls | grep -q "	bash	"'
	check "default config says auto" \
		'grep -q "^shell=auto" "$XDG_CONFIG_HOME/tazterm/tazterm.conf"'
	kill $pid
	sleep 1
fi

# An agent started by hand (not an agent split) is still an agent.
if command -v bash >/dev/null; then
	cp "$(command -v bash)" "$XDG_CACHE_HOME/claude"
	"$BIN" -e "$XDG_CACHE_HOME/claude" -c 'sleep 30; true' \
		>/dev/null 2>&1 &
	pid=$!
	sleep 2
	check "agent found by its process" 'ctl ls | grep -q "^1	agent	"'
	kill $pid
	sleep 1
fi

# Help: the shortcuts in use, custom [keys] included, no display.
check "help without display" \
	'DISPLAY= LANG=C "$BIN" help | grep -q "Ctrl+Shift+E *Split side by side"'
mkdir -p "$XDG_CACHE_HOME/helpconf/tazterm"
printf '[keys]\nsplit_side=Super+Return\nfocus_left=\n' \
	>"$XDG_CACHE_HOME/helpconf/tazterm/tazterm.conf"
check "help shows custom keys" \
	'XDG_CONFIG_HOME=$XDG_CACHE_HOME/helpconf LANG=C "$BIN" help |
	 grep -q "Super+Return *Split side by side" &&
	 ! XDG_CONFIG_HOME=$XDG_CACHE_HOME/helpconf LANG=C "$BIN" help |
	 grep -q "pane on the left"'
check "--help lists shortcuts" \
	'DISPLAY= LANG=C "$BIN" --help | grep -q "F1 *Keyboard shortcuts"'

check "-v without display" 'DISPLAY= "$BIN" -v | grep -q "^tazterm "'

exit $fails
