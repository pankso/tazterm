/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
/* tazterm-split.h — Pane container (recursive GtkPaned tree).
 *
 * Each leaf = a VteTerminal with its own shell. The split owns the state
 * (active pane) and the structure; the window hooks its signals through
 * callbacks (per-terminal setup, focus, empty).
 */
#ifndef TAZTERM_SPLIT_H
#define TAZTERM_SPLIT_H

#include <gtk/gtk.h>
#include <vte/vte.h>

#include "tazterm-config.h"

G_BEGIN_DECLS

typedef enum {
	TAZTERM_LEFT,
	TAZTERM_RIGHT,
	TAZTERM_UP,
	TAZTERM_DOWN
} TaztermDirection;

typedef struct {
	/* Called for every created terminal (initial + splits): the window
	 * connects key-press, right-click, title, child-exited there. */
	void (*term_setup)(VteTerminal *term, gpointer data);
	gpointer term_setup_data;
	/* Called when the active pane changes (title, search). */
	void (*focus_changed)(VteTerminal *term, gpointer data);
	gpointer focus_data;
	/* Called when no pane remains (close the window). */
	void (*empty)(gpointer data);
	gpointer empty_data;
	/* Click on the right part of a pane's status line ("✗ exit 2").
	 * Gets the same data as term_setup. */
	void (*status_clicked)(VteTerminal *term, gpointer data);
} TaztermSplitHooks;

/* New container with a first terminal: a shell, or command (argv,
 * NULL for a shell). */
GtkWidget *tazterm_split_new(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override,
    char **command, const TaztermSplitHooks *hooks);

/* Active pane's terminal (never NULL while a pane remains). */
VteTerminal *tazterm_split_active_term(GtkWidget *split);

/* Agent pane (agent split, or an agent running in the foreground) the
 * user was in last; the first one when none was visited. NULL: none. */
VteTerminal *tazterm_split_find_agent(GtkWidget *split);

/* Every pane's terminal, tree order (free with g_ptr_array_free). */
GPtrArray *tazterm_split_list(GtkWidget *split);

/* Pane active before the current one, or NULL (closed / none). */
VteTerminal *tazterm_split_previous_term(GtkWidget *split);

/* Orange outline on a background pane that wants the user; cleared
 * when the pane gets focus. No-op on the active pane. */
void tazterm_split_attention(GtkWidget *split, VteTerminal *term);

/* Pane count (debug / tests). */
int tazterm_split_count(GtkWidget *split);

/* Split the active pane: side by side / stacked. */
void tazterm_split_vertical(GtkWidget *split);
void tazterm_split_horizontal(GtkWidget *split);

/* Like vertical, but the new pane spawns command (agent) instead
 * of a shell. */
void tazterm_split_vertical_cmd(GtkWidget *split, char **command);

/* Close the pane holding term (remove from tree, collapse parent).
 * Safe when term already left the tree. Triggers empty() when empty. */
void tazterm_split_remove_term(GtkWidget *split, VteTerminal *term);

/* Close the active pane. */
void tazterm_split_close_current(GtkWidget *split);

/* Move focus to the neighbor pane in the given direction. */
void tazterm_split_focus_dir(GtkWidget *split, TaztermDirection dir);

/* Window-wide zoom: applies to every pane and is inherited by new
 * panes (unlike per-terminal scale). */
void tazterm_split_zoom_in(GtkWidget *split);
void tazterm_split_zoom_out(GtkWidget *split);
void tazterm_split_zoom_reset(GtkWidget *split);

G_END_DECLS

#endif /* TAZTERM_SPLIT_H */
