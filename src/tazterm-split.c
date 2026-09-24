/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
/* tazterm-split.c — Binary GtkPaned tree, leaves = terminals.
 *
 * Layout: root GtkBox -> (GtkPaned -> (leaf | paned))*.
 * Leaf = GtkEventBox.class(tazterm-pane[,-focused]) -> GtkBox ->
 *        VteTerminal + optional status bar (see the window).
 * Collapse replaces a single-child paned with its survivor.
 */
#include "tazterm-split.h"
#include "tazterm-term.h"
#include "tazterm-ai.h"

typedef struct {
	TaztermConfig *cfg; /* not owned (app lifetime) */
	char *shell_override;
	char *workdir_override;
	TaztermSplitHooks hooks;
	GtkWidget *root;
	VteTerminal *active;
	VteTerminal *prev;  /* previously active pane, weak pointer */
	gdouble font_scale; /* window-wide zoom, inherited by new panes */
	GPtrArray *hidden;  /* pane zoom: widgets hidden, NULL when off */
} Split;

#define SPLIT(w) ((Split *) g_object_get_data(G_OBJECT(w), "tazterm-split"))

static void unzoom(Split *sp);

static void
split_free(gpointer data)
{
	Split *sp = data;

	if (sp->prev)
		g_object_remove_weak_pointer(G_OBJECT(sp->prev),
		    (gpointer *) &sp->prev);
	if (sp->hidden)
		g_ptr_array_free(sp->hidden, TRUE);
	g_free(sp->shell_override);
	g_free(sp->workdir_override);
	g_free(sp);
}

/* --- CSS focus -------------------------------------------------------- */

static void
css_ensure(void)
{
	static gboolean done = FALSE;
	GtkCssProvider *css;

	if (done)
		return;
	done = TRUE;
	css = gtk_css_provider_new();
	/* outline (not border): no layout impact, so focusing a pane
	 * never resizes its VTE (no SIGWINCH storm for ncurses apps). */
	gtk_css_provider_load_from_data(css,
	    ".tazterm-pane { outline: 2px solid transparent; outline-offset: -2px; }"
	    ".tazterm-pane-focused { outline-color: #4a90d9; }"
	    ".tazterm-pane-attention { outline-color: #f57900; }"
	    ".tazterm-status { background-color: #111317; padding: 1px 6px; }"
	    ".tazterm-status label { color: #8a919c; font-size: 8pt; }",
	    -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
	    GTK_STYLE_PROVIDER(css),
	    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);
}

static void
leaf_set_focused(GtkWidget *leaf, gboolean focused)
{
	GtkStyleContext *ctx;

	ctx = gtk_widget_get_style_context(leaf);
	if (focused)
		gtk_style_context_add_class(ctx, "tazterm-pane-focused");
	else
		gtk_style_context_remove_class(ctx, "tazterm-pane-focused");
}

static GtkWidget *term_leaf(GtkWidget *term);

/* Change the active pane, remembering the old one ("tazterm ctl read"
 * from an agent pane targets the pane the user came from). */
static void
active_set(Split *sp, VteTerminal *term)
{
	if (sp->active == term)
		return;
	if (sp->prev)
		g_object_remove_weak_pointer(G_OBJECT(sp->prev),
		    (gpointer *) &sp->prev);
	sp->prev = sp->active;
	if (sp->prev)
		g_object_add_weak_pointer(G_OBJECT(sp->prev),
		    (gpointer *) &sp->prev);
	sp->active = term;
}

/* --- leaves ------------------------------------------------------------ */

static gboolean
on_term_focus_in(GtkWidget *term, GdkEventFocus *event, gpointer data)
{
	GtkWidget *split = GTK_WIDGET(data);
	Split *sp = SPLIT(split);
	static int focus_seq;

	(void) event;

	/* Order of visits: "send to agent" picks the last agent used. */
	g_object_set_data(G_OBJECT(term), "tazterm-focus-seq",
	    GINT_TO_POINTER(++focus_seq));
	if (sp->active != VTE_TERMINAL(term)) {
		if (sp->active)
			leaf_set_focused(
			    term_leaf(GTK_WIDGET(sp->active)),
			    FALSE);
		active_set(sp, VTE_TERMINAL(term));
		leaf_set_focused(term_leaf(term), TRUE);
		gtk_style_context_remove_class(gtk_widget_get_style_context(
		    term_leaf(term)), "tazterm-pane-attention");
		if (tazterm_debug())
			g_printerr("tazterm: focus pane %p\n",
			    (void *) sp->active);
		if (sp->hooks.focus_changed)
			sp->hooks.focus_changed(sp->active,
			    sp->hooks.focus_data);
	}
	return FALSE;
}

