/* tazterm-ai.h — Pragmatic AI agent integration.
 *
 * Agents (opencode, claude, navette) are ordinary CLI/TUI programs:
 * TazTerm detects them on PATH, opens a dedicated split that spawns
 * the launch command, and captures VTE scrollback (raw pty text) for
 * the agent via clipboard + /tmp file. No agent-side change needed.
 */
#ifndef TAZTERM_AI_H
#define TAZTERM_AI_H

#include <gtk/gtk.h>
#include <vte/vte.h>

G_BEGIN_DECLS

/* Known agents, auto-mode preference order. */
extern const char *const tazterm_ai_known_agents[];

/* Detect agents present on PATH. Returns the found names
 * (free with g_strfreev) and the default per cfg_agent
 * ("auto" = first found, else the requested name when present). */
char **tazterm_ai_detect(const char *cfg_agent, char **def_out);

/* Agent launch command in a shell (overridable for tests via
 * TAZTERM_AGENT_CMD). Returns a static string. */
const char *tazterm_ai_launch_cmd(const char *agent);

/* Last n lines of scrollback (raw text). Free it (g_free).
 * n <= 0: everything. */
char *tazterm_ai_last_lines(VteTerminal *term, int n);

/* TRUE when the text looks like an error (for "explain"). */
gboolean tazterm_ai_looks_like_error(const char *text);

/* Copy text -> clipboard + /tmp/tazterm-<prefix>-<pid>.log file.
 * Returns the file path (free it), or NULL on failure. */
char *tazterm_ai_save_capture(GtkWidget *win, const char *text,
    const char *prefix);

/* Build the "explain this error" prompt (markdown) from the last
 * n lines, save it (prefix "explain") + clipboard.
 * Returns the path (free it), or NULL with no text. */
char *tazterm_ai_explain(GtkWidget *win, VteTerminal *term, int nlines);

G_END_DECLS

#endif /* TAZTERM_AI_H */
