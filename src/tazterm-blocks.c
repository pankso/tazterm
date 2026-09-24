/* tazterm-blocks.c — Command blocks from bash prompt marks.
 *
 * VTE 0.56 does not parse OSC 133, but it reports OSC 6 (current file
 * URI) through "current-file-uri-changed". The bash rcfile emits:
 *   PS0 (before a command):  OSC 6 file://localhost/tazterm/start/TOKEN/N
 *   end of PS1 (prompt):     OSC 6 file://localhost/tazterm/mark/TOKEN/N/EXIT
 * VTE signals once per processed chunk, cursor at the chunk end: the
 * mark is the last thing of the prompt, so the cursor sits where the
 * command will be typed. Mark k-1 -> mark k = block k: command on the
 * prompt row, output below, exit code carried by mark k.
 * Two marks in one chunk collapse into the last one (VTE): a burst of
 * instant commands may merge, never corrupt.
 */
#include "tazterm-blocks.h"
#include "tazterm-term.h"

#include <stdlib.h>
#include <string.h>

#define RCFILE_VERSION "tazterm bash integration v1"
#define URI_PREFIX "file://localhost/tazterm/"
#define MAX_MARKS 2000

static const char rcfile_sh[] =
"# " RCFILE_VERSION " (managed by tazterm, rewritten on upgrade)\n"
"# Loaded as `bash --rcfile`: sources ~/.bashrc, then marks prompts\n"
"# (OSC 6: command blocks) and reports the cwd (OSC 7) to tazterm.\n"
"# Personal settings belong in ~/.bashrc, not here.\n"
"[ -r \"$HOME/.bashrc\" ] && . \"$HOME/.bashrc\"\n"
"if [ -n \"$TAZTERM_MARK_TOKEN\" ] && [ -z \"$__tazterm_hooked\" ]; then\n"
"\t__tazterm_hooked=1\n"
"\t__tazterm_n=0\n"
"\t__tazterm_ec=0\n"
"\t__tazterm_pre() {\n"
"\t\t__tazterm_ec=$?\n"
"\t\t__tazterm_n=$((__tazterm_n + 1))\n"
"\t}\n"
"\t__tazterm_post() {\n"
"\t\tif [ \"$PWD\" != \"$__tazterm_pwd\" ]; then\n"
"\t\t\t__tazterm_pwd=$PWD\n"
"\t\t\t[ -x \"$TAZTERM_BIN\" ] && \"$TAZTERM_BIN\" --osc7\n"
"\t\tfi\n"
"\t\t# Re-append when a prompt theme rebuilt PS1.\n"
"\t\tcase \"$PS1\" in\n"
"\t\t*tazterm/mark/*) ;;\n"
"\t\t*) PS1=\"$PS1\"'\\[\\e]6;file://localhost/tazterm/mark/"
"${TAZTERM_MARK_TOKEN}/${__tazterm_n}/${__tazterm_ec}\\a\\]' ;;\n"
"\t\tesac\n"
"\t}\n"
"\tPS0='\\e]6;file://localhost/tazterm/start/"
"${TAZTERM_MARK_TOKEN}/${__tazterm_n}\\a'\"$PS0\"\n"
"\tPROMPT_COMMAND=\"__tazterm_pre${PROMPT_COMMAND:+; "
"${PROMPT_COMMAND%;}}; __tazterm_post\"\n"
"fi\n";

char *
tazterm_blocks_rcfile(void)
{
	char *path;
	char *old = NULL;

	path = g_build_filename(g_get_user_config_dir(), "tazterm",
	    "bash-integration.sh", NULL);
	if (!g_file_get_contents(path, &old, NULL, NULL) ||
	    !strstr(old, RCFILE_VERSION)) {
		char *dir = g_path_get_dirname(path);

		g_mkdir_with_parents(dir, 0755);
		g_free(dir);
		/* NOFOLLOW: never redirect through a planted symlink. */
		if (!tazterm_write_file_nofollow(path, rcfile_sh, -1, 0644))
			g_warning("tazterm: cannot write %s", path);
	}
	g_free(old);
	return path;
}

/* --- marks -------------------------------------------------------------- */

typedef struct {
	glong row;       /* prompt end: where the command gets typed */
	glong col;
	int exit;        /* exit status of the command before this prompt */
	int seconds;     /* its run time, -1 unknown */
	gint64 time;     /* monotonic us */
} Mark;