/* A terminal's leaf: the EventBox above its box. */
static GtkWidget *
term_leaf(GtkWidget *term)
{
	GtkWidget *box;
	GtkWidget *leaf;

	box = gtk_widget_get_parent(term);
	leaf = box ? gtk_widget_get_parent(box) : NULL;
	return leaf && GTK_IS_EVENT_BOX(leaf) ? leaf : NULL;
}

static VteTerminal *
leaf_term(GtkWidget *leaf)
{
	return g_object_get_data(G_OBJECT(leaf), "tazterm-term");
}

/* Leaf of a terminal still in the tree (NULL once removed). */
static GtkWidget *
leaf_of(VteTerminal *term)
{
	if (!g_object_get_data(G_OBJECT(term), "tazterm-in-tree"))
		return NULL;
	return term_leaf(GTK_WIDGET(term));
}

static gboolean
on_status_click(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	Split *sp = SPLIT(data);
	VteTerminal *term;

	if (event->type != GDK_BUTTON_PRESS || event->button != 1)
		return FALSE;
	term = g_object_get_data(G_OBJECT(widget), "tazterm-term");
	if (term && sp->hooks.status_clicked)
		sp->hooks.status_clicked(term, sp->hooks.term_setup_data);
	return TRUE;
}

/* One-line bar under the pane; the window fills the two labels
 * ("tazterm-status-left" / "-right" on the terminal). The right one
 * is clickable (status_clicked hook). */
static GtkWidget *
status_bar_new(GtkWidget *split, VteTerminal *term)
{
	GtkWidget *bar;
	GtkWidget *left;
	GtkWidget *right;
	GtkWidget *click;

	bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_style_context_add_class(gtk_widget_get_style_context(bar),
	    "tazterm-status");
	left = gtk_label_new(NULL);
	right = gtk_label_new(NULL);
	/* Never let a long cwd widen the pane. */
	gtk_label_set_ellipsize(GTK_LABEL(left), PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_width_chars(GTK_LABEL(left), 1);
	gtk_label_set_xalign(GTK_LABEL(left), 0.0);
	click = gtk_event_box_new();
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(click), FALSE);
	gtk_container_add(GTK_CONTAINER(click), right);
	g_object_set_data(G_OBJECT(click), "tazterm-term", term);
	g_signal_connect(click, "button-press-event",
	    G_CALLBACK(on_status_click), split);
	gtk_box_pack_start(GTK_BOX(bar), left, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(bar), click, FALSE, FALSE, 0);
	g_object_set_data(G_OBJECT(term), "tazterm-status-left", left);
	g_object_set_data(G_OBJECT(term), "tazterm-status-right", right);
	return bar;
}

static GtkWidget *
leaf_new(Split *sp, GtkWidget *split, char **command,
    const char *workdir)
{
	GtkWidget *leaf;
	GtkWidget *box;
	VteTerminal *term;

	leaf = gtk_event_box_new();
	gtk_style_context_add_class(
	    gtk_widget_get_style_context(leaf), "tazterm-pane");
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(leaf), FALSE);

	/* workdir != NULL: inherit the active pane (split).
	 * NULL: overrides > config > $HOME fallback (initial pane). */
	term = tazterm_term_new_cmd(sp->cfg, sp->shell_override,
	    workdir ? workdir : sp->workdir_override, command);
	vte_terminal_set_font_scale(term, sp->font_scale);
	g_object_set_data(G_OBJECT(term), "tazterm-in-tree",
	    GINT_TO_POINTER(TRUE));
	g_signal_connect(term, "focus-in-event",
	    G_CALLBACK(on_term_focus_in), split);
	if (sp->hooks.term_setup)
		sp->hooks.term_setup(term, sp->hooks.term_setup_data);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(term), TRUE, TRUE, 0);
	if (sp->cfg->status_bar)
		gtk_box_pack_end(GTK_BOX(box), status_bar_new(split, term), FALSE,
		    FALSE, 0);
	gtk_container_add(GTK_CONTAINER(leaf), box);
	g_object_set_data(G_OBJECT(leaf), "tazterm-term", term);
	return leaf;
}

