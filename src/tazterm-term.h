/* tazterm-term.h — VteTerminal wrapper driven by TaztermConfig. */
#ifndef TAZTERM_TERM_H
#define TAZTERM_TERM_H

#include <gtk/gtk.h>
#include <vte/vte.h>

#include <sys/stat.h>

#include "tazterm-config.h"

G_BEGIN_DECLS

#define TAZTERM_VERSION "0.5"

/* TRUE when TAZTERM_DEBUG=1 (stderr logs, lxtaz pattern). */
gboolean tazterm_debug(void);

/* Create a configured VteTerminal and spawn the shell.
 * Shell precedence: shell_override > TAZTERM_SHELL > cfg->shell.
 * Directory precedence: workdir_override > cfg->workdir > $HOME.
 * With command (argv) != NULL: spawn it instead of a shell (agent
 * pane, -e). */
VteTerminal *tazterm_term_new(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override);
VteTerminal *tazterm_term_new_cmd(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override,
    char **command);

/* argv for a command line: parsed and spawned directly, or through
 * "cfg->shell -c" when it uses shell syntax (; | & $ ...).
 * NULL when empty/unparsable. Free with g_strfreev. */
char **tazterm_command_argv(TaztermConfig *cfg, const char *command);

/* Search: set the (plain text) pattern and jump to the next match.
 * Returns TRUE when found. */
gboolean tazterm_term_search(VteTerminal *term, const char *text);
gboolean tazterm_term_search_next(VteTerminal *term);
gboolean tazterm_term_search_prev(VteTerminal *term);

/* Visible rows only (debug dump). Caller frees (g_free). */
char *tazterm_term_get_visible_text(VteTerminal *term);

/* Last n rows of output, scrollback included, up to the cursor row
 * (n <= 0: whole scrollback). Caller frees (g_free). */
char *tazterm_term_get_text_tail(VteTerminal *term, int n);

/* Clean text headed for a pty: keep tab/newline, drop other C0, DEL
 * and C1 controls, repair invalid UTF-8, CR/CRLF -> LF.
 * NULL when empty or too large. Caller frees (g_free). */
char *tazterm_text_sanitize(const char *in);

/* Paste text, the current content of selection sel (CLIPBOARD or
 * PRIMARY), into term: sanitized, then VTE's own paste (bracketed only
 * when the app asked for it, never an Enter added).
 * FALSE if nothing to paste. */
gboolean tazterm_term_paste_text(VteTerminal *term, GdkAtom sel,
    const char *text);

/* Keyboard/menu paste from CLIPBOARD: async read, sanitized, and a
 * confirmation dialog before a multi-line paste into a shell without
 * bracketed paste (busybox ash), where each line would run. */
void tazterm_term_paste_clipboard(VteTerminal *term);

/* TRUE when a vte_terminal_match_check_event() tag is the URL regex
 * (else it is file:line[:col]). */
gboolean tazterm_term_match_is_url(VteTerminal *term, int tag);

/* Pane id (1, 2, ... never reused), exported as TAZTERM_PANE. */
int tazterm_term_get_id(VteTerminal *term);

/* Name of the pane's foreground process (/proc comm), NULL if
 * unknown. Caller frees (g_free). */
char *tazterm_term_get_process(VteTerminal *term);

/* Closing term would kill something: a program in the foreground of
 * its shell, or a command/agent pane still alive. what (may be NULL)
 * gets the process name (g_free). */
gboolean tazterm_term_is_busy(VteTerminal *term, char **what);

/* Control socket exported as TAZTERM_SOCKET to panes spawned from now
 * on (NULL: none, and an inherited one is removed). */
void tazterm_term_set_ctl_socket(const char *path);

/* Pane cwd: /proc, then OSC 7, then the directory it started in.
 * Caller frees, NULL if unknown. */
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
