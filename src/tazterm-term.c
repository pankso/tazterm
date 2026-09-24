/* tazterm-term.c — VteTerminal wrapper: spawn, search, capture, paste. */
#include "tazterm-term.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

gboolean
tazterm_debug(void)
{
	static int cached = -1;

	if (cached < 0)
		cached = g_getenv("TAZTERM_DEBUG") ? 1 : 0;
	return cached == 1;
}

/* Write data to path without following a symlink at the final
 * component (O_NOFOLLOW): a pre-planted symlink cannot redirect the
 * write into another file. temp+rename is NOT used here so the target
 * keeps its inode (config file); ELOOP means "symlink, refuse". */
gboolean
tazterm_write_file_nofollow(const char *path, const char *data,
    gssize len, mode_t mode)
{
	int fd;
	const char *p;
	gsize left;

	if (!path || !data)
		return FALSE;
	if (len < 0)
		len = (gssize) strlen(data);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW,
	    mode);
	if (fd < 0)
		return FALSE;
	p = data;
	left = (gsize) len;
	while (left > 0) {
		gssize n = write(fd, p, left);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			return FALSE;
		}
		if (n == 0) {
			close(fd);
			return FALSE;
		}
		p += n;
		left -= (gsize) n;
	}
	if (close(fd) < 0)
		return FALSE;
	return TRUE;
}

gboolean
tazterm_write_private(const char *path, const char *data, gssize len)
{
	char *tmpl;
	int fd;
	const char *p;
	gsize left;

	if (!path || !data)
		return FALSE;
	if (len < 0)
		len = (gssize) strlen(data);

	/* mkstemp 0600 + rename: does not follow a symlink at path. */
	tmpl = g_strdup_printf("%s.XXXXXX", path);
	fd = g_mkstemp(tmpl);
	if (fd < 0) {
		g_free(tmpl);
		return FALSE;
	}
	(void) fchmod(fd, 0600);
	p = data;
	left = (gsize) len;
	while (left > 0) {
		gssize n = write(fd, p, left);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			unlink(tmpl);
			g_free(tmpl);
			return FALSE;
		}
		if (n == 0) {
			close(fd);
			unlink(tmpl);
			g_free(tmpl);
			return FALSE;
		}
		p += n;
		left -= (gsize) n;
	}
	if (close(fd) < 0) {
		unlink(tmpl);
		g_free(tmpl);
		return FALSE;
	}
	if (rename(tmpl, path) != 0) {
		unlink(tmpl);
		g_free(tmpl);
		return FALSE;
	}
	g_free(tmpl);
	return TRUE;
}

/* OSC 7 path: unreserved (RFC 3986) plus '/'. Everything else, including
 * ESC and backslash, becomes %XX so a crafted directory name cannot
 * terminate the OSC sequence. */
static void
osc7_append_path(GString *gs, const char *path)
{
	const unsigned char *p;

	for (p = (const unsigned char *) path; *p; p++) {
		unsigned char c = *p;

		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') ||
		    c == '-' || c == '_' || c == '.' || c == '~' || c == '/')
			g_string_append_c(gs, (char) c);
		else
			g_string_append_printf(gs, "%%%02X", c);
	}
}

gboolean
tazterm_term_osc7_emit(void)
{
	char cwd[4096];
	GString *gs;
	gsize n, len;

	if (!getcwd(cwd, sizeof cwd))
		return FALSE;
	gs = g_string_sized_new(strlen(cwd) + 32);
	g_string_append(gs, "\033]7;file://localhost");
	osc7_append_path(gs, cwd);
	g_string_append(gs, "\033\\");
	len = gs->len;
	n = fwrite(gs->str, 1, len, stdout);
	g_string_free(gs, TRUE);
	if (n != len)
		return FALSE;
	return fflush(stdout) == 0;
}

