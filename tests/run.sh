#!/bin/sh
#
# TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
# Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
#
# Engineer: Christophe Lincoln <pankso@slitaz.org>
# Coding assistants: OpenCode & Claude
#
# run.sh — `make check`: unit tests, GUI scenarios on Xvfb, end to end.
# Display: $TAZTERM_TEST_DISPLAY (default :7). An existing server there
# is reused, else Xvfb is started and killed at the end. Config and
# sockets go to a throwaway dir: the user's setup is never touched.
cd "$(dirname "$0")"
BIN=$(cd ../src && pwd)/tazterm
export BIN

disp=${TAZTERM_TEST_DISPLAY:-:7}
xpid=
if [ ! -e "/tmp/.X11-unix/X${disp#:}" ]; then
	Xvfb "$disp" -screen 0 1280x800x24 >/dev/null 2>&1 &
	xpid=$!
	sleep 1
fi
tmp=$(mktemp -d)
export DISPLAY=$disp NO_AT_BRIDGE=1 XDG_CONFIG_HOME=$tmp/config \
	XDG_CACHE_HOME=$tmp/cache PS1='$ '
unset ENV PROMPT_COMMAND TAZTERM_SOCKET TAZTERM_PANE

fails=0
./unit || fails=$((fails + $?))
for t in paste-ash-multiline:/bin/sh paste-ash-accept:/bin/sh \
	paste-single:/bin/sh paste-bash:/bin/bash paste-inject:/bin/sh \
	paste-dead-pane:/bin/sh tail:- blocks:-; do
	./gui ${t%%:*} ${t#*:} 2>/dev/null || fails=$((fails + 1))
done
sh ./ctl.sh || fails=$((fails + $?))

[ -n "$xpid" ] && kill $xpid
rm -rf "$tmp"
if [ $fails -eq 0 ]; then
	echo "All tests passed."
else
	echo "$fails test(s) failed."
fi
exit $fails
