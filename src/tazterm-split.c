/* tazterm-split.c — E3: arbre binaire de GtkPaned, feuilles = terminaux.
 *
 * Structure : racine GtkBox -> (GtkPaned -> (feuille | paned))*.
 * Feuille = GtkEventBox.classe(tazterm-pane[,-focused]) + VteTerminal.
 * Le collapse remplace un paned a enfant unique par son survivant.
 */
#include "tazterm-split.h"
#include "tazterm-term.h"

typedef struct {
	TaztermConfig *cfg; /* non possede (vie de l'appli) */
	char *shell_override;
	char *workdir_override;
	TaztermSplitHooks hooks;
	GtkWidget *root;
	VteTerminal *active;
} Split;

#define SPLIT(w) ((Split *) g_object_get_data(G_OBJECT(w), "tazterm-split"))

static void
split_free(gpointer data)
{
	Split *sp = data;

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
	const char *rules =
	    ".tazterm-pane { border: 2px solid transparent; }"
	    ".tazterm-pane-focused { border: 2px solid #4a90d9; }";

	if (done)
		return;
	done = TRUE;
	css = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css, rules, -1, NULL);
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

/* --- feuilles ----------------------------------------------------------- */

static gboolean
on_term_focus_in(GtkWidget *term, GdkEventFocus *event, gpointer data)
{
	GtkWidget *split = GTK_WIDGET(data);
	Split *sp = SPLIT(split);

	(void) event;

	if (sp->active != VTE_TERMINAL(term)) {
		if (sp->active)
			leaf_set_focused(
			    gtk_widget_get_parent(GTK_WIDGET(sp->active)),
			    FALSE);
		sp->active = VTE_TERMINAL(term);
		leaf_set_focused(gtk_widget_get_parent(term), TRUE);
		if (tazterm_debug())
			g_printerr("tazterm: focus pane %p\n",
			    (void *) sp->active);
		if (sp->hooks.focus_changed)
			sp->hooks.focus_changed(sp->active,
			    sp->hooks.focus_data);
	}
	return FALSE;
}

/* La feuille d'un terminal = son parent direct (EventBox marquee). */
static GtkWidget *
leaf_of(VteTerminal *term)
{
	GtkWidget *leaf;

	leaf = gtk_widget_get_parent(GTK_WIDGET(term));
	if (!leaf || !GTK_IS_EVENT_BOX(leaf))
		return NULL;
	if (!g_object_get_data(G_OBJECT(term), "tazterm-in-tree"))
		return NULL;
	return leaf;
}

static GtkWidget *
leaf_new(Split *sp, GtkWidget *split, const char *command)
{
	GtkWidget *leaf;
	VteTerminal *term;

	leaf = gtk_event_box_new();
	gtk_style_context_add_class(
	    gtk_widget_get_style_context(leaf), "tazterm-pane");
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(leaf), FALSE);

	term = tazterm_term_new_cmd(sp->cfg, sp->shell_override,
	    sp->workdir_override, command);
	g_object_set_data(G_OBJECT(term), "tazterm-in-tree",
	    GINT_TO_POINTER(TRUE));
	g_signal_connect(term, "focus-in-event",
	    G_CALLBACK(on_term_focus_in), split);
	if (sp->hooks.term_setup)
		sp->hooks.term_setup(term, sp->hooks.term_setup_data);

	gtk_container_add(GTK_CONTAINER(leaf), GTK_WIDGET(term));
	return leaf;
}

/* --- parcours ------------------------------------------------------------- */

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

/* Premier terminal du sous-arbre (pour le focus apres collapse). */
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

/* --- API ---------------------------------------------------------------------- */