/* Shell integration: the shell reports its cwd via OSC 7, which VTE
 * exposes through vte_terminal_get_current_directory_uri().
 * /etc/profile.d/vte.sh only covers login bash/zsh; here we target ash
 * (the SliTaz default) via $ENV, and bash via PROMPT_COMMAND when unset.
 * File owned by tazterm: rewritten when the version marker differs.
 * Users may append custom code BELOW the marker line.
 *
 * Encoding is done in C (`tazterm --osc7`): a bashism in PWD would
 * break ash, and a raw PWD inside OSC 7 lets a directory name inject
 * terminal sequences. cd() returns the cd status, not the encoder's.
 * The guard is NOT exported: an exported guard leaked into every child
 * and disabled the hook in a tazterm started from a pane (v3 bug).
 */
#define INTEGRATION_VERSION "tazterm shell integration v4"
#define INTEGRATION_MARKER "# TAZTERM-CUSTOM-BELOW\n"

static const char integration_sh[] =
"# " INTEGRATION_VERSION " (managed by tazterm, do not edit above)\n"
"# ash/dash: sourced via $ENV. Reports cwd with OSC 7 for split panes.\n"
"# bash is covered by PROMPT_COMMAND (see tazterm) or /etc/profile.d/vte.sh.\n"
"# Custom code may go BELOW the marker line (kept on upgrade).\n"
"if [ -z \"$__tazterm_hooked\" ] && [ -n \"$PS1\" ]; then\n"
"    __tazterm_hooked=1\n"
"    __tazterm_osc7() {\n"
"        if [ -n \"$TAZTERM_BIN\" ] && [ -x \"$TAZTERM_BIN\" ]; then\n"
"            \"$TAZTERM_BIN\" --osc7\n"
"        fi\n"
"    }\n"
"    cd() {\n"
"        command cd \"$@\" || return\n"
"        __tazterm_osc7\n"
"        return 0\n"
"    }\n"
"    __tazterm_osc7\n"
"fi\n"
INTEGRATION_MARKER;

static char *
integration_path(void)
{
	return g_build_filename(g_get_user_config_dir(), "tazterm",
	    "shell-integration.sh", NULL);
}

static void
integration_ensure(void)
{
	char *path;
	char *old = NULL;
	const char *custom = NULL;
	char *data;
	gboolean ok;

	path = integration_path();
	if (g_file_get_contents(path, &old, NULL, NULL)) {
		if (strstr(old, INTEGRATION_VERSION)) {
			g_free(old);
			g_free(path);
			return;
		}
		/* Upgrade: keep the user's code below the marker. */
		custom = strstr(old, INTEGRATION_MARKER);
		if (custom)
			custom += strlen(INTEGRATION_MARKER);
	}
	{
		char *dir = g_path_get_dirname(path);

		g_mkdir_with_parents(dir, 0755);
		g_free(dir);
	}
	data = g_strconcat(integration_sh, custom ? custom : "", NULL);
	g_free(old);
	/* NOFOLLOW write: never redirect through a planted symlink. */
	ok = tazterm_write_file_nofollow(path, data, -1, 0644);
	g_free(data);
	if (!ok) {
		if (tazterm_debug())
			g_printerr("tazterm: cannot write %s\n", path);
		g_free(path);
		return;
	}
	if (tazterm_debug())
		g_printerr("tazterm: wrote %s\n", path);
	g_free(path);
}

static void
on_spawn_ready(VteTerminal *term, GPid pid, GError *error, gpointer user_data)
{
	(void) user_data;

	if (!term)
		return;
	if (error) {
		g_warning("tazterm: spawn failed: %s", error->message);
		vte_terminal_feed(term,
		    "\r\n[tazterm] cannot start the shell.\r\n",
		    -1);
		return;
	}
	/* VTE 0.56 already watches spawn_async's child internally and emits
	 * "child-exited". Do NOT call vte_terminal_watch_child here: a
	 * second watch causes ECHILD + double emission and corrupts dispose
	 * (intermittent crash when closing a pane). */
	(void) pid;
	if (pid > 1)
		g_object_set_data(G_OBJECT(term), "tazterm-pid",
		    GINT_TO_POINTER(pid));
}

