/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
/* tazterm-window.h — Main window + split + search + menu + zoom. */
#ifndef TAZTERM_WINDOW_H
#define TAZTERM_WINDOW_H

#include <gtk/gtk.h>

#include "tazterm-config.h"

G_BEGIN_DECLS

/* Command-line options (xterm-compatible where it matters). */
typedef struct {
	const char *shell;     /* -s */
	const char *workdir;   /* -d */
	char **command;        /* -e ARGV..., NULL: shell */
	const char *title;     /* -T: base title, default "TazTerm" */
	const char *geometry;  /* --geometry COLSxROWS[+X+Y] */
	gboolean hold;         /* --hold: keep the -e pane after exit */
} TaztermWinOpts;

GtkWidget *tazterm_window_new(TaztermConfig *cfg,
    const TaztermWinOpts *opts);

G_END_DECLS

#endif /* TAZTERM_WINDOW_H */
