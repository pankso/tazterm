/* tazterm-term.h — VteTerminal wrapper driven by TaztermConfig. */
#ifndef TAZTERM_TERM_H
#define TAZTERM_TERM_H

#include <gtk/gtk.h>
#include <vte/vte.h>

#include "tazterm-config.h"

G_BEGIN_DECLS

/* TRUE when TAZTERM_DEBUG=1 (stderr logs, lxtaz pattern). */
gboolean tazterm_debug(void);

/* Create a configured VteTerminal and spawn the shell.
 * Shell precedence: shell_override > TAZTERM_SHELL > cfg->shell.
 * Directory precedence: workdir_override > cfg->workdir > $HOME.
 * With command != NULL: spawn command instead of a shell (agent pane).
 * No space -> direct argv; with space (tests) -> shell -c "command". */
VteTerminal *tazterm_term_new(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override);
VteTerminal *tazterm_term_new_cmd(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override,
    const char *command);

/* Zoom: VTE font scale, clamped [0.5 .. 3.0]. */
void tazterm_term_zoom_in(VteTerminal *term);
void tazterm_term_zoom_out(VteTerminal *term);
void tazterm_term_zoom_reset(VteTerminal *term);

/* Search: set the (plain text) pattern and jump to the next match.
 * Returns TRUE when found. */
gboolean tazterm_term_search(VteTerminal *term, const char *text);
gboolean tazterm_term_search_next(VteTerminal *term);
gboolean tazterm_term_search_prev(VteTerminal *term);

/* Currently visible text (for output capture). Caller frees (g_free). */
char *tazterm_term_get_visible_text(VteTerminal *term);

/* Active shell's cwd (OSC 7, then /proc). Caller frees, NULL if unknown. */
char *tazterm_term_get_cwd(VteTerminal *term);

G_END_DECLS

#endif /* TAZTERM_TERM_H */