static void
env_set_bin(char ***envv)
{
	char *bin;

	bin = g_file_read_link("/proc/self/exe", NULL);
	if (!bin)
		bin = g_find_program_in_path("tazterm");
	if (bin) {
		*envv = g_environ_setenv(*envv, "TAZTERM_BIN", bin, TRUE);
		g_free(bin);
	}
}

/* The binary path is embedded inside double quotes in shell code:
 * reject anything but a boring path so a weird install location
 * cannot break out of the quoting. */
static gboolean
bin_is_shell_safe(const char *bin)
{
	const unsigned char *p;

	if (!bin || !*bin || *bin != '/')
		return FALSE;
	for (p = (const unsigned char *) bin; *p; p++) {
		unsigned char c = *p;

		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') ||
		    c == '/' || c == '-' || c == '_' || c == '.' ||
		    c == '+' || c == ':')
			continue;
		return FALSE;
	}
	return TRUE;
}

/* Hook OSC 7 reporting into shell panes so splits can inherit the
 * active pane's cwd. Respects existing user settings: never overrides
 * an already-set ENV or PROMPT_COMMAND. */
static void
env_integrate_shell(char ***envv, const char *shell)
{
	const char *base;
	const char *old;
	const char *bin;

	env_set_bin(envv);

	base = shell ? strrchr(shell, '/') : NULL;
	base = base ? base + 1 : shell;
	if (base && strstr(base, "bash")) {
		old = g_environ_getenv(*envv, "PROMPT_COMMAND");
		if (!old || !*old) {
			bin = g_environ_getenv(*envv, "TAZTERM_BIN");
			if (!bin_is_shell_safe(bin)) {
				if (tazterm_debug())
					g_printerr(
					    "tazterm: skip PROMPT_COMMAND"
					    " (unsafe bin path)\n");
				return;
			}
			*envv = g_environ_setenv(*envv, "PROMPT_COMMAND",
			    "[ -x \"$TAZTERM_BIN\" ] && "
			    "\"$TAZTERM_BIN\" --osc7",
			    TRUE);
		} else if (tazterm_debug()) {
			g_printerr("tazterm: keep user PROMPT_COMMAND\n");
		}
		return;
	}
	/* ash/dash/sh: sourced via $ENV. */
	old = g_environ_getenv(*envv, "ENV");
	if (!old || !*old) {
		char *path;

		integration_ensure();
		path = integration_path();
		*envv = g_environ_setenv(*envv, "ENV", path, TRUE);
		g_free(path);
	} else {
		char *path = integration_path();

		/* ENV inherited from a parent tazterm: still our file,
		 * keep it up to date. */
		if (!strcmp(old, path))
			integration_ensure();
		else if (tazterm_debug())
			g_printerr("tazterm: keep user ENV=%s\n", old);
		g_free(path);
	}
}

static int next_pane_id = 1;
static char *ctl_socket = NULL;

void
tazterm_term_set_ctl_socket(const char *path)
{
	g_free(ctl_socket);
	ctl_socket = g_strdup(path);
}

int
tazterm_term_get_id(VteTerminal *term)
{
	return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(term),
	    "tazterm-pane-id"));
}

/* Pane identity for programs inside it (agents calling tazterm ctl).
 * Inherited values from a parent tazterm are always replaced. */
static void
env_set_pane(char ***envv, VteTerminal *term)
{
	char *id;

	id = g_strdup_printf("%d", next_pane_id);
	g_object_set_data(G_OBJECT(term), "tazterm-pane-id",
	    GINT_TO_POINTER(next_pane_id));
	next_pane_id++;
	*envv = g_environ_setenv(*envv, "TAZTERM_PANE", id, TRUE);
	g_free(id);
	*envv = g_environ_setenv(*envv, "TERM_PROGRAM", "tazterm", TRUE);
	*envv = g_environ_setenv(*envv, "TERM_PROGRAM_VERSION",
	    TAZTERM_VERSION, TRUE);
	if (ctl_socket)
		*envv = g_environ_setenv(*envv, "TAZTERM_SOCKET", ctl_socket,
		    TRUE);
	else
		*envv = g_environ_unsetenv(*envv, "TAZTERM_SOCKET");
}

