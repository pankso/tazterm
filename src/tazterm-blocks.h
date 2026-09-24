/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
/* tazterm-blocks.h — Command blocks: command, output, exit code.
 *
 * bash panes load a tazterm rcfile that ends PS1 with an OSC 6 mark
 * (and PS0 with a start mark). Each mark gives the prompt position and
 * the previous command's exit code, so the scrollback splits into
 * blocks. ash has no prompt hook: no blocks there.
 */
#ifndef TAZTERM_BLOCKS_H
#define TAZTERM_BLOCKS_H

#include <gtk/gtk.h>
#include <vte/vte.h>

G_BEGIN_DECLS

typedef struct {
	int number;       /* 1 = first command of the pane */
	char *command;    /* first line of the command */
	char *output;     /* rows between the command and the next prompt */
	int exit;         /* exit status */
	int seconds;      /* run time, -1 unknown */
} TaztermBlock;

/* rcfile for `bash --rcfile` (written/upgraded on demand). g_free. */
char *tazterm_blocks_rcfile(void);

/* Track marks on a bash pane; token is exported to the shell as
 * TAZTERM_MARK_TOKEN (a mark without it is ignored: plain output
 * cannot forge blocks). */
void tazterm_blocks_attach(VteTerminal *term, const char *token);

/* TRUE once the pane's shell reported a prompt mark. */
gboolean tazterm_blocks_enabled(VteTerminal *term);

/* Last completed block, NULL when none. tazterm_block_free(). */
TaztermBlock *tazterm_blocks_last(VteTerminal *term);

/* Up to max last completed blocks, oldest first (g_ptr_array_unref;
 * the array frees its blocks). */
GPtrArray *tazterm_blocks_list(VteTerminal *term, int max);

void tazterm_block_free(TaztermBlock *b);

/* Block as text for an agent or a human:
 *   $ command
 *   output
 *   [exit N, 3s] */
char *tazterm_block_format(const TaztermBlock *b);

/* Exit code of the last command, -1 when none (cheap: no text). */
int tazterm_blocks_last_exit(VteTerminal *term);

/* Run time of the last command in seconds, -1 unknown (cheap). */
int tazterm_blocks_last_seconds(VteTerminal *term);

/* Seconds the current command has been running, -1 at the prompt. */
int tazterm_blocks_running(VteTerminal *term);

/* Prompt row before (dir < 0) or after (dir > 0) row, for prompt
 * jumps. FALSE when there is none. */
gboolean tazterm_blocks_prompt_row(VteTerminal *term, glong row, int dir,
    glong *out);

/* Called on every completed block (ctl wait, long-command alert). */
void tazterm_blocks_add_listener(void (*fn)(VteTerminal *term,
    gpointer data), gpointer data);

G_END_DECLS

#endif /* TAZTERM_BLOCKS_H */
