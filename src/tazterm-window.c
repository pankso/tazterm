/* tazterm-window.c — GtkWindow 900x600 + TaztermSplit + search.
 *
 * Shortcuts:
 *   Ctrl+Shift+C / V / Q : copy / paste / quit all
 *   Ctrl+Shift+F          : show/hide search (toggle; Enter = next,
 *                           Shift+Enter = previous, Esc closes, also
 *                           from the terminal)
 *   Ctrl+Plus / Minus / 0 : zoom in / out / reset
 *   Ctrl+Shift+E          : split side by side (horizontal paned)
 *   Ctrl+Shift+O          : split stacked (vertical paned)
 *   Ctrl+Shift+W          : close current pane
 *   Ctrl+Shift+A          : open an agent split (active pane)
 *   Ctrl+Shift+T          : paste selection into the agent pane
 *   Ctrl+Shift+S          : copy scrollback (clipboard)
 *   Ctrl+Shift+X          : explain prompt (agent pane + clipboard)
 *   Alt+Arrows            : focus neighbor pane
 * Every terminal action (copy, search, zoom) targets the active pane.
 * The last closed pane quits (via the split's empty hook).
 */
#include "tazterm-window.h"
#include "tazterm-ai.h"
#include "tazterm-blocks.h"
#include "tazterm-ctl.h"
#include "tazterm-split.h"
#include "tazterm-term.h"

#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

typedef struct {
	GtkWidget *win;
	GtkWidget *split;
	TaztermConfig *cfg; /* not owned (main) */
	GtkWidget *search_bar;
	GtkWidget *search_entry;
	char **agents;      /* detected on PATH (g_strfreev) */
	char *agent;        /* default (g_free, NULL if none) */
	char *title;        /* base title (-T), default "TazTerm" */
	guint status_timer; /* 1 s status bar refresh */
	GtkWidget *close_dialog; /* pending close confirmation */
	gboolean fullscreen;
} TaztermWin;

static void
win_free(gpointer data)
{
	TaztermWin *tw = data;

	g_strfreev(tw->agents);
	g_free(tw->agent);
	g_free(tw->title);
	g_free(tw);
}

#define TW(x) ((TaztermWin *) (x))

static void status_update(VteTerminal *term);

/* Debug aid: SIGUSR1 dumps the active pane's text (headless rendering
 * checks). Registered only when TAZTERM_DEBUG is set. */
static TaztermWin *debug_win = NULL;

static gboolean
on_sigusr1(gpointer data)
{
	TaztermWin *tw = debug_win;
	VteTerminal *term;
	char *text;
	char *path;

	(void) data;
	if (!tw)
		return TRUE;
	term = tazterm_split_active_term(tw->split);
	if (!term)
		return TRUE;
	text = tazterm_term_get_visible_text(term);
	path = g_strdup_printf("/tmp/tazterm-dump-%d.txt", (int) getpid());
	if (!tazterm_write_private(path, text ? text : "", -1))
		g_printerr("tazterm: dump failed\n");
	else
		g_printerr("tazterm: dump -> %s (%lu bytes)\n", path,
		    (unsigned long) (text ? strlen(text) : 0));
	g_free(text);
	g_free(path);
	return TRUE;
}

static gboolean
on_quit_signal(gpointer data)
{
	gtk_widget_destroy(GTK_WIDGET(data));
	return G_SOURCE_REMOVE;
}

/* --- title -------------------------------------------------------------- */

static void
title_update(TaztermWin *tw, VteTerminal *term)
{
	const char *title;
	char *full;

	title = term ? vte_terminal_get_window_title(term) : NULL;
	if (title && *title) {
		/* Pty-controlled string: cap at 256 chars so a runaway
		 * program cannot balloon the title (char count keeps
		 * UTF-8 valid, unlike a byte cut). */
		char *short_title = g_utf8_substring(title, 0, 256);

		full = g_strdup_printf("%s - %s", short_title, tw->title);
		g_free(short_title);
		gtk_window_set_title(GTK_WINDOW(tw->win), full);
		g_free(full);
	} else {
		gtk_window_set_title(GTK_WINDOW(tw->win), tw->title);
	}
}

/* --- search ------------------------------------------------------------- */

static void search_hide(TaztermWin *tw);

static gboolean
search_is_shown(TaztermWin *tw)
{
	return gtk_search_bar_get_search_mode(
	    GTK_SEARCH_BAR(tw->search_bar));
}

static void
search_show(TaztermWin *tw)
{
	/* Ctrl+Shift+F toggles: close when already open. */
	if (search_is_shown(tw)) {
		search_hide(tw);
		return;
	}
	gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(tw->search_bar), TRUE);
	gtk_widget_grab_focus(tw->search_entry);
	if (tazterm_debug())
		g_printerr("tazterm: search show\n");
}

static void
search_hide(TaztermWin *tw)
{
	gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(tw->search_bar), FALSE);
	gtk_widget_grab_focus(
	    GTK_WIDGET(tazterm_split_active_term(tw->split)));
	if (tazterm_debug())
		g_printerr("tazterm: search hide\n");
}

static void
on_search_changed(GtkSearchEntry *entry, gpointer data)
{
	TaztermWin *tw = TW(data);
	VteTerminal *term;
	const char *text;

	term = tazterm_split_active_term(tw->split);
	if (!term)
		return;
	text = gtk_entry_get_text(GTK_ENTRY(entry));
	if (text && *text)
		tazterm_term_search(term, text);
}

/* Enter = next, Shift+Enter = previous, Esc = close. */
static gboolean
on_search_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	TaztermWin *tw = TW(data);
	VteTerminal *term;

	(void) widget;

	term = tazterm_split_active_term(tw->split);
	if (event->keyval == GDK_KEY_Escape) {
		search_hide(tw);
		return TRUE;
	}
	if ((event->keyval == GDK_KEY_Return ||
	    event->keyval == GDK_KEY_KP_Enter) && term) {
		if (event->state & GDK_SHIFT_MASK)
			tazterm_term_search_prev(term);
		else
			tazterm_term_search_next(term);
		return TRUE;
	}
	return FALSE;
}