/* --- walk --------------------------------------------------------------- */

static void
collect_terms(GtkWidget *w, GPtrArray *out)
{
	GList *children, *l;

	if (VTE_IS_TERMINAL(w)) {
		g_ptr_array_add(out, w);
		return;
	}
	if (!GTK_IS_CONTAINER(w))
		return;
	children = gtk_container_get_children(GTK_CONTAINER(w));
	for (l = children; l; l = l->next)
		collect_terms(GTK_WIDGET(l->data), out);
	g_list_free(children);
}

/* First terminal of the subtree (focus after collapse). */
static VteTerminal *
first_term(GtkWidget *w)
{
	GPtrArray *arr;
	VteTerminal *term = NULL;

	arr = g_ptr_array_new();
	collect_terms(w, arr);
	if (arr->len > 0)
		term = VTE_TERMINAL(g_ptr_array_index(arr, 0));
	g_ptr_array_free(arr, TRUE);
	return term;
}

/* --- equilibrage ------------------------------------------------------------ */

static gboolean
balance_idle(gpointer data)
{
	GtkWidget *paned = GTK_WIDGET(data);
	GtkAllocation alloc;
	int pos;

	gtk_widget_get_allocation(paned, &alloc);
	if (gtk_orientable_get_orientation(GTK_ORIENTABLE(paned)) ==
	    GTK_ORIENTATION_HORIZONTAL)
		pos = alloc.width / 2;
	else
		pos = alloc.height / 2;
	if (pos > 0)
		gtk_paned_set_position(GTK_PANED(paned), pos);
	g_object_unref(paned);
	return FALSE;
}

/* --- zoom (window-wide, inherited by new panes) ---------------------------- */

#define TAZTERM_ZOOM_STEP 0.1
#define TAZTERM_ZOOM_MIN 0.5
#define TAZTERM_ZOOM_MAX 3.0

static void
zoom_apply(GtkWidget *split, gdouble scale)
{
	Split *sp = SPLIT(split);
	GPtrArray *arr;
	guint i;

	if (scale < TAZTERM_ZOOM_MIN)
		scale = TAZTERM_ZOOM_MIN;
	if (scale > TAZTERM_ZOOM_MAX)
		scale = TAZTERM_ZOOM_MAX;
	sp->font_scale = scale;

	arr = g_ptr_array_new();
	collect_terms(split, arr);
	for (i = 0; i < arr->len; i++)
		vte_terminal_set_font_scale(
		    VTE_TERMINAL(g_ptr_array_index(arr, i)), scale);
	if (tazterm_debug())
		g_printerr("tazterm: zoom scale=%.2f (%u panes)\n", scale,
		    arr->len);
	g_ptr_array_free(arr, TRUE);
}

void
tazterm_split_zoom_in(GtkWidget *split)
{
	Split *sp = SPLIT(split);

	zoom_apply(split, sp->font_scale + TAZTERM_ZOOM_STEP);
}

void
tazterm_split_zoom_out(GtkWidget *split)
{
	Split *sp = SPLIT(split);

	zoom_apply(split, sp->font_scale - TAZTERM_ZOOM_STEP);
}

void
tazterm_split_zoom_reset(GtkWidget *split)
{
	zoom_apply(split, 1.0);
}

/* --- API ---------------------------------------------------------------------- */

GtkWidget *
tazterm_split_new(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override,
    char **command, const TaztermSplitHooks *hooks)
{
	Split *sp;
	GtkWidget *leaf;

	css_ensure();

	sp = g_new0(Split, 1);
	sp->cfg = cfg;
	sp->font_scale = 1.0;
	sp->shell_override = g_strdup(shell_override);
	sp->workdir_override = g_strdup(workdir_override);
	if (hooks)
		sp->hooks = *hooks;

	sp->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	g_object_set_data_full(G_OBJECT(sp->root), "tazterm-split", sp,
	    split_free);

	leaf = leaf_new(sp, sp->root, command, NULL);
	gtk_box_pack_start(GTK_BOX(sp->root), leaf, TRUE, TRUE, 0);

	sp->active = leaf_term(leaf);
	leaf_set_focused(leaf, TRUE);

	if (tazterm_debug())
		g_printerr("tazterm: split root, 1 pane\n");
	return sp->root;
}