VteTerminal *
tazterm_term_new(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override)
{
	return tazterm_term_new_cmd(cfg, shell_override, workdir_override,
	    NULL);
}

char **
tazterm_command_argv(TaztermConfig *cfg, const char *command)
{
	char **argv = NULL;

	if (!command || !*command)
		return NULL;
	/* Shell syntax (tests, "make; notify"): through the configured
	 * shell. Plain "claude --continue": parsed, spawned directly. */
	if (strpbrk(command, ";|&<>$`(){}*?\n")) {
		argv = g_new0(char *, 4);
		argv[0] = g_strdup(cfg->shell);
		argv[1] = g_strdup("-c");
		argv[2] = g_strdup(command);
		return argv;
	}
	if (!g_shell_parse_argv(command, NULL, &argv, NULL))
		return NULL;
	return argv;
}

VteTerminal *
tazterm_term_new_cmd(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override,
    char **command)
{
	VteTerminal *term;
	PangoFontDescription *font;
	const char *shell;
	const char *workdir;
	char *shell_argv[2];
	char **argv;
	char **envv;

	term = VTE_TERMINAL(vte_terminal_new());
	vte_terminal_set_scrollback_lines(term, cfg->scrollback);
	vte_terminal_set_scroll_on_output(term, TRUE);
	vte_terminal_set_scroll_on_keystroke(term, TRUE);
	vte_terminal_set_mouse_autohide(term, TRUE);

	font = pango_font_description_from_string(cfg->font_desc);
	vte_terminal_set_font(term, font);
	pango_font_description_free(font);

	vte_terminal_set_colors(term,
	    cfg->fg_set ? &cfg->foreground : NULL,
	    cfg->bg_set ? &cfg->background : NULL,
	    NULL, 0);

	if (shell_override && *shell_override) {
		shell = shell_override;
	} else {
		shell = g_getenv("TAZTERM_SHELL");
		if (!shell || !*shell)
			shell = cfg->shell;
	}
	if (command && command[0]) {
		/* Command pane (agent, -e): spawned directly, no race with
		 * a shell reading typed-in keys. */
		argv = command;
	} else {
		shell_argv[0] = (char *) shell;
		shell_argv[1] = NULL;
		argv = shell_argv;
	}
	if (workdir_override && *workdir_override)
		workdir = workdir_override;
	else if (cfg->workdir)
		workdir = cfg->workdir;
	else
		workdir = g_get_home_dir();
	if (!g_file_test(workdir, G_FILE_TEST_IS_DIR)) {
		g_warning("tazterm: directory '%s' missing, falling back to $HOME",
		    workdir);
		workdir = g_get_home_dir();
	}

	if (tazterm_debug())
		g_printerr("tazterm: spawn argv0='%s'%s cwd='%s'\n", argv[0],
		    argv[1] ? " (+args)" : "", workdir);

	envv = g_get_environ();
	/* Exported by integration v3: drop it or a nested tazterm skips
	 * its own hook. */
	envv = g_environ_unsetenv(envv, "TAZTERM_OSC7");
	/* VTE 0.56 keeps the caller's TERM when envv is given: a session
	 * started from the console leaks TERM=linux (8 colors, 256-color
	 * TUIs fall back to plain yellow). Say what VTE really is. */
	envv = g_environ_setenv(envv, "TERM", "xterm-256color", TRUE);
	env_set_pane(&envv, term);
	if (argv == shell_argv) {
		env_integrate_shell(&envv, shell);
		/* Shell pane: remembered for the paste safety check. */
		g_object_set_data_full(G_OBJECT(term), "tazterm-shell",
		    g_strdup(shell), g_free);
	}

	vte_terminal_spawn_async(term,
	    VTE_PTY_DEFAULT,
	    workdir,
	    argv,
	    envv,
	    G_SPAWN_SEARCH_PATH,
	    NULL, NULL, NULL,
	    -1,
	    NULL,
	    on_spawn_ready,
	    NULL);
	g_strfreev(envv);

	return term;
}

#define TAZTERM_SEARCH_MAX 256