/* --- context menu ------------------------------------------------------ */

static void
on_copy(GtkMenuItem *item, gpointer data)
{
	(void) item;
	vte_terminal_copy_clipboard_format(VTE_TERMINAL(data), VTE_FORMAT_TEXT);
}

static void
on_paste(GtkMenuItem *item, gpointer data)
{
	(void) item;
	/* Sanitized, bracketed when the app asks, confirmed before a
	 * multi-line paste into a raw shell prompt (see term.c). */
	tazterm_term_paste_clipboard(VTE_TERMINAL(data));
}

static void
on_select_all(GtkMenuItem *item, gpointer data)
{
	(void) item;
	vte_terminal_select_all(VTE_TERMINAL(data));
}

static void
on_search_menu(GtkMenuItem *item, gpointer data)
{
	(void) item;
	search_show(TW(data));
}

static void
on_zoom_in(GtkMenuItem *item, gpointer data)
{
	(void) item;
	tazterm_split_zoom_in(TW(data)->split);
}

static void
on_zoom_out(GtkMenuItem *item, gpointer data)
{
	(void) item;
	tazterm_split_zoom_out(TW(data)->split);
}

static void
on_zoom_reset(GtkMenuItem *item, gpointer data)
{
	(void) item;
	tazterm_split_zoom_reset(TW(data)->split);
}

static void
on_split_v(GtkMenuItem *item, gpointer data)
{
	TaztermWin *tw = TW(data);

	(void) item;
	tazterm_split_vertical(tw->split);
}

static void
on_split_h(GtkMenuItem *item, gpointer data)
{
	TaztermWin *tw = TW(data);

	(void) item;
	tazterm_split_horizontal(tw->split);
}

/* --- close confirmation ------------------------------------------------ */

/* Pending close: term is a weak pointer (NULL = close the window). */
typedef struct {
	TaztermWin *tw;
	VteTerminal *term;
	gboolean window;
} CloseReq;

static void
on_close_dialog_destroy(GtkWidget *dialog, gpointer data)
{
	CloseReq *req = data;

	(void) dialog;
	req->tw->close_dialog = NULL;
	if (req->term)
		g_object_remove_weak_pointer(G_OBJECT(req->term),
		    (gpointer *) &req->term);
	g_free(req);
}

static void
on_close_dialog_response(GtkDialog *dialog, int response, gpointer data)
{
	CloseReq *req = data;
	TaztermWin *tw = req->tw;
	gboolean window = req->window;
	VteTerminal *term = req->term;

	gtk_widget_destroy(GTK_WIDGET(dialog)); /* frees req */
	if (response != GTK_RESPONSE_ACCEPT)
		return;
	if (window)
		gtk_widget_destroy(tw->win);
	else if (term)
		tazterm_split_remove_term(tw->split, term);
}

/* Close a pane (term) or the window (term == NULL), asking first when
 * a program would be killed: an agent at work, an unsaved vim. */
static void
close_confirm(TaztermWin *tw, VteTerminal *term)
{
	GString *busy;
	GtkWidget *dialog;
	CloseReq *req;
	char *what;
	GPtrArray *arr;
	guint i;

	if (tw->close_dialog) {
		gtk_window_present(GTK_WINDOW(tw->close_dialog));
		return;
	}
	busy = g_string_new(NULL);
	arr = term ? NULL : tazterm_split_list(tw->split);
	for (i = 0; term ? i < 1 : i < arr->len; i++) {
		VteTerminal *t = term ? term : g_ptr_array_index(arr, i);

		if (tw->cfg->confirm_close && tazterm_term_is_busy(t, &what)) {
			g_string_append_printf(busy, "%s%s", busy->len ? ", " :
			    "", what ? what : "?");
			g_free(what);
		}
	}
	if (arr)
		g_ptr_array_free(arr, TRUE);
	if (!busy->len) {
		g_string_free(busy, TRUE);
		if (term)
			tazterm_split_remove_term(tw->split, term);
		else
			gtk_widget_destroy(tw->win);
		return;
	}

	dialog = gtk_message_dialog_new(GTK_WINDOW(tw->win),
	    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
	    GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s",
	    term ? _("Close this pane?") : _("Close TazTerm?"));
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
	    _("Still running: %s. It will be stopped."), busy->str);
	g_string_free(busy, TRUE);
	gtk_dialog_add_buttons(GTK_DIALOG(dialog),
	    _("Cancel"), GTK_RESPONSE_CANCEL,
	    _("Close"), GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog),
	    GTK_RESPONSE_CANCEL);
	req = g_new0(CloseReq, 1);
	req->tw = tw;
	req->window = term == NULL;
	req->term = term;
	if (term)
		g_object_add_weak_pointer(G_OBJECT(term),
		    (gpointer *) &req->term);
	g_signal_connect(dialog, "response",
	    G_CALLBACK(on_close_dialog_response), req);
	g_signal_connect(dialog, "destroy",
	    G_CALLBACK(on_close_dialog_destroy), req);
	tw->close_dialog = dialog;
	gtk_widget_show(dialog);
}

/* Window manager close button / Alt+F4. */
static gboolean
on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	(void) widget;
	(void) event;
	close_confirm(TW(data), NULL);
	return TRUE; /* close_confirm destroys the window when it may */
}

static void
on_close_pane(GtkMenuItem *item, gpointer data)
{
	TaztermWin *tw = TW(data);

	(void) item;
	close_confirm(tw, tazterm_split_active_term(tw->split));
}

static void
fullscreen_set(TaztermWin *tw, gboolean full)
{
	tw->fullscreen = full;
	if (full)
		gtk_window_fullscreen(GTK_WINDOW(tw->win));
	else
		gtk_window_unfullscreen(GTK_WINDOW(tw->win));
	if (tazterm_debug())
		g_printerr("tazterm: fullscreen %s\n",
		    full ? "on" : "off");
}