typedef struct {
	char *token;
	GArray *marks;   /* oldest first */
	int dropped;     /* marks dropped from the front (numbering) */
	gint64 start;    /* last PS0 start mark, 0 = none */
} Blocks;

static void (*listener)(VteTerminal *, gpointer);
static gpointer listener_data;

void
tazterm_blocks_set_listener(void (*fn)(VteTerminal *term, gpointer data),
    gpointer data)
{
	listener = fn;
	listener_data = data;
}

static void
blocks_free(gpointer data)
{
	Blocks *b = data;

	g_free(b->token);
	g_array_unref(b->marks);
	g_free(b);
}

static Blocks *
blocks_of(VteTerminal *term)
{
	return g_object_get_data(G_OBJECT(term), "tazterm-blocks");
}

static void
on_file_uri(VteTerminal *term, gpointer data)
{
	Blocks *b = data;
	const char *uri;
	char **part;
	Mark m;
	gint64 ec;

	uri = vte_terminal_get_current_file_uri(term);
	if (!uri || !g_str_has_prefix(uri, URI_PREFIX))
		return;
	part = g_strsplit(uri + strlen(URI_PREFIX), "/", 5);
	/* Wrong or missing token: printed by some program, not our
	 * prompt. Ignore. */
	if (!part[0] || !part[1] || strcmp(part[1], b->token) != 0)
		goto out;
	if (!strcmp(part[0], "start")) {
		b->start = g_get_monotonic_time();
		goto out;
	}
	if (strcmp(part[0], "mark") != 0 || !part[2] || !part[3] ||
	    !g_ascii_string_to_signed(part[3], 10, 0, 255, &ec, NULL))
		goto out;

	vte_terminal_get_cursor_position(term, &m.col, &m.row);
	m.exit = (int) ec;
	m.time = g_get_monotonic_time();
	m.seconds = -1;
	if (b->marks->len > 0) {
		Mark *prev = &g_array_index(b->marks, Mark, b->marks->len - 1);

		if (b->start > prev->time)
			m.seconds = (int) ((m.time - b->start +
			    G_USEC_PER_SEC / 2) / G_USEC_PER_SEC);
	}
	b->start = 0;
	/* clear/reset moved rows back: older marks are meaningless. */
	while (b->marks->len > 0 &&
	    g_array_index(b->marks, Mark, b->marks->len - 1).row >= m.row) {
		g_array_remove_index(b->marks, b->marks->len - 1);
		b->dropped++;
	}
	g_array_append_val(b->marks, m);
	if (b->marks->len > MAX_MARKS) {
		g_array_remove_index(b->marks, 0);
		b->dropped++;
	}
	if (tazterm_debug())
		g_printerr("tazterm: mark pane %d row %ld exit %d\n",
		    tazterm_term_get_id(term), m.row, m.exit);
	if (listener && b->marks->len >= 2) {
		TaztermBlock *last = tazterm_blocks_last(term);

		/* Enter on an empty line is no command. */
		if (last && *last->command)
			listener(term, listener_data);
		tazterm_block_free(last);
	}
out:
	g_strfreev(part);
}

void
tazterm_blocks_attach(VteTerminal *term, const char *token)
{
	Blocks *b;

	b = g_new0(Blocks, 1);
	b->token = g_strdup(token);
	b->marks = g_array_new(FALSE, FALSE, sizeof(Mark));
	g_object_set_data_full(G_OBJECT(term), "tazterm-blocks", b,
	    blocks_free);
	g_signal_connect(term, "current-file-uri-changed",
	    G_CALLBACK(on_file_uri), b);
}

gboolean
tazterm_blocks_enabled(VteTerminal *term)
{
	Blocks *b = blocks_of(term);

	return b && b->marks->len > 0;
}

/* --- blocks -------------------------------------------------------------- */

static char *
rows_text(VteTerminal *term, glong r0, glong c0, glong r1)
{
	char *t;

	t = vte_terminal_get_text_range(term, r0, c0, r1,
	    vte_terminal_get_column_count(term) - 1, NULL, NULL, NULL);
	return t ? g_strchomp(t) : g_strdup("");
}

