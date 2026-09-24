/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
/* tazterm-ai.h — Pragmatic AI agent integration.
 *
 * Agents (opencode, claude, navette) are ordinary CLI/TUI programs:
 * TazTerm detects them on PATH, opens a dedicated split that spawns
 * the launch command, and hands VTE scrollback (raw pty text) to the
 * agent pane or the clipboard. Nothing is written to disk.
 * No agent-side change needed.
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

/* Last n lines of output, scrollback included (raw text, trailing
 * blanks trimmed). n <= 0: everything. Free it (g_free). */
char *tazterm_ai_last_lines(VteTerminal *term, int n);

/* Mask obvious secrets (private keys, API tokens, password=...) with
 * [REDACTED]. count (may be NULL) gets the number masked.
 * Returns a new string (g_free). */
char *tazterm_ai_redact(const char *text, guint *count);

/* "Explain" prompt (markdown) around the last n lines of term, framed
 * as untrusted data for the agent, secrets masked when redact.
 * Free it (g_free). */
char *tazterm_ai_explain_prompt(VteTerminal *term, int nlines,
    gboolean redact);

G_END_DECLS

#endif /* TAZTERM_AI_H */