static void
on_fullscreen(GtkMenuItem *item, gpointer data)
{
	TaztermWin *tw = TW(data);

	(void) item;
	fullscreen_set(tw, !tw->fullscreen);
}

/* Also track external changes (e.g. Openbox, EWMH). */
static gboolean
on_window_state(GtkWidget *widget, GdkEventWindowState *event,
    gpointer data)
{
	TaztermWin *tw = TW(data);

	(void) widget;
	if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)
		tw->fullscreen =
		    (event->new_window_state &
		    GDK_WINDOW_STATE_FULLSCREEN) != 0;
	return FALSE;
}

/* --- AI actions ----------------------------------------------------------- */

/* Vertical split whose new pane spawns the agent directly (no feed_child:
 * lost race with the shell's async spawn). agent == NULL: window default. */
static void
ai_agent_split(TaztermWin *tw, const char *agent)
{
	const char *cmd;
	char **argv;

	if (!agent)
		agent = tw->agent;
	if (!agent) {
		if (tazterm_debug())
			g_printerr("tazterm: agent split without agent\n");
		return;
	}
	cmd = tazterm_ai_launch_cmd(agent);
	argv = tazterm_command_argv(tw->cfg, cmd);
	if (!argv) {
		g_warning("tazterm: cannot parse agent command '%s'", cmd);
		return;
	}
	tazterm_split_vertical_cmd(tw->split, argv);
	g_strfreev(argv);
	/* The new pane becomes active: tag it so "send to agent"
	 * can find it later. */
	{
		VteTerminal *pane = tazterm_split_active_term(tw->split);

		if (pane)
			g_object_set_data(G_OBJECT(pane), "tazterm-agent",
			    GINT_TO_POINTER(TRUE));
	}
	if (tazterm_debug())
		g_printerr("tazterm: agent split: %s (agent=%s)\n", cmd,
		    agent);
}

/* Clipboard only: scrollback may hold secrets, never written to disk. */
static void
ai_copy_scrollback(TaztermWin *tw)
{
	VteTerminal *term;
	char *text;

	term = tazterm_split_active_term(tw->split);
	if (!term)
		return;
	text = tazterm_ai_last_lines(term, tw->cfg->ai_capture_lines);
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
	    text, -1);
	if (tazterm_debug())
		g_printerr("tazterm: scrollback copy (%lu bytes)\n",
		    (unsigned long) strlen(text));
	g_free(text);
}

/* Prompt to the clipboard, and pasted into the agent pane when there
 * is one (no Enter: the user reviews, adds a question, submits). */
static void
ai_explain(TaztermWin *tw)
{
	VteTerminal *term;
	VteTerminal *agent;
	char *prompt;

	term = tazterm_split_active_term(tw->split);
	if (!term)
		return;
	prompt = tazterm_ai_explain_prompt(term, tw->cfg->ai_explain_lines,
	    tw->cfg->ai_redact);
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
	    prompt, -1);
	agent = tazterm_split_find_agent(tw->split);
	if (agent && agent != term &&
	    tazterm_term_paste_text(agent, GDK_SELECTION_CLIPBOARD, prompt))
		gtk_widget_grab_focus(GTK_WIDGET(agent));
	if (tazterm_debug())
		g_printerr("tazterm: explain (%lu bytes) -> %s\n",
		    (unsigned long) strlen(prompt),
		    agent && agent != term ? "agent" : "clipboard");
	g_free(prompt);
}

static void
on_agent_split(GtkMenuItem *item, gpointer data)
{
	(void) item;
	ai_agent_split(TW(data), NULL); /* default */
}

/* "Split this agent" request (per-agent menu); freed with the menu. */
typedef struct {
	TaztermWin *tw;
	char *agent;
} AgentSplitReq;

static void
agent_req_free(gpointer data, GClosure *closure)
{
	AgentSplitReq *req = data;

	(void) closure;
	g_free(req->agent);
	g_free(req);
}

static void
on_agent_split_named(GtkMenuItem *item, gpointer data)
{
	AgentSplitReq *req = data;

	(void) item;
	ai_agent_split(req->tw, req->agent);
}

static void
on_copy_scrollback(GtkMenuItem *item, gpointer data)
{
	(void) item;
	ai_copy_scrollback(TW(data));
}

static void
on_explain(GtkMenuItem *item, gpointer data)
{
	(void) item;
	ai_explain(TW(data));
}

/* Pending send: win is a weak pointer (the window may close while
 * PRIMARY answers). */
typedef struct {
	GtkWidget *win;
} SelReq;

static void
on_selection_for_agent(GtkClipboard *clip, const char *text, gpointer data)
{
	SelReq *req = data;
	TaztermWin *tw;
	VteTerminal *agent;

	(void) clip;
	if (!req->win)
		goto out;
	tw = g_object_get_data(G_OBJECT(req->win), "tazterm-win");
	/* Looked up after the wait: the agent pane may be gone. */
	agent = tw ? tazterm_split_find_agent(tw->split) : NULL;
	if (!agent) {
		if (tazterm_debug())
			g_printerr("tazterm: send to agent: agent gone\n");
		goto out;
	}
	if (!tazterm_term_paste_text(agent, GDK_SELECTION_PRIMARY, text)) {
		if (tazterm_debug())
			g_printerr("tazterm: send to agent: paste refused\n");
		goto out;
	}
	gtk_widget_grab_focus(GTK_WIDGET(agent));
	if (tazterm_debug())
		g_printerr("tazterm: sent %lu bytes to agent\n",
		    (unsigned long) strlen(text));
out:
	if (req->win)
		g_object_remove_weak_pointer(G_OBJECT(req->win),
		    (gpointer *) &req->win);
	g_free(req);
}

/* Paste the source pane's selection into the agent pane (sanitized,
 * no Enter: the user adds a question and submits), then focus it.
 * No agent pane, no selection, or source IS the agent: do nothing. */
