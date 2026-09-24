/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
/* tazterm-config.h — ~/.config/tazterm/tazterm.conf configuration. */
#ifndef TAZTERM_CONFIG_H
#define TAZTERM_CONFIG_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct {
	char *font_desc;   /* e.g. "Monospace 12" */
	char *shell;       /* resolved: "auto" -> bash if installed, else /bin/sh */
	char *workdir;     /* NULL = $HOME */
	char *editor;      /* Ctrl+click file:line, NULL = $VISUAL/$EDITOR */
	long scrollback;   /* lines, default 10000 */
	GdkRGBA foreground;
	GdkRGBA background;
	gboolean fg_set;
	gboolean bg_set;
	GdkRGBA palette[16];   /* theme colors 0-15 */
	gboolean palette_set;  /* FALSE: VTE's own palette */
	GdkRGBA cursor;
	gboolean cursor_set;
	int cursor_shape;      /* VteCursorShape, -1 = VTE default */
	int bold_is_bright;    /* 0 / 1, -1 = VTE default */
	gboolean status_bar;   /* per-pane status line, default TRUE */
	gboolean confirm_close; /* ask before killing a running program */
	int notify_after;      /* alert when a command this long (s) ends
	                        * out of sight, 0 = never */
	char *ai_agent;    /* "auto" or opencode|claude|navette */
	int ai_explain_lines;  /* default 200 */
	int ai_capture_lines;  /* default 2000 */
	gboolean ai_redact;    /* mask secrets sent to agents, default TRUE */
} TaztermConfig;

/* Load the config (file created with defaults when missing).
 * Never returns NULL (defaults on failure). */
TaztermConfig *tazterm_config_load(void);

void tazterm_config_free(TaztermConfig *cfg);

/* Config file path (free with g_free). */
char *tazterm_config_path(void);

G_END_DECLS

#endif /* TAZTERM_CONFIG_H */