/* Block k (1 <= k < marks->len): from mark k-1 to mark k. */
static TaztermBlock *
block_at(VteTerminal *term, Blocks *b, guint k)
{
	Mark *prev = &g_array_index(b->marks, Mark, k - 1);
	Mark *cur = &g_array_index(b->marks, Mark, k);
	GtkAdjustment *va;
	TaztermBlock *blk;
	glong lower, out0, out1;

	va = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(term));
	lower = (glong) gtk_adjustment_get_lower(va);
	blk = g_new0(TaztermBlock, 1);
	blk->number = b->dropped + (int) k;
	blk->exit = cur->exit;
	blk->seconds = cur->seconds;
	blk->command = prev->row >= lower ?
	    g_strstrip(rows_text(term, prev->row, prev->col, prev->row)) :
	    g_strdup("(scrolled out)");
	out0 = MAX(prev->row + 1, lower);
	out1 = cur->row - 1;
	blk->output = out1 >= out0 ? rows_text(term, out0, 0, out1) :
	    g_strdup("");
	return blk;
}

void
tazterm_block_free(TaztermBlock *blk)
{
	if (!blk)
		return;
	g_free(blk->command);
	g_free(blk->output);
	g_free(blk);
}

GPtrArray *
tazterm_blocks_list(VteTerminal *term, int max)
{
	Blocks *b = blocks_of(term);
	GPtrArray *arr;
	guint k;

	arr = g_ptr_array_new_with_free_func(
	    (GDestroyNotify) tazterm_block_free);
	if (!b)
		return arr;
	for (k = b->marks->len; k-- > 1 && (int) arr->len < max; ) {
		TaztermBlock *blk = block_at(term, b, k);

		if (*blk->command)
			g_ptr_array_insert(arr, 0, blk);
		else
			tazterm_block_free(blk);
	}
	return arr;
}

TaztermBlock *
tazterm_blocks_last(VteTerminal *term)
{
	GPtrArray *arr;
	TaztermBlock *blk = NULL;

	arr = tazterm_blocks_list(term, 1);
	if (arr->len > 0) {
		/* GLib 2.56: no g_ptr_array_steal_index(). The free func
		 * accepts NULL. */
		blk = g_ptr_array_index(arr, 0);
		arr->pdata[0] = NULL;
	}
	g_ptr_array_unref(arr);
	return blk;
}

char *
tazterm_block_format(const TaztermBlock *blk)
{
	GString *gs;

	gs = g_string_new(NULL);
	g_string_append_printf(gs, "$ %s\n", blk->command);
	if (*blk->output)
		g_string_append_printf(gs, "%s\n", blk->output);
	g_string_append_printf(gs, "[exit %d", blk->exit);
	if (blk->seconds >= 0)
		g_string_append_printf(gs, ", %ds", blk->seconds);
	g_string_append(gs, "]\n");
	return g_string_free(gs, FALSE);
}

int
tazterm_blocks_last_exit(VteTerminal *term)
{
	Blocks *b = blocks_of(term);

	/* An empty Enter keeps bash's $?: the last mark is right. */
	if (!b || b->marks->len < 2)
		return -1;
	return g_array_index(b->marks, Mark, b->marks->len - 1).exit;
}

int
tazterm_blocks_running(VteTerminal *term)
{
	Blocks *b = blocks_of(term);
	Mark *last;

	if (!b || !b->start || b->marks->len == 0)
		return -1;
	last = &g_array_index(b->marks, Mark, b->marks->len - 1);
	if (b->start <= last->time)
		return -1;
	return (int) ((g_get_monotonic_time() - b->start) / G_USEC_PER_SEC);
}

gboolean
tazterm_blocks_prompt_row(VteTerminal *term, glong row, int dir, glong *out)
{
	Blocks *b = blocks_of(term);
	guint i;

	if (!b)
		return FALSE;
	if (dir < 0) {
		for (i = b->marks->len; i-- > 0; ) {
			glong r = g_array_index(b->marks, Mark, i).row;

			if (r < row) {
				*out = r;
				return TRUE;
			}
		}
	} else {
		for (i = 0; i < b->marks->len; i++) {
			glong r = g_array_index(b->marks, Mark, i).row;

			if (r > row) {
				*out = r;
				return TRUE;
			}
		}
	}
	return FALSE;
}