static void
ai_send_to_agent(TaztermWin *tw, VteTerminal *src)
{
	VteTerminal *agent;
	SelReq *req;

	if (!src)
		src = tazterm_split_active_term(tw->split);
	if (!src)
		return;
	agent = tazterm_split_find_agent(tw->split);
	if (!agent) {
		if (tazterm_debug())
			g_printerr("tazterm: send to agent: no agent pane\n");
		return;
	}
	if (agent == src) {
		if (tazterm_debug())
			g_printerr("tazterm: send to agent: source is agent\n");
		return;
	}
	if (!vte_terminal_get_has_selection(src)) {
		if (tazterm_debug())
			g_printerr(
			    "tazterm: send to agent without selection\n");
		return;
	}
	/* VTE 0.56 has no selected-text getter: the selection goes
	 * through PRIMARY (CLIPBOARD untouched), read asynchronously. */
	vte_terminal_copy_primary(src);
	req = g_new0(SelReq, 1);
	req->win = tw->win;
	g_object_add_weak_pointer(G_OBJECT(req->win), (gpointer *) &req->win);
	gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY),
	    on_selection_for_agent, req);
}

/* "Send to agent" request (clicked source pane); freed with the menu. */
typedef struct {
	TaztermWin *tw;
	VteTerminal *src; /* weak pointer: nulled if the pane dies */
} SendReq;

static void
send_req_free(gpointer data, GClosure *closure)
{
	SendReq *req = data;

	(void) closure;
	if (req->src)
		g_object_remove_weak_pointer(G_OBJECT(req->src),
		    (gpointer *) &req->src);
	g_free(req);
}

static void
on_send_to_agent(GtkMenuItem *item, gpointer data)
{
	SendReq *req = data;

	(void) item;
	/* The pane may have closed while the menu was open: weak
	 * pointer is NULL then, and ai_send_to_agent falls back
	 * to the active pane. */
	ai_send_to_agent(req->tw, req->src);
}

static void
menu_add(GtkWidget *menu, const char *label,
    GCallback cb, gpointer data)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label(label);
	g_signal_connect(item, "activate", cb, data);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

/* Variant destroying data when the item dies (ephemeral menu). */
static void
menu_add_data(GtkWidget *menu, const char *label,
    GCallback cb, gpointer data, GClosureNotify destroy)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label(label);
	g_signal_connect_data(item, "activate", cb, data, destroy, 0);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void
show_popup(TaztermWin *tw, VteTerminal *term, GdkEventButton *event)
{
	GtkWidget *menu;
	GtkWidget *sep;

	menu = gtk_menu_new();
	menu_add(menu, _("Copy"), G_CALLBACK(on_copy), term);
	menu_add(menu, _("Paste"), G_CALLBACK(on_paste), term);
	menu_add(menu, _("Select All"), G_CALLBACK(on_select_all),
	    term);
	sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
	menu_add(menu, _("Find…"), G_CALLBACK(on_search_menu), tw);
	sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
	menu_add(menu, _("Split Side by Side"), G_CALLBACK(on_split_v),
	    tw);
	menu_add(menu, _("Split Stacked"), G_CALLBACK(on_split_h), tw);
	menu_add(menu, _("Close Pane"), G_CALLBACK(on_close_pane),
	    tw);
	sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
	if (tw->agent) {
		char *label;
		int i;

		/* Shortcut = default; menu = one item per detected agent. */
		label = g_strdup_printf(
		    _("Open Agent Split (%s)"), tw->agent);
		menu_add(menu, label, G_CALLBACK(on_agent_split), tw);
		g_free(label);
		for (i = 0; tw->agents && tw->agents[i]; i++) {
			AgentSplitReq *req;

			if (!g_strcmp0(tw->agents[i], tw->agent))
				continue;
			req = g_new0(AgentSplitReq, 1);
			req->tw = tw;
			req->agent = g_strdup(tw->agents[i]);
			label = g_strdup_printf(_("Agent Split: %s"),
			    tw->agents[i]);
			menu_add_data(menu, label,
			    G_CALLBACK(on_agent_split_named), req,
			    agent_req_free);
			g_free(label);
		}
	}
	menu_add(menu, _("Copy Scrollback"),
	    G_CALLBACK(on_copy_scrollback), tw);
	menu_add(menu, _("Explain Last Error"),
	    G_CALLBACK(on_explain), tw);
	{
		GtkWidget *item;
		SendReq *req;

		/* Source = the clicked pane (captured now): focusing the
		 * agent after the send must not change what gets sent. */
		req = g_new0(SendReq, 1);
		req->tw = tw;
		req->src = term;
		g_object_add_weak_pointer(G_OBJECT(term),
		    (gpointer *) &req->src);
		item = gtk_menu_item_new_with_label(
		    _("Send to Agent"));
		g_signal_connect_data(item, "activate",
		    G_CALLBACK(on_send_to_agent), req, send_req_free, 0);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		gtk_widget_set_sensitive(item,
		    vte_terminal_get_has_selection(term) &&
		    tazterm_split_find_agent(tw->split) != NULL);
	}
	sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
	menu_add(menu, _("Zoom In"), G_CALLBACK(on_zoom_in), tw);
	menu_add(menu, _("Zoom Out"), G_CALLBACK(on_zoom_out), tw);
	menu_add(menu, _("Normal Size"), G_CALLBACK(on_zoom_reset),
	    tw);
	sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
	{
		GtkWidget *fs;

		fs = gtk_check_menu_item_new_with_label(_("Fullscreen"));
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(fs),
		    tw->fullscreen);
		g_signal_connect(fs, "activate", G_CALLBACK(on_fullscreen),
		    tw);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), fs);
	}

	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);
}

/* --- Ctrl+click: URLs and file:line --------------------------------- */

/* Editors that run inside a terminal (a GUI $EDITOR such as leafpad
 * would leave an empty pane behind). */
