/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
/* tazterm-keys.h — Shortcuts, configurable in [keys].
 *
 * action=Ctrl+Shift+E       one binding
 * action=Alt+Left Alt+KP_Left  several, space separated
 * action=                   disabled: the key goes to the application
 * Modifiers: Ctrl, Shift, Alt, Super. Key names are GDK's (letters,
 * F11, Left, plus, KP_Add...), case does not matter for letters.
 */
#ifndef TAZTERM_KEYS_H
#define TAZTERM_KEYS_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum {
	TAZTERM_KEY_COPY,
	TAZTERM_KEY_PASTE,
	TAZTERM_KEY_QUIT,
	TAZTERM_KEY_FIND,
	TAZTERM_KEY_SPLIT_SIDE,
	TAZTERM_KEY_SPLIT_STACKED,
	TAZTERM_KEY_CLOSE,
	TAZTERM_KEY_ZOOM_PANE,
	TAZTERM_KEY_EQUALIZE,
	TAZTERM_KEY_FOCUS_LEFT,
	TAZTERM_KEY_FOCUS_RIGHT,
	TAZTERM_KEY_FOCUS_UP,
	TAZTERM_KEY_FOCUS_DOWN,
	TAZTERM_KEY_RESIZE_LEFT,
	TAZTERM_KEY_RESIZE_RIGHT,
	TAZTERM_KEY_RESIZE_UP,
	TAZTERM_KEY_RESIZE_DOWN,
	TAZTERM_KEY_PROMPT_PREV,
	TAZTERM_KEY_PROMPT_NEXT,
	TAZTERM_KEY_AGENT,
	TAZTERM_KEY_SEND,
	TAZTERM_KEY_EXPLAIN,
	TAZTERM_KEY_SCROLLBACK,
	TAZTERM_KEY_FONT_BIGGER,
	TAZTERM_KEY_FONT_SMALLER,
	TAZTERM_KEY_FONT_RESET,
	TAZTERM_KEY_FULLSCREEN,
	TAZTERM_KEY_N
} TaztermKeyAction;

typedef struct TaztermKeys TaztermKeys;

/* Defaults, overridden by the [keys] group of kf (may be NULL). Bad
 * bindings are warned about and ignored. */
TaztermKeys *tazterm_keys_new(GKeyFile *kf);
void tazterm_keys_free(TaztermKeys *keys);

/* Action bound to a key press, -1 when none. */
int tazterm_keys_lookup(const TaztermKeys *keys, guint keyval,
    GdkModifierType state);

/* Commented [keys] section with every action and its default, for the
 * config file written on first run. g_free. */
char *tazterm_keys_default_conf(void);

G_END_DECLS

#endif /* TAZTERM_KEYS_H */
