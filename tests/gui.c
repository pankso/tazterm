/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
/* gui.c — Real VTE on a (virtual) display, one scenario per run:
 *   gui MODE SHELL
 * Links the real src/ objects. Prints PASS/FAIL, exit status 0 = pass.
 * Handles `--osc7` itself: panes call $TAZTERM_BIN, i.e. us. */
#include <stdio.h>
#include <string.h>

#include "tazterm-blocks.h"
#include "tazterm-config.h"
#include "tazterm-term.h"

static TaztermConfig *cfg;
static VteTerminal *term;
static const char *mode;
static int step;
static int result = 1;

static int
dialogs(void)
{
	GList *l, *all = gtk_window_list_toplevels();
	int n = 0;

	for (l = all; l; l = l->next)
		if (GTK_IS_MESSAGE_DIALOG(l->data) &&
		    gtk_widget_get_visible(l->data))
			n++;
	g_list_free(all);
	return n;
}

static void
dialog_answer(int response)
{
	GList *l, *all = gtk_window_list_toplevels();

	for (l = all; l; l = l->next)
		if (GTK_IS_MESSAGE_DIALOG(l->data))
			gtk_dialog_response(GTK_DIALOG(l->data), response);
	g_list_free(all);
}

/* A whole screen line equal to s (command output, not the echo). */
static gboolean
has_line(const char *s)
{
	char *t = tazterm_term_get_visible_text(term);
	char **ls = g_strsplit(t, "\n", -1);
	gboolean found = FALSE;
	int i;

	for (i = 0; ls[i]; i++)
		if (!strcmp(g_strstrip(ls[i]), s))
			found = TRUE;
	g_strfreev(ls);
	g_free(t);
	return found;
}

static void
clip(const char *s)
{
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), s,
	    -1);
}

static void
finish(gboolean ok, const char *what)
{
	printf("%s %s (%s)\n", ok ? "PASS" : "FAIL", mode, what);
	result = ok ? 0 : 1;
	gtk_main_quit();
}

static void
mark(const char *token, int n, int ec)
{
	char *s = g_strdup_printf("\033]6;file://localhost/tazterm/mark/%s/"
	    "%d/%d\007", token, n, ec);

	vte_terminal_feed(term, s, -1);
	g_free(s);
}

static gboolean
tick(gpointer data)
{
	(void) data;
	step++;
	if (!strcmp(mode, "paste-ash-multiline")) {
		/* busybox ash runs each pasted line: must ask first. */
		if (step == 1) {
			clip("echo ONE\necho TWO\n");
			tazterm_term_paste_clipboard(term);
		} else if (step == 2) {
			int d = dialogs();

			dialog_answer(GTK_RESPONSE_CANCEL);
			if (d != 1)
				finish(FALSE, "no confirmation dialog");
		} else if (step == 3) {
			finish(!has_line("ONE"), "cancel runs nothing");
		}
	} else if (!strcmp(mode, "paste-ash-accept")) {
		if (step == 1) {
			clip("echo ONE\necho TWO");
			tazterm_term_paste_clipboard(term);
		} else if (step == 2) {
			dialog_answer(GTK_RESPONSE_ACCEPT);
		} else if (step == 3) {
			/* Text intact (the old forced bracketed paste
			 * garbled it into "-ONE: not found"). */
			finish(has_line("ONE"), "accepted paste intact");
		}
	} else if (!strcmp(mode, "paste-single")) {
		if (step == 1) {
			clip("echo SINGLE");
			tazterm_term_paste_clipboard(term);
		} else if (step == 2) {
			finish(dialogs() == 0 && !has_line("SINGLE"),
			    "no dialog, not executed");
		}
	} else if (!strcmp(mode, "paste-bash")) {
		/* bash holds a bracketed multi-line paste: no dialog. */
		if (step == 1) {
			clip("echo ONE\necho TWO\n");
			tazterm_term_paste_clipboard(term);
		} else if (step == 2) {
			finish(dialogs() == 0 && !has_line("ONE"),
			    "no dialog, not executed");
		}
	} else if (!strcmp(mode, "paste-inject")) {
		if (step == 1) {
			clip("echo SAFE\033[201~\033[31m; echo \302\233PWN");
			tazterm_term_paste_clipboard(term);
		} else if (step == 2) {
			char *c = gtk_clipboard_wait_for_text(
			    gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));

			finish(c && !strchr(c, '\033') && !strstr(c, "\302\233"),
			    "ESC and C1 stripped");
			g_free(c);
		}
	} else if (!strcmp(mode, "paste-dead-pane")) {
		/* Pane dies while the clipboard answers / dialog is up. */
		if (step == 1) {
			clip("echo X\necho Y");
			tazterm_term_paste_clipboard(term);
			gtk_widget_destroy(GTK_WIDGET(term));
			term = NULL;
		} else if (step == 2) {
			dialog_answer(GTK_RESPONSE_ACCEPT);
		} else if (step == 3) {
			finish(TRUE, "no crash");
		}
	} else if (!strcmp(mode, "tail")) {
		if (step == 1) {
			GString *g = g_string_new(NULL);
			int i;

			for (i = 1; i <= 500; i++)
				g_string_append_printf(g, "row%d\r\n", i);
			vte_terminal_feed(term, g->str, g->len);
			g_string_free(g, TRUE);
		} else if (step == 2) {
			char *t = tazterm_term_get_text_tail(term, 200);

			finish(g_str_has_prefix(t, "row301\n") &&
			    g_str_has_suffix(t, "row500"),
			    "200 lines from the scrollback");
			g_free(t);
		}
	} else if (!strcmp(mode, "blocks")) {
		/* Prompt marks fed directly, one per main loop turn (VTE
		 * merges marks of one chunk). */
		if (step == 1) {
			tazterm_blocks_attach(term, "t0k");
			vte_terminal_feed(term, "$ ", -1);
			mark("t0k", 1, 0);
		} else if (step == 2) {
			vte_terminal_feed(term, "make\r\nerror: boom\r\n$ ", -1);
			mark("t0k", 2, 2);
		} else if (step == 3) {
			/* Forged: wrong token, must be ignored. */
			vte_terminal_feed(term, "cat evil\r\n", -1);
			mark("bad", 3, 0);
		} else if (step == 4) {
			TaztermBlock *b = tazterm_blocks_last(term);

			finish(b && !strcmp(b->command, "make") &&
			    !strcmp(b->output, "error: boom") && b->exit == 2,
			    "command, output, exit; forged mark ignored");
			tazterm_block_free(b);
		}
	}
	return G_SOURCE_CONTINUE;
}

int
main(int argc, char **argv)
{
	GtkWidget *win;
	char *cmd[] = { "sleep", "60", NULL };

	if (argc >= 2 && !strcmp(argv[1], "--osc7"))
		return tazterm_term_osc7_emit() ? 0 : 1;
	if (argc < 3) {
		fprintf(stderr, "usage: gui MODE SHELL|-\n");
		return 2;
	}
	gtk_init(&argc, &argv);
	mode = argv[1];
	cfg = tazterm_config_load();
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	/* "-": no shell, a quiet command pane to feed text into. */
	term = strcmp(argv[2], "-") ?
	    tazterm_term_new(cfg, argv[2], "/tmp") :
	    tazterm_term_new_cmd(cfg, NULL, "/tmp", cmd);
	gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(term));
	gtk_widget_show_all(win);
	g_timeout_add(900, tick, NULL);
	/* Hard stop: a hung scenario fails instead of blocking. */
	g_timeout_add_seconds(10, (GSourceFunc) gtk_main_quit, NULL);
	gtk_main();
	return result;
}