static gboolean
editor_in_terminal(const char *cmd)
{
	static const char *const known[] = {
		"vi", "vim", "nvim", "nano", "mcedit", "micro", "ne", "joe",
		"jed", "mg", "zile", "kak", "hx", "helix", NULL
	};
	char **argv = NULL;
	char *base;
	gboolean ok = FALSE;
	int i;

	if (!cmd || !*cmd || !g_shell_parse_argv(cmd, NULL, &argv, NULL))
		return FALSE;
	base = g_path_get_basename(argv[0]);
	for (i = 0; known[i]; i++)
		if (!strcmp(base, known[i]))
			ok = TRUE;
	/* emacs only with -nw */
	if (!strcmp(base, "emacs") && strstr(cmd, "-nw"))
		ok = TRUE;
	g_free(base);
	g_strfreev(argv);
	return ok;
}

static const char *
editor_cmd(TaztermWin *tw)
{
	const char *e;

	if (tw->cfg->editor)
		return tw->cfg->editor;
	e = g_getenv("VISUAL");
	if (e && *e)
		return e;
	e = g_getenv("EDITOR");
	if (editor_in_terminal(e))
		return e;
	return "vi";
}

/* "src/main.c:42:5" -> editor +42 src/main.c in a split, resolved
 * from the clicked pane's cwd. Only existing regular files. */
static void
open_file_match(TaztermWin *tw, VteTerminal *term, const char *match)
{
	char **part;
	char *cwd, *path, *line_arg, *base;
	char **ed = NULL;
	GPtrArray *argv;
	int i;

	part = g_strsplit(match, ":", 3);
	if (!part[0] || !part[1]) {
		g_strfreev(part);
		return;
	}
	cwd = tazterm_term_get_cwd(term);
	if (g_path_is_absolute(part[0]))
		path = g_strdup(part[0]);
	else
		path = g_build_filename(cwd ? cwd : g_get_home_dir(), part[0],
		    NULL);
	g_free(cwd);
	if (!g_file_test(path, G_FILE_TEST_IS_REGULAR) ||
	    !g_shell_parse_argv(editor_cmd(tw), NULL, &ed, NULL)) {
		if (tazterm_debug())
			g_printerr("tazterm: open %s: no such file\n", path);
		g_free(path);
		g_strfreev(part);
		return;
	}
	argv = g_ptr_array_new();
	for (i = 0; ed[i]; i++)
		g_ptr_array_add(argv, ed[i]);
	/* busybox vi has no +N; -c N works there and in vim/nvim. */
	base = g_path_get_basename(ed[0]);
	if (!strcmp(base, "vi") || !strcmp(base, "vim") ||
	    !strcmp(base, "nvim")) {
		g_ptr_array_add(argv, "-c");
		line_arg = g_strdup(part[1]);
	} else {
		line_arg = g_strdup_printf("+%s", part[1]);
	}
	g_free(base);
	g_ptr_array_add(argv, line_arg);
	g_ptr_array_add(argv, path);
	g_ptr_array_add(argv, NULL);
	tazterm_split_vertical_cmd(tw->split, (char **) argv->pdata);
	if (tazterm_debug())
		g_printerr("tazterm: open %s +%s with %s\n", path, part[1],
		    ed[0]);
	g_ptr_array_free(argv, TRUE);
	g_strfreev(ed);
	g_free(line_arg);
	g_free(path);
	g_strfreev(part);
}

/* $BROWSER (SliTaz applications.conf), else GIO's default handler.
 * Never on a plain click: Ctrl is the user's intent. */
static void
open_url_match(const char *url)
{
	const char *browser = g_getenv("BROWSER");
	char **argv = NULL;
	GError *err = NULL;

	if (browser && *browser &&
	    g_shell_parse_argv(browser, NULL, &argv, NULL)) {
		GPtrArray *a = g_ptr_array_new();
		int i;

		for (i = 0; argv[i]; i++)
			g_ptr_array_add(a, argv[i]);
		g_ptr_array_add(a, (gpointer) url);
		g_ptr_array_add(a, NULL);
		if (!g_spawn_async(NULL, (char **) a->pdata, NULL,
		    G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err)) {
			g_warning("tazterm: %s: %s", argv[0], err->message);
			g_clear_error(&err);
		}
		g_ptr_array_free(a, TRUE);
		g_strfreev(argv);
		return;
	}
	if (!g_app_info_launch_default_for_uri(url, NULL, &err)) {
		g_warning("tazterm: open %s: %s", url, err->message);
		g_clear_error(&err);
	}
}

static gboolean
on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	TaztermWin *tw = TW(data);

	if (event->type == GDK_BUTTON_PRESS && event->button == 1 &&
	    (event->state & GDK_CONTROL_MASK)) {
		int tag = -1;
		char *m = vte_terminal_match_check_event(VTE_TERMINAL(widget),
		    (GdkEvent *) event, &tag);

		if (m) {
			/* The split opens beside the clicked pane. */
			gtk_widget_grab_focus(widget);
			if (tazterm_term_match_is_url(VTE_TERMINAL(widget), tag))
				open_url_match(m);
			else
				open_file_match(tw, VTE_TERMINAL(widget), m);
			g_free(m);
			return TRUE;
		}
	}

	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		gtk_widget_grab_focus(widget);
		if (tazterm_debug())
			g_printerr("tazterm: popup menu\n");
		show_popup(tw, VTE_TERMINAL(widget), event);
		return TRUE;
	}
	return FALSE;
}

/* --- keyboard ----------------------------------------------------------- */

/* Ctrl+Shift+Up/Down: scroll so the previous / next prompt is the top
 * row (bash panes). Past the last prompt: back to the bottom. */
static void
prompt_jump(VteTerminal *term, int dir)
{
	GtkAdjustment *va;
	glong top, row;

	va = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(term));
	top = (glong) gtk_adjustment_get_value(va);
	if (tazterm_blocks_prompt_row(term, top, dir, &row))
		gtk_adjustment_set_value(va, (gdouble) row);
	else if (dir > 0)
		gtk_adjustment_set_value(va, gtk_adjustment_get_upper(va) -
		    gtk_adjustment_get_page_size(va));
}