gboolean
tazterm_term_search(VteTerminal *term, const char *text)
{
	VteRegex *regex;
	GError *err = NULL;
	char *literal;

	if (!text || !*text)
		return FALSE;
	/* Plain-text search: escape regex metacharacters so the pattern
	 * is always literal (no ReDoS via crafted input) and cap length. */
	if (strlen(text) > TAZTERM_SEARCH_MAX)
		return FALSE;

	literal = g_regex_escape_string(text, -1);
	/* VTE requires PCRE2_MULTILINE for search, otherwise
	 * search_set_regex fails (VTE-side runtime check). */
	regex = vte_regex_new_for_search(literal, -1, PCRE2_MULTILINE,
	    &err);
	g_free(literal);
	if (!regex) {
		g_warning("tazterm: invalid regex: %s",
		    err ? err->message : "?");
		g_clear_error(&err);
		return FALSE;
	}
	vte_terminal_search_set_regex(term, regex, 0);
	vte_terminal_search_set_wrap_around(term, TRUE);
	vte_regex_unref(regex);
	if (tazterm_debug())
		g_printerr("tazterm: search\n");
	return vte_terminal_search_find_next(term);
}

gboolean
tazterm_term_search_next(VteTerminal *term)
{
	return vte_terminal_search_find_next(term);
}

gboolean
tazterm_term_search_prev(VteTerminal *term)
{
	return vte_terminal_search_find_previous(term);
}

char *
tazterm_term_get_visible_text(VteTerminal *term)
{
	/* Visible rows only (VTE 0.56 get_text): debug dump. For captures
	 * use tazterm_term_get_text_tail(), which reads the scrollback. */
	return vte_terminal_get_text(term, NULL, FALSE, NULL);
}

char *
tazterm_term_get_text_tail(VteTerminal *term, int n)
{
	GtkAdjustment *va;
	glong col, row, first, start;

	/* Rows are absolute: the scrollback starts at the adjustment's
	 * lower bound, the cursor row ends the output. Independent of
	 * where the view is scrolled. */
	va = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(term));
	first = (glong) gtk_adjustment_get_lower(va);
	vte_terminal_get_cursor_position(term, &col, &row);
	start = n > 0 ? row - n + 1 : first;
	if (start < first)
		start = first;
	return vte_terminal_get_text_range(term, start, 0, row,
	    vte_terminal_get_column_count(term) - 1, NULL, NULL, NULL);
}

#define TAZTERM_PASTE_MAX (256 * 1024)

char *
tazterm_text_sanitize(const char *in)
{
	char *valid;
	const char *p;
	GString *gs;

	if (!in || !*in)
		return NULL;
	valid = g_utf8_make_valid(in, -1);
	gs = g_string_new(NULL);
	for (p = valid; *p; p = g_utf8_next_char(p)) {
		gunichar c = g_utf8_get_char(p);

		if (c == '\r') {
			if (p[1] != '\n')
				g_string_append_c(gs, '\n');
		} else if (c == '\t' || c == '\n' ||
		    (c >= 0x20 && c != 0x7f && (c < 0x80 || c > 0x9f))) {
			g_string_append_unichar(gs, c);
		}
	}
	g_free(valid);
	if (gs->len == 0 || gs->len > TAZTERM_PASTE_MAX) {
		if (tazterm_debug() && gs->len)
			g_printerr("tazterm: paste too large (%lu bytes)\n",
			    (unsigned long) gs->len);
		g_string_free(gs, TRUE);
		return NULL;
	}
	return g_string_free(gs, FALSE);
}

/* Hand clean text to VTE's own paste: VTE maps newlines to CR and adds
 * bracketed-paste markers only when the application asked for them
 * (DECSET 2004, invisible to tazterm on VTE 0.56). Forcing the markers
 * garbled the paste in busybox ash and still ran each line.
 * VTE re-reads the selection, so the selection is replaced only when
 * cleaning changed the text: an untouched paste keeps the original
 * owner (the copied text survives tazterm). Under X11 any client may
 * serve different data on the second read; X11 clients are trusted
 * anyway (they can inject keys). */
