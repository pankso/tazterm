/* tazterm-split.h — E3: conteneur a panneaux (arbre GtkPaned recursif).
 *
 * Chaque feuille = un VteTerminal avec son propre shell. Le split possede
 * l'etat (panneau actif) et la structure ; la fenetre branche ses signaux
 * via les hooks (setup par terminal, focus, vide).
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
	/* Appele pour chaque terminal cree (initial + splits) : la fenetre
	 * y branche key-press, clic-droit, titre, child-exited. */
	void (*term_setup)(VteTerminal *term, gpointer data);
	gpointer term_setup_data;
	/* Appele quand le panneau actif change (titre, recherche). */
	void (*focus_changed)(VteTerminal *term, gpointer data);
	gpointer focus_data;
	/* Appele quand il ne reste aucun panneau (fermer la fenetre). */
	void (*empty)(gpointer data);
	gpointer empty_data;
} TaztermSplitHooks;

/* Nouveau conteneur avec un premier terminal. */
GtkWidget *tazterm_split_new(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override,
    const TaztermSplitHooks *hooks);

/* Terminal du panneau actif (jamais NULL tant qu'il reste un panneau). */
VteTerminal *tazterm_split_active_term(GtkWidget *split);

/* Nombre de panneaux (debug / tests). */
int tazterm_split_count(GtkWidget *split);

/* Divise le panneau actif : cote a cote / empiles. */
void tazterm_split_vertical(GtkWidget *split);
void tazterm_split_horizontal(GtkWidget *split);

/* Comme vertical, mais le nouveau panneau spawne command (agent)
 * au lieu d'un shell. */
void tazterm_split_vertical_cmd(GtkWidget *split, const char *command);

/* Ferme le panneau contenant term (retire de l'arbre, collapse le parent).
 * Sans danger si term n'est plus dans l'arbre. Declenche empty() si vide. */
void tazterm_split_remove_term(GtkWidget *split, VteTerminal *term);

/* Ferme le panneau actif. */
void tazterm_split_close_current(GtkWidget *split);

/* Deplace le focus au panneau voisin dans la direction donnee. */
void tazterm_split_focus_dir(GtkWidget *split, TaztermDirection dir);

G_END_DECLS

#endif /* TAZTERM_SPLIT_H */