static gboolean
on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	TaztermWin *tw = TW(data);
	VteTerminal *term;
	gboolean ctrl, shift, alt;

	(void) widget;

	term = tazterm_split_active_term(tw->split);
	ctrl = (event->state & GDK_CONTROL_MASK) != 0;
	shift = (event->state & GDK_SHIFT_MASK) != 0;
	alt = (event->state & GDK_MOD1_MASK) != 0;

	/* Esc in the terminal while search is open: close it instead of
	 * sending \e to the shell. */
	if (!ctrl && !alt && event->keyval == GDK_KEY_Escape &&
	    search_is_shown(tw)) {
		search_hide(tw);
		return TRUE;
	}

	/* F11: fullscreen (no modifier, like usual terminals). */
	if (!ctrl && !alt && !shift && event->keyval == GDK_KEY_F11) {
		fullscreen_set(tw, !tw->fullscreen);
		return TRUE;
	}

	/* Pane navigation. */
	if (alt && !ctrl) {
		switch (event->keyval) {
		case GDK_KEY_Left:
		case GDK_KEY_KP_Left:
			tazterm_split_focus_dir(tw->split, TAZTERM_LEFT);
			return TRUE;
		case GDK_KEY_Right:
		case GDK_KEY_KP_Right:
			tazterm_split_focus_dir(tw->split, TAZTERM_RIGHT);
			return TRUE;
		case GDK_KEY_Up:
		case GDK_KEY_KP_Up:
			tazterm_split_focus_dir(tw->split, TAZTERM_UP);
			return TRUE;
		case GDK_KEY_Down:
		case GDK_KEY_KP_Down:
			tazterm_split_focus_dir(tw->split, TAZTERM_DOWN);
			return TRUE;
		default:
			break;
		}
		return FALSE;
	}

	if (ctrl && shift) {
		switch (event->keyval) {
		case GDK_KEY_C:
		case GDK_KEY_c:
			if (term)
				vte_terminal_copy_clipboard_format(term,
				    VTE_FORMAT_TEXT);
			return TRUE;
		case GDK_KEY_V:
		case GDK_KEY_v:
			if (term)
				tazterm_term_paste_clipboard(term);
			return TRUE;
		case GDK_KEY_Q:
		case GDK_KEY_q:
			close_confirm(tw, NULL);
			return TRUE;
		case GDK_KEY_F:
		case GDK_KEY_f:
			search_show(tw);
			return TRUE;
		case GDK_KEY_E:
		case GDK_KEY_e:
			tazterm_split_vertical(tw->split);
			return TRUE;
		case GDK_KEY_O:
		case GDK_KEY_o:
			tazterm_split_horizontal(tw->split);
			return TRUE;
		case GDK_KEY_W:
		case GDK_KEY_w:
			close_confirm(tw, term);
			return TRUE;
		case GDK_KEY_A:
		case GDK_KEY_a:
			ai_agent_split(tw, NULL);
			return TRUE;
		case GDK_KEY_T:
		case GDK_KEY_t:
			ai_send_to_agent(tw, term);
			return TRUE;
		case GDK_KEY_S:
		case GDK_KEY_s:
			ai_copy_scrollback(tw);
			return TRUE;
		case GDK_KEY_X:
		case GDK_KEY_x:
			ai_explain(tw);
			return TRUE;
		case GDK_KEY_Up:
		case GDK_KEY_Down:
			if (term)
				prompt_jump(term,
				    event->keyval == GDK_KEY_Up ? -1 : 1);
			return TRUE;
		default:
			break;
		}
	}
	if (ctrl && !alt) {
		if (!term)
			return FALSE;
		switch (event->keyval) {
		case GDK_KEY_plus:
		case GDK_KEY_KP_Add:
		case GDK_KEY_equal: /* '+' without Shift on some layouts */
			tazterm_split_zoom_in(tw->split);
			return TRUE;
		case GDK_KEY_minus:
		case GDK_KEY_KP_Subtract:
			tazterm_split_zoom_out(tw->split);
			return TRUE;
		case GDK_KEY_0:
		case GDK_KEY_KP_0:
			tazterm_split_zoom_reset(tw->split);
			return TRUE;
		default:
			break;
		}
	}
	return FALSE;
}

/* --- split hooks ---------------------------------------------------------- */

static void
on_title_changed(VteTerminal *term, gpointer data)
{
	TaztermWin *tw = TW(data);

	if (term == tazterm_split_active_term(tw->split))
		title_update(tw, term);
}

static void
on_pane_focus(VteTerminal *term, gpointer data)
{
	TaztermWin *tw = TW(data);
	const char *text;

	title_update(tw, term);
	/* The user is here: the bell has been seen. */
	g_object_set_data(G_OBJECT(term), "tazterm-bell", NULL);
	status_update(term);
	/* Open search follows the active pane. */
	if (search_is_shown(tw)) {
		text = gtk_entry_get_text(GTK_ENTRY(tw->search_entry));
		if (text && *text)
			tazterm_term_search(term, text);
	}
}

/* --- status bar --------------------------------------------------------- */

/* Pane state kept as object data on the terminal:
 *   tazterm-last-output  monotonic seconds of the last screen change
 *   tazterm-bell         BEL not seen yet by the user
 *   tazterm-exit         held pane: wait status + 1 */
#define STATUS_BUSY_SECS 2

static int
now_secs(void)
{
	return (int) (g_get_monotonic_time() / G_USEC_PER_SEC);
}

static char *
fmt_age(int secs)
{
	if (secs < 60)
		return g_strdup_printf("%ds", secs);
	if (secs < 3600)
		return g_strdup_printf("%dm", secs / 60);
	return g_strdup_printf("%dh%02d", secs / 3600, secs % 3600 / 60);
}

/* $HOME -> ~ (the bar is narrow). */
static char *
short_path(const char *path)
{
	const char *home = g_get_home_dir();
	gsize n = home ? strlen(home) : 0;

	if (n > 1 && g_str_has_prefix(path, home) &&
	    (path[n] == '/' || path[n] == '\0'))
		return g_strconcat("~", path + n, NULL);
	return g_strdup(path);
}

