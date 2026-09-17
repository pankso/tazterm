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
 *   Ctrl+Shift+S          : copy scrollback (clipboard + /tmp)
 *   Ctrl+Shift+X          : explain last error (/tmp + clipboard)
 *   Alt+Arrows            : focus neighbor pane
 * Every terminal action (copy, search, zoom) targets the active pane.
 * The last closed pane quits (via the split's empty hook).
 */
#include "tazterm-window.h"
#include "tazterm-ai.h"
#include "tazterm-split.h"
#include "tazterm-term.h"

#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <signal.h>

typedef struct {
	GtkWidget *win;
	GtkWidget *split;
	TaztermConfig *cfg; /* not owned (main) */
	GtkWidget *search_bar;
	GtkWidget *search_entry;
	char **agents;      /* detected on PATH (g_strfreev) */
	char *agent;        /* default (g_free, NULL if none) */
	gboolean fullscreen;
} TaztermWin;

static void
win_free(gpointer data)
{
	TaztermWin *tw = data;

	g_strfreev(tw->agents);
	g_free(tw->agent);
	g_free(tw);
}

#define TW(x) ((TaztermWin *) (x))

/* Debug aid: SIGUSR1 dumps the active pane's text (headless rendering
 * checks). Single-window app: global is fine. */
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
	g_file_set_contents(path, text ? text : "", -1, NULL);
	g_printerr("tazterm: dump -> %s (%lu bytes)\n", path,
	    (unsigned long) (text ? strlen(text) : 0));
	g_free(text);
	g_free(path);
	return TRUE;
}

/* --- title -------------------------------------------------------------- */

static void
title_update(TaztermWin *tw, VteTerminal *term)
{
	const char *title;
	char *full;

	title = term ? vte_terminal_get_window_title(term) : NULL;
	if (title && *title) {
		full = g_strdup_printf("%s - TazTerm", title);
		gtk_window_set_title(GTK_WINDOW(tw->win), full);
		g_free(full);
	} else {
		gtk_window_set_title(GTK_WINDOW(tw->win), "TazTerm");
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
	vte_terminal_paste_clipboard(VTE_TERMINAL(data));
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

static void
on_close_pane(GtkMenuItem *item, gpointer data)
{
	TaztermWin *tw = TW(data);

	(void) item;
	tazterm_split_close_current(tw->split);
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

	if (!agent)
		agent = tw->agent;
	if (!agent) {
		if (tazterm_debug())
			g_printerr("tazterm: agent split without agent\n");
		return;
	}
	cmd = tazterm_ai_launch_cmd(agent);
	tazterm_split_vertical_cmd(tw->split, cmd);
	if (tazterm_debug())
		g_printerr("tazterm: agent split: %s (agent=%s)\n", cmd,
		    agent);
}

static void
ai_copy_scrollback(TaztermWin *tw)
{
	VteTerminal *term;
	char *text;
	char *path;

	term = tazterm_split_active_term(tw->split);
	if (!term)
		return;
	text = tazterm_ai_last_lines(term, tw->cfg->ai_capture_lines);
	path = tazterm_ai_save_capture(tw->win, text, "capture");
	if (tazterm_debug())
		g_printerr("tazterm: scrollback copy -> %s\n",
		    path ? path : "(failed)");
	g_free(text);
	g_free(path);
}

static void
ai_explain(TaztermWin *tw)
{
	VteTerminal *term;
	char *path;

	term = tazterm_split_active_term(tw->split);
	if (!term)
		return;
	path = tazterm_ai_explain(tw->win, term,
	    tw->cfg->ai_explain_lines);
	if (tazterm_debug())
		g_printerr("tazterm: explain -> %s\n",
		    path ? path : "(failed)");
	g_free(path);
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
	menu_add(menu, _("Copier"), G_CALLBACK(on_copy), term);
	menu_add(menu, _("Coller"), G_CALLBACK(on_paste), term);
	menu_add(menu, _("Tout sélectionner"), G_CALLBACK(on_select_all),
	    term);
	sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
	menu_add(menu, _("Rechercher…"), G_CALLBACK(on_search_menu), tw);
	sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
	menu_add(menu, _("Diviser côté à côté"), G_CALLBACK(on_split_v),
	    tw);
	menu_add(menu, _("Diviser empilés"), G_CALLBACK(on_split_h), tw);
	menu_add(menu, _("Fermer ce panneau"), G_CALLBACK(on_close_pane),
	    tw);
	sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
	if (tw->agent) {
		char *label;
		int i;

		/* Shortcut = default; menu = one item per detected agent. */
		label = g_strdup_printf(
		    _("Ouvrir un split agent (%s)"), tw->agent);
		menu_add(menu, label, G_CALLBACK(on_agent_split), tw);
		g_free(label);
		for (i = 0; tw->agents && tw->agents[i]; i++) {
			AgentSplitReq *req;

			if (!g_strcmp0(tw->agents[i], tw->agent))
				continue;
			req = g_new0(AgentSplitReq, 1);
			req->tw = tw;
			req->agent = g_strdup(tw->agents[i]);
			label = g_strdup_printf(_("Split agent : %s"),
			    tw->agents[i]);
			menu_add_data(menu, label,
			    G_CALLBACK(on_agent_split_named), req,
			    agent_req_free);
			g_free(label);
		}
	}
	menu_add(menu, _("Copier le scrollback"),
	    G_CALLBACK(on_copy_scrollback), tw);
	menu_add(menu, _("Expliquer la dernière erreur"),
	    G_CALLBACK(on_explain), tw);
	sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
	menu_add(menu, _("Zoom avant"), G_CALLBACK(on_zoom_in), tw);
	menu_add(menu, _("Zoom arrière"), G_CALLBACK(on_zoom_out), tw);
	menu_add(menu, _("Taille normale"), G_CALLBACK(on_zoom_reset),
	    tw);
	sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
	{
		GtkWidget *fs;

		fs = gtk_check_menu_item_new_with_label(_("Plein écran"));
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(fs),
		    tw->fullscreen);
		g_signal_connect(fs, "activate", G_CALLBACK(on_fullscreen),
		    tw);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), fs);
	}

	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);
}