VteTerminal *
tazterm_split_active_term(GtkWidget *split)
{
	return SPLIT(split)->active;
}

VteTerminal *
tazterm_split_find_agent(GtkWidget *split)
{
	GPtrArray *arr;
	VteTerminal *found = NULL;
	Split *sp = SPLIT(split);
	int best = -1;
	guint i;

	arr = g_ptr_array_new();
	collect_terms(split, arr);
	for (i = 0; i < arr->len; i++) {
		VteTerminal *t = VTE_TERMINAL(g_ptr_array_index(arr, i));
		int seen = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(t),
		    "tazterm-focus-seq"));

		if (seen > best && tazterm_ai_pane_is_agent(t,
		    sp->cfg->ai_agent)) {
			found = t;
			best = seen;
		}
	}
	g_ptr_array_free(arr, TRUE);
	return found;
}

GPtrArray *
tazterm_split_list(GtkWidget *split)
{
	GPtrArray *arr;

	arr = g_ptr_array_new();
	collect_terms(split, arr);
	return arr;
}

VteTerminal *
tazterm_split_previous_term(GtkWidget *split)
{
	return SPLIT(split)->prev;
}

void
tazterm_split_attention(GtkWidget *split, VteTerminal *term)
{
	GtkWidget *leaf;

	/* The user is looking at the active pane already. */
	if (!term || term == SPLIT(split)->active)
		return;
	leaf = leaf_of(term);
	if (!leaf)
		return;
	gtk_style_context_add_class(gtk_widget_get_style_context(leaf),
	    "tazterm-pane-attention");
	/* Hidden by a pane zoom: the visible pane says so instead. */
	if (SPLIT(split)->hidden && !gtk_widget_is_visible(leaf) &&
	    leaf_of(SPLIT(split)->active))
		gtk_style_context_add_class(gtk_widget_get_style_context(
		    leaf_of(SPLIT(split)->active)), "tazterm-pane-attention");
	if (tazterm_debug())
		g_printerr("tazterm: attention pane %d\n",
		    tazterm_term_get_id(term));
}

int
tazterm_split_count(GtkWidget *split)
{
	GPtrArray *arr;
	int n;

	arr = g_ptr_array_new();
	collect_terms(split, arr);
	n = (int) arr->len;
	g_ptr_array_free(arr, TRUE);
	return n;
}

static void
split_current(GtkWidget *split, GtkOrientation orientation,
    char **command)
{
	Split *sp = SPLIT(split);
	GtkWidget *leaf;
	GtkWidget *parent;
	GtkWidget *paned;
	GtkWidget *newleaf;
	int pos = 0;

	if (!sp->active)
		return;
	unzoom(sp);
	leaf = leaf_of(sp->active);
	if (!leaf)
		return;
	parent = gtk_widget_get_parent(leaf);

	paned = gtk_paned_new(orientation);
	/* The new pane inherits the active pane's cwd
	 * (fallback: overrides > config > $HOME when unknown). */
	{
		char *cwd = tazterm_term_get_cwd(sp->active);

		newleaf = leaf_new(sp, split, command, cwd);
		if (tazterm_debug())
			g_printerr("tazterm: split pane cwd='%s'\n",
			    cwd ? cwd : "(default)");
		g_free(cwd);
	}

	g_object_ref(leaf);
	if (GTK_IS_BOX(parent)) {
		pos = 0;
		gtk_container_remove(GTK_CONTAINER(parent), leaf);
		gtk_box_pack_start(GTK_BOX(parent), paned, TRUE, TRUE, 0);
		gtk_box_reorder_child(GTK_BOX(parent), paned, pos);
	} else if (GTK_IS_PANED(parent)) {
		if (gtk_paned_get_child1(GTK_PANED(parent)) == leaf)
			pos = 1;
		else
			pos = 2;
		gtk_container_remove(GTK_CONTAINER(parent), leaf);
		if (pos == 1)
			gtk_paned_pack1(GTK_PANED(parent), paned, TRUE,
			    FALSE);
		else
			gtk_paned_pack2(GTK_PANED(parent), paned, TRUE,
			    FALSE);
	} else {
		g_object_unref(leaf);
		gtk_widget_destroy(newleaf);
		return;
	}

	gtk_paned_pack1(GTK_PANED(paned), leaf, TRUE, FALSE);
	g_object_unref(leaf);
	gtk_paned_pack2(GTK_PANED(paned), newleaf, TRUE, FALSE);
	gtk_widget_show_all(paned);

	/* Balance at half once the allocation is known. */
	g_idle_add(balance_idle, g_object_ref(paned));

	active_set(sp, leaf_term(newleaf));
	leaf_set_focused(leaf, FALSE);
	leaf_set_focused(newleaf, TRUE);
	gtk_widget_grab_focus(GTK_WIDGET(sp->active));
	if (sp->hooks.focus_changed)
		sp->hooks.focus_changed(sp->active, sp->hooks.focus_data);

	if (tazterm_debug())
		g_printerr("tazterm: split %s, %d panes\n",
		    orientation == GTK_ORIENTATION_HORIZONTAL ? "vertical" :
		    "horizontal",
		    tazterm_split_count(split));
}

