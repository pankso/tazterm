# Contributing to TazTerm

TazTerm follows the SliTaz way: keep it simple, keep it small, test it on a real system.

## Code

- C99, GTK3 and VTE only. A new dependency needs a very good reason.
- Style of the existing code: tabs, about 80 columns, BSD-like braces, `static` helpers, one module per concern (`src/tazterm-*.c`).
- Comments in English, explaining why rather than what.
- User-visible strings in English inside `_()`; translations live in `po/`.
- Anything handed to an agent goes through the redaction filter. Nothing may write into a pane on behalf of an agent without a human confirmation.

## Tests

Run `make check` before sending a change. It needs Xvfb and uses display `:7` (set `TAZTERM_TEST_DISPLAY` to change it). A bug fix comes with a test when it can be expressed as one: `tests/unit.c` for pure functions, `tests/gui.c` for VTE scenarios, `tests/ctl.sh` for the binary and the control socket.

`TAZTERM_DEBUG=1 tazterm` logs spawns, marks, status changes and ctl requests on stderr.

## Commits

One line, English, prefixed with `tazterm:`, saying what changes: `tazterm: confirm before closing a pane with a running program`.

## Translations

```sh
make -C po update-pot
msgmerge -U po/xx.po po/tazterm.pot
```

Add the language code to `po/LINGUAS`.