static gboolean
on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	TaztermWin *tw = TW(data);

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
				vte_terminal_paste_clipboard(term);
			return TRUE;
		case GDK_KEY_Q:
		case GDK_KEY_q:
			gtk_widget_destroy(tw->win);
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
			tazterm_split_close_current(tw->split);
			return TRUE;
		case GDK_KEY_A:
		case GDK_KEY_a:
			ai_agent_split(tw, NULL);
			return TRUE;
		case GDK_KEY_S:
		case GDK_KEY_s:
			ai_copy_scrollback(tw);
			return TRUE;
		case GDK_KEY_X:
		case GDK_KEY_x:
			ai_explain(tw);
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
	/* Open search follows the active pane. */
	if (search_is_shown(tw)) {
		text = gtk_entry_get_text(GTK_ENTRY(tw->search_entry));
		if (text && *text)
			tazterm_term_search(term, text);
	}
}

/* A shell ended: remove its pane (close the window when last). */
static void
on_child_exited(VteTerminal *term, int status, gpointer data)
{
	TaztermWin *tw = TW(data);

	(void) status;

	tazterm_split_remove_term(tw->split, term);
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
}

/* --- build -------------------------------------------------------------- */

GtkWidget *
tazterm_window_new(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override)
{
	TaztermWin *tw;
	GtkWidget *box;
	TaztermSplitHooks hooks;

	tw = g_new0(TaztermWin, 1);
	tw->cfg = cfg;

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
	gtk_window_set_title(GTK_WINDOW(tw->win), "TazTerm");
	gtk_window_set_default_size(GTK_WINDOW(tw->win), 900, 600);
	g_signal_connect(tw->win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(tw->win, "window-state-event",
	    G_CALLBACK(on_window_state), tw);

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

	hooks.term_setup = term_setup;
	hooks.term_setup_data = tw;
	hooks.focus_changed = on_pane_focus;
	hooks.focus_data = tw;
	hooks.empty = on_split_empty;
	hooks.empty_data = tw;
	tw->split = tazterm_split_new(cfg, shell_override, workdir_override,
	    &hooks);
	gtk_box_pack_start(GTK_BOX(box), tw->split, TRUE, TRUE, 0);

	/* State attached to the window, freed on destroy. */
	g_object_set_data_full(G_OBJECT(tw->win), "tazterm-win", tw, win_free);

	debug_win = tw;
	g_unix_signal_add(SIGUSR1, on_sigusr1, NULL);

	return tw->win;
}
