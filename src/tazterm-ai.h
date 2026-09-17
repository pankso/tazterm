/* tazterm-ai.h — E4: integration pragmatique des agents IA.
 *
 * Les agents (opencode, claude, navette) sont des programmes CLI/TUI
 * ordinaires : TazTerm les detecte au PATH, ouvre un split dedie qui
 * leur "tape" la commande de lancement, et capture le scrollback VTE
 * (texte brut du pty) pour l'envoyer vers l'agent via presse-papier +
 * fichier /tmp. Aucune modification requise cote agent.
 */
#ifndef TAZTERM_AI_H
#define TAZTERM_AI_H

#include <gtk/gtk.h>
#include <vte/vte.h>

G_BEGIN_DECLS

/* Agents connus, par ordre de preference en mode auto. */
extern const char *const tazterm_ai_known_agents[];

/* Detecte les agents presents au PATH. Retourne la liste des noms
 * trouves (a liberer : g_strfreev) et le defaut selon cfg_agent
 * ("auto" = premier trouve, sinon le nom demande s'il existe). */
char **tazterm_ai_detect(const char *cfg_agent, char **def_out);

/* Commande de lancement de l'agent dans un shell (surchargable pour les
 * tests via TAZTERM_AGENT_CMD). Retourne une chaine statique. */
const char *tazterm_ai_launch_cmd(const char *agent);

/* N dernieres lignes du scrollback (texte brut). A liberer (g_free).
 * n <= 0 : tout. */
char *tazterm_ai_last_lines(VteTerminal *term, int n);

/* TRUE si le texte ressemble a une erreur (pour "expliquer"). */
gboolean tazterm_ai_looks_like_error(const char *text);

/* Copie texte -> presse-papier + fichier /tmp/tazterm-<prefix>-<pid>.log.
 * Retourne le chemin du fichier (a liberer), ou NULL en echec. */
char *tazterm_ai_save_capture(GtkWidget *win, const char *text,
    const char *prefix);

/* Construit le prompt "explique cette erreur" (markdown) a partir des
 * n dernieres lignes, le sauvegarde (prefix "explain") + presse-papier.
 * Retourne le chemin (a liberer), ou NULL si pas de texte. */
char *tazterm_ai_explain(GtkWidget *win, VteTerminal *term, int nlines);

G_END_DECLS

#endif /* TAZTERM_AI_H */
