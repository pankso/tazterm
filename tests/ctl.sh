#!/bin/sh
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

kill $pid
sleep 1
check "socket removed on SIGTERM" '[ ! -e "$(sock $pid)" ]'

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

check "-v without display" 'DISPLAY= "$BIN" -v | grep -q "^tazterm "'

exit $fails