GtkWidget *
tazterm_split_new(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override,
    const TaztermSplitHooks *hooks)
{
	Split *sp;
	GtkWidget *leaf;

	css_ensure();

	sp = g_new0(Split, 1);
	sp->cfg = cfg;
	sp->shell_override = g_strdup(shell_override);
	sp->workdir_override = g_strdup(workdir_override);
	if (hooks)
		sp->hooks = *hooks;

	sp->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	g_object_set_data_full(G_OBJECT(sp->root), "tazterm-split", sp,
	    split_free);

	leaf = leaf_new(sp, sp->root, NULL);
	gtk_box_pack_start(GTK_BOX(sp->root), leaf, TRUE, TRUE, 0);

	sp->active = VTE_TERMINAL(gtk_bin_get_child(GTK_BIN(leaf)));
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
    const char *command)
{
	Split *sp = SPLIT(split);
	GtkWidget *leaf;
	GtkWidget *parent;
	GtkWidget *paned;
	GtkWidget *newleaf;
	int pos = 0;

	if (!sp->active)
		return;
	leaf = leaf_of(sp->active);
	if (!leaf)
		return;
	parent = gtk_widget_get_parent(leaf);

	paned = gtk_paned_new(orientation);
	newleaf = leaf_new(sp, split, command);

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

	/* Equilibre a moitie des que l'allocation est connue. */
	g_idle_add(balance_idle, g_object_ref(paned));

	sp->active = VTE_TERMINAL(gtk_bin_get_child(GTK_BIN(newleaf)));
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
	/* Panneaux cote a cote = paned horizontal. */
	split_current(split, GTK_ORIENTATION_HORIZONTAL, NULL);
}

void
tazterm_split_horizontal(GtkWidget *split)
{
	/* Panneaux empiles = paned vertical. */
	split_current(split, GTK_ORIENTATION_VERTICAL, NULL);
}

void
tazterm_split_vertical_cmd(GtkWidget *split, const char *command)
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
	parent = gtk_widget_get_parent(leaf);

	g_object_set_data(G_OBJECT(term), "tazterm-in-tree",
	    GINT_TO_POINTER(FALSE));

	if (parent == split) {
		/* Dernier panneau : destroy() se desparente seul, ne pas
		 * container_remove() avant (double dispose). */
		gtk_widget_destroy(leaf);
		sp->active = NULL;
		if (tazterm_debug())
			g_printerr("tazterm: close last pane\n");
		if (sp->hooks.empty)
			sp->hooks.empty(sp->hooks.empty_data);
		return;
	}

	if (!GTK_IS_PANED(parent)) {
		/* Ne devrait pas arriver (racine ou paned uniquement). */
		gtk_widget_destroy(leaf);
		return;
	}

	if (gtk_paned_get_child1(GTK_PANED(parent)) == leaf)
		sibling = gtk_paned_get_child2(GTK_PANED(parent));
	else
		sibling = gtk_paned_get_child1(GTK_PANED(parent));

	if (!sibling) {
		/* Invariant brise (paned a enfant unique) : on nettoie. */
		g_warning("tazterm: paned with single child");
		gtk_widget_destroy(leaf);
		gtk_widget_destroy(parent);
		focus = first_term(split);
	} else {
		int slot;
		GtkWidget *gp;

		focus = first_term(sibling);
		/* Capture la place AVANT de detruire : destroy() se
		 * desparente seul, remove() + destroy() = double dispose. */
		gp = gtk_widget_get_parent(parent);
		if (GTK_IS_PANED(gp) &&
		    gtk_paned_get_child1(GTK_PANED(gp)) == parent)
			slot = 1;
		else
			slot = 2;

		g_object_ref(sibling);
		gtk_container_remove(GTK_CONTAINER(parent), sibling);
		gtk_widget_destroy(leaf);   /* tue le shell */
		gtk_widget_destroy(parent); /* vide, se desparente seul */

		/* Remonte le survivant a la place du paned. */
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
			sp->active = focus;
		leaf_set_focused(gtk_widget_get_parent(
		    GTK_WIDGET(sp->active)), TRUE);
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
	toplevel = gtk_widget_get_toplevel(GTK_WIDGET(sp->active));
	arr = g_ptr_array_new();
	collect_terms(split, arr);
	if (arr->len < 2) {
		g_ptr_array_free(arr, TRUE);
		return;
	}

	gtk_widget_translate_coordinates(GTK_WIDGET(sp->active), toplevel,
	    0, 0, &ax, &ay);
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
		gtk_widget_translate_coordinates(t, toplevel, 0, 0, &bx,
		    &by);
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

	if (best)
		gtk_widget_grab_focus(best); /* focus-in met a jour active */
}