static void
paste_clean(VteTerminal *term, GdkAtom sel, const char *clean,
    const char *orig)
{
	if (g_strcmp0(clean, orig) != 0)
		gtk_clipboard_set_text(gtk_clipboard_get(sel), clean, -1);
	if (sel == GDK_SELECTION_PRIMARY)
		vte_terminal_paste_primary(term);
	else
		vte_terminal_paste_clipboard(term);
}

gboolean
tazterm_term_paste_text(VteTerminal *term, GdkAtom sel, const char *text)
{
	char *clean;

	clean = tazterm_text_sanitize(text);
	if (!clean)
		return FALSE;
	paste_clean(term, sel, clean, text);
	g_free(clean);
	return TRUE;
}

/* TRUE when the pane's foreground process is its own shell and that
 * shell has no bracketed paste (busybox ash, dash): each pasted line
 * would run at once. bash/zsh/fish hold a multi-line paste in the
 * editor until Enter. */
static gboolean
term_at_raw_prompt(VteTerminal *term)
{
	const char *shell;
	const char *base;
	gpointer pid;
	VtePty *pty;
	pid_t fg;

	shell = g_object_get_data(G_OBJECT(term), "tazterm-shell");
	pid = g_object_get_data(G_OBJECT(term), "tazterm-pid");
	pty = vte_terminal_get_pty(term);
	if (!shell || !pid || !pty)
		return FALSE;
	/* Unknown foreground (error): assume the prompt, stay safe. */
	fg = tcgetpgrp(vte_pty_get_fd(pty));
	if (fg >= 0 && fg != (pid_t) GPOINTER_TO_INT(pid))
		return FALSE;
	base = strrchr(shell, '/');
	base = base ? base + 1 : shell;
	return !(strstr(base, "bash") || !strcmp(base, "zsh") ||
	    !strcmp(base, "fish"));
}

/* Pending clipboard paste. term is a weak pointer: the pane may die
 * while the clipboard answers or the dialog is open. */
typedef struct {
	VteTerminal *term;
	char *orig;
	char *clean;
} PasteReq;

static void
paste_req_free(PasteReq *req)
{
	if (req->term)
		g_object_remove_weak_pointer(G_OBJECT(req->term),
		    (gpointer *) &req->term);
	g_free(req->orig);
	g_free(req->clean);
	g_free(req);
}

static void
on_paste_confirm_destroy(GtkWidget *dialog, gpointer data)
{
	(void) dialog;
	paste_req_free(data);
}

static void
on_paste_confirm(GtkDialog *dialog, int response, gpointer data)
{
	PasteReq *req = data;

	if (response == GTK_RESPONSE_ACCEPT && req->term)
		paste_clean(req->term, GDK_SELECTION_CLIPBOARD, req->clean,
		    req->orig);
	gtk_widget_destroy(GTK_WIDGET(dialog)); /* frees req */
}

#define TAZTERM_PREVIEW_LINES 8
#define TAZTERM_PREVIEW_CHARS 80

/* Ask before a multi-line paste reaches a raw prompt. Non-blocking
 * (no gtk_dialog_run: a nested main loop may free the pane). */