static void
label_set(GtkWidget *label, const char *text, gboolean markup)
{
	if (!g_strcmp0(gtk_label_get_label(GTK_LABEL(label)), text))
		return;
	if (tazterm_debug())
		g_printerr("tazterm: status '%s'\n", text);
	if (markup)
		gtk_label_set_markup(GTK_LABEL(label), text);
	else
		gtk_label_set_text(GTK_LABEL(label), text);
}

/* Left: "id · role · process · cwd". Right: what the pane is doing,
 * agents first: working (output flowing) or waiting for the user. */
static void
status_update(VteTerminal *term)
{
	GtkWidget *left, *right;
	gboolean agent, shell;
	char *proc, *cwd, *spath, *ltext, *age;
	const char *shell_path;
	const char *base;
	char *rtext;
	int idle, exitst, running, last_exit;

	left = g_object_get_data(G_OBJECT(term), "tazterm-status-left");
	right = g_object_get_data(G_OBJECT(term), "tazterm-status-right");
	if (!left || !right)
		return;
	agent = g_object_get_data(G_OBJECT(term), "tazterm-agent") != NULL;
	shell_path = g_object_get_data(G_OBJECT(term), "tazterm-shell");
	shell = shell_path != NULL;
	exitst = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(term),
	    "tazterm-exit"));

	proc = exitst ? NULL : tazterm_term_get_process(term);
	cwd = tazterm_term_get_cwd(term);
	spath = cwd ? short_path(cwd) : g_strdup("");
	ltext = g_strdup_printf("%d · %s · %s%s%s",
	    tazterm_term_get_id(term),
	    agent ? _("agent") : shell ? _("shell") : _("command"),
	    proc ? proc : "-", *spath ? " · " : "", spath);
	label_set(left, ltext, FALSE);

	idle = now_secs() - GPOINTER_TO_INT(g_object_get_data(G_OBJECT(term),
	    "tazterm-last-output"));
	age = fmt_age(idle);
	base = shell_path ? strrchr(shell_path, '/') : NULL;
	base = base ? base + 1 : shell_path;
	if (exitst)
		rtext = WIFEXITED(exitst - 1) ?
		    g_strdup_printf(_("done (exit %d)"),
		    WEXITSTATUS(exitst - 1)) : g_strdup(_("done (signal)"));
	else if (g_object_get_data(G_OBJECT(term), "tazterm-bell"))
		rtext = g_strdup_printf("<span foreground=\"#f57900\">● %s"
		    "</span>", _("waiting for you"));
	else if ((running = tazterm_blocks_running(term)) >= 0) {
		char *rage = fmt_age(running);

		rtext = g_strdup_printf("<span foreground=\"#73d216\">● %s · "
		    "%s</span>", idle < STATUS_BUSY_SECS ? _("active") :
		    _("running"), rage);
		g_free(rage);
	} else if (idle < STATUS_BUSY_SECS)
		rtext = g_strdup_printf("<span foreground=\"#73d216\">● %s"
		    "</span>", agent ? _("working") : _("active"));
	else if (!agent && (last_exit = tazterm_blocks_last_exit(term)) > 0)
		rtext = g_strdup_printf("<span foreground=\"#ef2929\">✗ %s "
		    "%d</span>", _("exit"), last_exit);
	else if (agent)
		rtext = g_strdup_printf(_("idle · %s"), age);
	else if (shell && proc && g_strcmp0(proc, base) != 0)
		rtext = g_strdup_printf(_("quiet · %s"), age);
	else
		rtext = g_strdup("");
	label_set(right, rtext, TRUE);

	g_free(proc);
	g_free(cwd);
	g_free(spath);
	g_free(ltext);
	g_free(age);
	g_free(rtext);
}

static gboolean
status_tick(gpointer data)
{
	TaztermWin *tw = TW(data);
	GPtrArray *arr;
	guint i;

	arr = tazterm_split_list(tw->split);
	for (i = 0; i < arr->len; i++)
		status_update(g_ptr_array_index(arr, i));
	g_ptr_array_free(arr, TRUE);
	return G_SOURCE_CONTINUE;
}

static void
on_contents_changed(VteTerminal *term, gpointer data)
{
	(void) data;
	g_object_set_data(G_OBJECT(term), "tazterm-last-output",
	    GINT_TO_POINTER(now_secs()));
}

/* BEL from a pane (an agent asking for permission or done, a build
 * ending with \a): orange outline when it is not the pane in use,
 * urgency hint (taskbar) when the window is not focused. */
static void
on_bell(VteTerminal *term, gpointer data)
{
	TaztermWin *tw = TW(data);

	tazterm_split_attention(tw->split, term);
	if (term != tazterm_split_active_term(tw->split)) {
		g_object_set_data(G_OBJECT(term), "tazterm-bell",
		    GINT_TO_POINTER(TRUE));
		status_update(term);
	}
	if (!gtk_window_is_active(GTK_WINDOW(tw->win)))
		gtk_window_set_urgency_hint(GTK_WINDOW(tw->win), TRUE);
	if (tazterm_debug())
		g_printerr("tazterm: bell pane %d\n", tazterm_term_get_id(term));
}

static gboolean
on_win_focus_in(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	(void) event;
	(void) data;
	gtk_window_set_urgency_hint(GTK_WINDOW(widget), FALSE);
	return FALSE;
}

/* A shell ended: remove its pane (close the window when last).
 * --hold pane: keep its output on screen, closed by the user. */
static void
on_child_exited(VteTerminal *term, int status, gpointer data)
{
	TaztermWin *tw = TW(data);

	if (g_object_get_data(G_OBJECT(term), "tazterm-hold")) {
		char *msg, *note;

		if (WIFEXITED(status))
			note = g_strdup_printf(_("exited with code %d — "
			    "Ctrl+Shift+W to close"), WEXITSTATUS(status));
		else
			note = g_strdup_printf(_("killed by signal %d — "
			    "Ctrl+Shift+W to close"), WTERMSIG(status));
		msg = g_strdup_printf("\r\n[%s]\r\n", note);
		g_free(note);
		vte_terminal_feed(term, msg, -1);
		g_free(msg);
		g_object_set_data(G_OBJECT(term), "tazterm-exit",
		    GINT_TO_POINTER(status + 1));
		status_update(term);
		return;
	}
	tazterm_split_remove_term(tw->split, term);
}

