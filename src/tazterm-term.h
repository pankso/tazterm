/* tazterm-term.h — E2: wrapper VteTerminal pilote par TaztermConfig. */
#ifndef TAZTERM_TERM_H
#define TAZTERM_TERM_H

#include <gtk/gtk.h>
#include <vte/vte.h>

#include "tazterm-config.h"

G_BEGIN_DECLS

/* Cree un VteTerminal configure et y spawne le shell.
 * Precedence shell : shell_override > TAZTERM_SHELL > cfg->shell.
 * Precedence dossier : workdir_override > cfg->workdir > $HOME.
 * Si command != NULL : spawne command a la place du shell (panneau
 * agent). Sans espace -> argv direct ; avec espace (tests) ->
 * shell -c "command". */
VteTerminal *tazterm_term_new(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override);
VteTerminal *tazterm_term_new_cmd(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override,
    const char *command);

/* TRUE si TAZTERM_DEBUG=1 (logs stderr, motif lxtaz). */
gboolean tazterm_debug(void);

/* Zoom : echelle de fonte VTE, bornee [0.5 .. 3.0]. */
void tazterm_term_zoom_in(VteTerminal *term);
void tazterm_term_zoom_out(VteTerminal *term);
void tazterm_term_zoom_reset(VteTerminal *term);

/* Recherche : definit le motif (texte brut) et saute a l'occurrence
 * suivante. Retourne TRUE si trouve. */
gboolean tazterm_term_search(VteTerminal *term, const char *text);
gboolean tazterm_term_search_next(VteTerminal *term);
gboolean tazterm_term_search_prev(VteTerminal *term);

/* Texte actuellement visible (pour E4 : capture output). L'appelant libere
 * avec g_free(). */
char *tazterm_term_get_visible_text(VteTerminal *term);

G_END_DECLS

#endif /* TAZTERM_TERM_H */
