/* tazterm-term.h — VteTerminal wrapper driven by TaztermConfig. */
#ifndef TAZTERM_TERM_H
#define TAZTERM_TERM_H

#include <gtk/gtk.h>
#include <vte/vte.h>

#include <sys/stat.h>

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

/* Currently selected text, or NULL when there is no selection.
 * Reads PRIMARY (does not touch CLIPBOARD). Caller frees (g_free). */
char *tazterm_term_get_selected_text(VteTerminal *term);

/* Feed text as one paste: strip C0 (keep tab/newline), wrap in
 * bracketed-paste, one trailing newline. FALSE if empty or too large. */
gboolean tazterm_term_feed_paste(VteTerminal *term, const char *text);

/* Manual paste from CLIPBOARD through the same sanitizer, but without
 * the trailing newline (paste never executes by itself). */
gboolean tazterm_term_paste_clipboard(VteTerminal *term);

/* Active shell's cwd (/proc then OSC 7). Caller frees, NULL if unknown. */
char *tazterm_term_get_cwd(VteTerminal *term);

/* Write data to path without following a symlink at path (O_NOFOLLOW,
 * ELOOP when path is a symlink: refuse). len < 0 means strlen(data). */
gboolean tazterm_write_file_nofollow(const char *path, const char *data,
    gssize len, mode_t mode);

/* Write data to path as mode 0600 (temp+rename, does not follow a
 * symlink at path). len < 0 means strlen(data). */
gboolean tazterm_write_private(const char *path, const char *data,
    gssize len);

/* Emit OSC 7 for getcwd() to stdout (percent-encoded path).
 * Used as `tazterm --osc7` from the shell integration; no GTK. */
gboolean tazterm_term_osc7_emit(void);

G_END_DECLS

#endif /* TAZTERM_TERM_H */