void
tazterm_split_vertical(GtkWidget *split)
{
	/* Side-by-side panes = horizontal paned. */
	split_current(split, GTK_ORIENTATION_HORIZONTAL, NULL);
}

void
tazterm_split_horizontal(GtkWidget *split)
{
	/* Stacked panes = vertical paned. */
	split_current(split, GTK_ORIENTATION_VERTICAL, NULL);
}

void
tazterm_split_vertical_cmd(GtkWidget *split, char **command)
{
	split_current(split, GTK_ORIENTATION_HORIZONTAL, command);
}

void
tazterm_split_remove_term(GtkWidget *split, VteTerminal *term)
{
	Split *sp = SPLIT(split);
	GtkWidget *leaf;
	GtkWidget *parent;
	GtkWidget *sibling;
	VteTerminal *focus = NULL;

	leaf = term ? leaf_of(term) : NULL;
	if (!leaf)
		return;
	unzoom(sp);
	parent = gtk_widget_get_parent(leaf);

	g_object_set_data(G_OBJECT(term), "tazterm-in-tree",
	    GINT_TO_POINTER(FALSE));

	if (parent == split) {
		/* Last pane: destroy() unparents itself, never
		 * container_remove() first (double dispose). */
		gtk_widget_destroy(leaf);
		sp->active = NULL;
		if (tazterm_debug())
			g_printerr("tazterm: close last pane\n");
		if (sp->hooks.empty)
			sp->hooks.empty(sp->hooks.empty_data);
		return;
	}

	if (!GTK_IS_PANED(parent)) {
		/* Should not happen (root or paned only). */
		gtk_widget_destroy(leaf);
		return;
	}

	if (gtk_paned_get_child1(GTK_PANED(parent)) == leaf)
		sibling = gtk_paned_get_child2(GTK_PANED(parent));
	else
		sibling = gtk_paned_get_child1(GTK_PANED(parent));

	if (!sibling) {
		/* Broken invariant (single-child paned): clean up. */
		g_warning("tazterm: paned with single child");
		gtk_widget_destroy(leaf);
		gtk_widget_destroy(parent);
		focus = first_term(split);
	} else {
		int slot;
		GtkWidget *gp;

		focus = first_term(sibling);
		/* Capture the slot BEFORE destroying: destroy() unparents
		 * itself, remove() + destroy() = double dispose. */
		gp = gtk_widget_get_parent(parent);
		if (GTK_IS_PANED(gp) &&
		    gtk_paned_get_child1(GTK_PANED(gp)) == parent)
			slot = 1;
		else
			slot = 2;

		g_object_ref(sibling);
		gtk_container_remove(GTK_CONTAINER(parent), sibling);
		gtk_widget_destroy(leaf);   /* kills the shell */
		gtk_widget_destroy(parent); /* empty, unparents itself */

		/* Lift the survivor where the paned was. */
		if (gp == split) {
			gtk_box_pack_start(GTK_BOX(gp), sibling, TRUE,
			    TRUE, 0);
		} else if (GTK_IS_PANED(gp)) {
			if (slot == 1)
				gtk_paned_pack1(GTK_PANED(gp), sibling,
				    TRUE, FALSE);
			else
				gtk_paned_pack2(GTK_PANED(gp), sibling,
				    TRUE, FALSE);
		} else {
			g_object_unref(sibling);
			return;
		}
		g_object_unref(sibling);
	}

	if (focus) {
		if (sp->active == term)
			active_set(sp, focus);
		leaf_set_focused(term_leaf(GTK_WIDGET(sp->active)), TRUE);
		gtk_widget_grab_focus(GTK_WIDGET(sp->active));
		if (sp->hooks.focus_changed)
			sp->hooks.focus_changed(sp->active,
			    sp->hooks.focus_data);
	}
	if (tazterm_debug())
		g_printerr("tazterm: close pane, %d left\n",
		    tazterm_split_count(split));
}