static void
paste_confirm(PasteReq *req)
{
	GtkWidget *top;
	GtkWidget *dialog;
	GString *preview;
	char **lines;
	guint n, i;

	lines = g_strsplit(req->clean, "\n", -1);
	n = g_strv_length(lines);
	if (n > 1 && !*lines[n - 1])
		n--; /* trailing newline: no extra line */
	preview = g_string_new(NULL);
	for (i = 0; i < n && i < TAZTERM_PREVIEW_LINES; i++) {
		char *cut = g_utf8_substring(lines[i], 0,
		    TAZTERM_PREVIEW_CHARS);

		g_string_append_printf(preview, "%s%s\n", cut,
		    g_utf8_strlen(lines[i], -1) > TAZTERM_PREVIEW_CHARS ?
		    "…" : "");
		g_free(cut);
	}
	if (n > TAZTERM_PREVIEW_LINES)
		g_string_append(preview, "…\n");
	g_strfreev(lines);

	top = gtk_widget_get_toplevel(GTK_WIDGET(req->term));
	dialog = gtk_message_dialog_new(
	    gtk_widget_is_toplevel(top) ? GTK_WINDOW(top) : NULL,
	    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
	    GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
	    _("Coller %u lignes dans le shell ?"), n);
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
	    "%s\n\n%s",
	    _("Ce shell exécute chaque ligne dès qu'elle est collée."),
	    preview->str);
	g_string_free(preview, TRUE);
	gtk_dialog_add_buttons(GTK_DIALOG(dialog),
	    _("Annuler"), GTK_RESPONSE_CANCEL,
	    _("Coller"), GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog),
	    GTK_RESPONSE_CANCEL);
	g_signal_connect(dialog, "response", G_CALLBACK(on_paste_confirm),
	    req);
	g_signal_connect(dialog, "destroy",
	    G_CALLBACK(on_paste_confirm_destroy), req);
	gtk_widget_show(dialog);
	if (tazterm_debug())
		g_printerr("tazterm: paste: confirm %u lines\n", n);
}

static void
on_clipboard_text(GtkClipboard *clip, const char *text, gpointer data)
{
	PasteReq *req = data;

	(void) clip;
	if (!req->term)
		goto out;
	req->clean = tazterm_text_sanitize(text);
	if (!req->clean)
		goto out;
	req->orig = g_strdup(text);
	if (strchr(req->clean, '\n') && term_at_raw_prompt(req->term)) {
		paste_confirm(req); /* owns req now */
		return;
	}
	paste_clean(req->term, GDK_SELECTION_CLIPBOARD, req->clean,
	    req->orig);
out:
	paste_req_free(req);
}

void
tazterm_term_paste_clipboard(VteTerminal *term)
{
	PasteReq *req;

	if (!term)
		return;
	/* Async read: gtk_clipboard_wait_for_text() spins a nested main
	 * loop where child-exited may destroy the pane under us. */
	req = g_new0(PasteReq, 1);
	req->term = term;
	g_object_add_weak_pointer(G_OBJECT(term), (gpointer *) &req->term);
	gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
	    on_clipboard_text, req);
}

char *
tazterm_term_get_process(VteTerminal *term)
{
	VtePty *pty;
	pid_t fg;
	char *path;
	char *comm = NULL;

	pty = vte_terminal_get_pty(term);
	if (!pty)
		return NULL;
	fg = tcgetpgrp(vte_pty_get_fd(pty));
	if (fg <= 0)
		return NULL;
	path = g_strdup_printf("/proc/%d/comm", (int) fg);
	if (!g_file_get_contents(path, &comm, NULL, NULL))
		comm = NULL;
	g_free(path);
	return comm ? g_strstrip(comm) : NULL;
}

/* Active shell's cwd. /proc first when readable (cannot be spoofed by
 * OSC 7 from the PTY); OSC 7 next (works when /proc/cwd is blocked). */
char *
tazterm_term_get_cwd(VteTerminal *term)
{
	const char *uri;
	char *path = NULL;
	gpointer p;
	char *link;

	p = g_object_get_data(G_OBJECT(term), "tazterm-pid");
	if (p) {
		link = g_strdup_printf("/proc/%d/cwd", GPOINTER_TO_INT(p));
		path = g_file_read_link(link, NULL);
		g_free(link);
		if (path && g_file_test(path, G_FILE_TEST_IS_DIR))
			return path;
		g_free(path);
		path = NULL;
	}

	uri = vte_terminal_get_current_directory_uri(term);
	if (uri && *uri) {
		/* Only local file URIs: the pty can emit any OSC 7,
		 * so refuse remote hosts (file://evilhost/...) that
		 * g_filename_from_uri would happily map to a local path. */
		if (!g_str_has_prefix(uri, "file://localhost/") &&
		    !g_str_has_prefix(uri, "file:///"))
			return NULL;
		path = g_filename_from_uri(uri, NULL, NULL);
		if (path && g_file_test(path, G_FILE_TEST_IS_DIR))
			return path;
		g_free(path);
	}
	return NULL;
}