/* xterm-style geometry: COLSxROWS[+X+Y], the terminal cell grid. */
static void
geometry_apply(TaztermWin *tw, VteTerminal *term, const char *geo)
{
	unsigned cols, rows;
	int x, y, n;

	n = sscanf(geo, "%ux%u%d%d", &cols, &rows, &x, &y);
	if (n < 2 || cols < 2 || rows < 1 || cols > 1000 || rows > 1000) {
		g_warning("tazterm: bad geometry '%s' (COLSxROWS[+X+Y])", geo);
		return;
	}
	vte_terminal_set_size(term, cols, rows);
	gtk_window_set_default_size(GTK_WINDOW(tw->win), -1, -1);
	if (n == 4)
		gtk_window_move(GTK_WINDOW(tw->win), x, y);
}

static void
on_split_empty(gpointer data)
{
	TaztermWin *tw = TW(data);

	gtk_widget_destroy(tw->win);
}

/* Hook up signals for every terminal created by the split. */
static void
term_setup(VteTerminal *term, gpointer data)
{
	TaztermWin *tw = TW(data);

	g_signal_connect(term, "key-press-event",
	    G_CALLBACK(on_key_press), tw);
	g_signal_connect(term, "button-press-event",
	    G_CALLBACK(on_button_press), tw);
	g_signal_connect(term, "child-exited",
	    G_CALLBACK(on_child_exited), tw);
	g_signal_connect(term, "window-title-changed",
	    G_CALLBACK(on_title_changed), tw);
	g_signal_connect(term, "bell", G_CALLBACK(on_bell), tw);
	g_signal_connect(term, "contents-changed",
	    G_CALLBACK(on_contents_changed), NULL);
}

/* --- build -------------------------------------------------------------- */

GtkWidget *
tazterm_window_new(TaztermConfig *cfg, const TaztermWinOpts *opts)
{
	TaztermWin *tw;
	GtkWidget *box;
	TaztermSplitHooks hooks;
	VteTerminal *first;

	tw = g_new0(TaztermWin, 1);
	tw->cfg = cfg;
	tw->title = g_strdup(opts->title && *opts->title ? opts->title :
	    "TazTerm");

	/* AI agents detected once per window. */
	tw->agents = tazterm_ai_detect(cfg->ai_agent, &tw->agent);
	if (tazterm_debug()) {
		char *list = g_strjoinv(",", tw->agents);

		g_printerr("tazterm: agents: %s (default: %s)\n",
		    (list && *list) ? list : "(none)",
		    tw->agent ? tw->agent : "(none)");
		g_free(list);
	}

	tw->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(tw->win), tw->title);
	gtk_window_set_icon_name(GTK_WINDOW(tw->win), "tazterm");
	gtk_window_set_default_size(GTK_WINDOW(tw->win), 900, 600);
	/* Socket closed before the panes go: no request on a dying tree. */
	g_signal_connect(tw->win, "destroy", G_CALLBACK(tazterm_ctl_stop),
	    NULL);
	g_signal_connect(tw->win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(tw->win, "window-state-event",
	    G_CALLBACK(on_window_state), tw);
	g_signal_connect(tw->win, "delete-event",
	    G_CALLBACK(on_delete_event), tw);
	g_signal_connect(tw->win, "focus-in-event",
	    G_CALLBACK(on_win_focus_in), NULL);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(tw->win), box);

	/* Search bar (hidden by default), with close button. */
	tw->search_bar = gtk_search_bar_new();
	gtk_search_bar_set_show_close_button(
	    GTK_SEARCH_BAR(tw->search_bar), TRUE);
	tw->search_entry = gtk_search_entry_new();
	gtk_container_add(GTK_CONTAINER(tw->search_bar), tw->search_entry);
	gtk_box_pack_start(GTK_BOX(box), tw->search_bar, FALSE, FALSE, 0);
	g_signal_connect(tw->search_entry, "search-changed",
	    G_CALLBACK(on_search_changed), tw);
	g_signal_connect(tw->search_entry, "key-press-event",
	    G_CALLBACK(on_search_key), tw);

	/* Before the first pane: its env gets TAZTERM_SOCKET. */
	tazterm_ctl_start(cfg);

	hooks.term_setup = term_setup;
	hooks.term_setup_data = tw;
	hooks.focus_changed = on_pane_focus;
	hooks.focus_data = tw;
	hooks.empty = on_split_empty;
	hooks.empty_data = tw;
	tw->split = tazterm_split_new(cfg, opts->shell, opts->workdir,
	    opts->command, &hooks);
	gtk_box_pack_start(GTK_BOX(box), tw->split, TRUE, TRUE, 0);
	first = tazterm_split_active_term(tw->split);
	if (opts->hold && opts->command)
		g_object_set_data(G_OBJECT(first), "tazterm-hold",
		    GINT_TO_POINTER(TRUE));
	if (opts->geometry)
		geometry_apply(tw, first, opts->geometry);
	tazterm_ctl_set_split(tw->split);

	if (cfg->status_bar) {
		tw->status_timer = g_timeout_add_seconds(1, status_tick, tw);
		g_signal_connect_swapped(tw->win, "destroy",
		    G_CALLBACK(g_source_remove),
		    GUINT_TO_POINTER(tw->status_timer));
	}

	/* State attached to the window, freed on destroy. */
	g_object_set_data_full(G_OBJECT(tw->win), "tazterm-win", tw, win_free);

	debug_win = tw;
	if (tazterm_debug())
		g_unix_signal_add(SIGUSR1, on_sigusr1, NULL);
	/* Clean exit on kill / logout: the ctl socket gets unlinked. */
	g_unix_signal_add(SIGTERM, on_quit_signal, tw->win);
	g_unix_signal_add(SIGHUP, on_quit_signal, tw->win);
	g_unix_signal_add(SIGINT, on_quit_signal, tw->win);

	return tw->win;
}