void
tazterm_split_close_current(GtkWidget *split)
{
	tazterm_split_remove_term(split, SPLIT(split)->active);
}

void
tazterm_split_focus_dir(GtkWidget *split, TaztermDirection dir)
{
	Split *sp = SPLIT(split);
	GPtrArray *arr;
	GtkWidget *toplevel;
	GtkWidget *best = NULL;
	int ax, ay, bx, by;
	guint aw, ah;
	long best_dist = 0;
	guint i;

	if (!sp->active)
		return;
	unzoom(sp);
	toplevel = gtk_widget_get_toplevel(GTK_WIDGET(sp->active));
	arr = g_ptr_array_new();
	collect_terms(split, arr);
	if (arr->len < 2) {
		g_ptr_array_free(arr, TRUE);
		return;
	}

	if (!gtk_widget_translate_coordinates(GTK_WIDGET(sp->active),
	    toplevel, 0, 0, &ax, &ay)) {
		g_ptr_array_free(arr, TRUE);
		return;
	}
	aw = (guint) gtk_widget_get_allocated_width(
	    GTK_WIDGET(sp->active));
	ah = (guint) gtk_widget_get_allocated_height(
	    GTK_WIDGET(sp->active));
	ax += (int) (aw / 2);
	ay += (int) (ah / 2);

	for (i = 0; i < arr->len; i++) {
		GtkWidget *t = GTK_WIDGET(g_ptr_array_index(arr, i));
		int dx, dy;
		long dist;

		if (t == GTK_WIDGET(sp->active))
			continue;
		/* Unrealized widgets have no coordinates: skip instead
		 * of reading uninitialized values. */
		bx = ax;
		by = ay;
		if (!gtk_widget_translate_coordinates(t, toplevel, 0, 0,
		    &bx, &by))
			continue;
		bx += gtk_widget_get_allocated_width(t) / 2;
		by += gtk_widget_get_allocated_height(t) / 2;
		dx = bx - ax;
		dy = by - ay;

		switch (dir) {
		case TAZTERM_LEFT:
			if (dx > -10)
				continue;
			break;
		case TAZTERM_RIGHT:
			if (dx < 10)
				continue;
			break;
		case TAZTERM_UP:
			if (dy > -10)
				continue;
			break;
		case TAZTERM_DOWN:
			if (dy < 10)
				continue;
			break;
		}
		dist = (long) dx * dx + (long) dy * dy;
		if (!best || dist < best_dist) {
			best = t;
			best_dist = dist;
		}
	}
	g_ptr_array_free(arr, TRUE);

	if (best) {
		gtk_widget_grab_focus(best);
		/* focus-in updates the active pane, but only when the window
		 * has the input focus: do it here too (idempotent). */
		on_term_focus_in(best, NULL, split);
	}
}

/* --- pane zoom, resize, equalize ------------------------------------------ */

/* Zoom: every sibling on the way from the active leaf up to the root
 * is hidden, so the leaf fills the window. No reparenting: the other
 * panes keep running untouched, only unmapped. */
static void
unzoom(Split *sp)
{
	guint i;

	if (!sp->hidden)
		return;
	for (i = 0; i < sp->hidden->len; i++)
		gtk_widget_show(g_ptr_array_index(sp->hidden, i));
	g_ptr_array_free(sp->hidden, TRUE);
	sp->hidden = NULL;
	if (sp->active && leaf_of(sp->active))
		gtk_style_context_remove_class(gtk_widget_get_style_context(
		    leaf_of(sp->active)), "tazterm-pane-attention");
	if (tazterm_debug())
		g_printerr("tazterm: pane zoom off\n");
}

void
tazterm_split_zoom_pane(GtkWidget *split)
{
	Split *sp = SPLIT(split);
	GtkWidget *w, *parent;

	if (sp->hidden) {
		unzoom(sp);
		return;
	}
	w = sp->active ? leaf_of(sp->active) : NULL;
	if (!w)
		return;
	sp->hidden = g_ptr_array_new_with_free_func(g_object_unref);
	for (; (parent = gtk_widget_get_parent(w)) && GTK_IS_PANED(parent);
	    w = parent) {
		GtkWidget *other = gtk_paned_get_child1(GTK_PANED(parent));

		if (other == w)
			other = gtk_paned_get_child2(GTK_PANED(parent));
		if (other) {
			gtk_widget_hide(other);
			g_ptr_array_add(sp->hidden, g_object_ref(other));
		}
	}
	if (!sp->hidden->len) {
		/* Single pane: nothing to zoom. */
		g_ptr_array_free(sp->hidden, TRUE);
		sp->hidden = NULL;
		return;
	}
	if (tazterm_debug())
		g_printerr("tazterm: pane zoom on (%u hidden)\n",
		    sp->hidden->len);
}

gboolean
tazterm_split_is_zoomed(GtkWidget *split)
{
	return SPLIT(split)->hidden != NULL;
}

/* Move the border of the active pane in dir: the closest divider on
 * that axis, 5% of its paned per step. */
void
tazterm_split_resize(GtkWidget *split, TaztermDirection dir)
{
	Split *sp = SPLIT(split);
	GtkOrientation axis;
	GtkWidget *w, *parent;
	int size, pos;

	axis = dir == TAZTERM_LEFT || dir == TAZTERM_RIGHT ?
	    GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
	unzoom(sp);
	w = sp->active ? leaf_of(sp->active) : NULL;
	for (; w && (parent = gtk_widget_get_parent(w)) &&
	    GTK_IS_PANED(parent); w = parent) {
		if (gtk_orientable_get_orientation(GTK_ORIENTABLE(parent)) !=
		    axis)
			continue;
		size = axis == GTK_ORIENTATION_HORIZONTAL ?
		    gtk_widget_get_allocated_width(parent) :
		    gtk_widget_get_allocated_height(parent);
		pos = gtk_paned_get_position(GTK_PANED(parent));
		pos += (dir == TAZTERM_LEFT || dir == TAZTERM_UP ? -1 : 1) *
		    MAX(size / 20, 1);
		gtk_paned_set_position(GTK_PANED(parent),
		    CLAMP(pos, size / 10, size - size / 10));
		return;
	}
}

/* Panes along one axis under w: A | (B | C) counts 3 side by side. */
static int
axis_count(GtkWidget *w, GtkOrientation axis)
{
	if (!GTK_IS_PANED(w) ||
	    gtk_orientable_get_orientation(GTK_ORIENTABLE(w)) != axis)
		return 1;
	return axis_count(gtk_paned_get_child1(GTK_PANED(w)), axis) +
	    axis_count(gtk_paned_get_child2(GTK_PANED(w)), axis);
}

static void
equalize(GtkWidget *w)
{
	GtkOrientation axis;
	int size, n1, n2;

	if (!GTK_IS_PANED(w))
		return;
	axis = gtk_orientable_get_orientation(GTK_ORIENTABLE(w));
	size = axis == GTK_ORIENTATION_HORIZONTAL ?
	    gtk_widget_get_allocated_width(w) :
	    gtk_widget_get_allocated_height(w);
	n1 = axis_count(gtk_paned_get_child1(GTK_PANED(w)), axis);
	n2 = axis_count(gtk_paned_get_child2(GTK_PANED(w)), axis);
	if (size > 0)
		gtk_paned_set_position(GTK_PANED(w), size * n1 / (n1 + n2));
	equalize(gtk_paned_get_child1(GTK_PANED(w)));
	equalize(gtk_paned_get_child2(GTK_PANED(w)));
}

/* Same size for every pane on each axis (nested splits included). */
void
tazterm_split_equalize(GtkWidget *split)
{
	GList *children, *l;

	unzoom(SPLIT(split));
	children = gtk_container_get_children(GTK_CONTAINER(split));
	for (l = children; l; l = l->next)
		equalize(GTK_WIDGET(l->data));
	g_list_free(children);
}
