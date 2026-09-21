/* tazterm-term.c — VteTerminal wrapper: spawn, zoom, search. */
#include "tazterm-term.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <glib/gstdio.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#define TAZTERM_ZOOM_STEP 0.1
#define TAZTERM_ZOOM_MIN 0.5
#define TAZTERM_ZOOM_MAX 3.0

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
 */
static const char integration_sh[] =
"# tazterm shell integration v3 (managed by tazterm, do not edit above)\n"
"# ash/dash: sourced via $ENV. Reports cwd with OSC 7 for split panes.\n"
"# bash is covered by PROMPT_COMMAND (see tazterm) or /etc/profile.d/vte.sh.\n"
"# Custom code may go BELOW the marker line.\n"
"if [ -z \"$TAZTERM_OSC7\" ] && [ -n \"$PS1\" ]; then\n"
"    TAZTERM_OSC7=1\n"
"    export TAZTERM_OSC7\n"
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
"# TAZTERM-CUSTOM-BELOW\n";

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

	path = integration_path();
	if (g_file_get_contents(path, &old, NULL, NULL) &&
	    strstr(old, "tazterm shell integration v3")) {
		g_free(old);
		g_free(path);
		return;
	}
	g_free(old);
	{
		char *dir = g_path_get_dirname(path);

		g_mkdir_with_parents(dir, 0755);
		g_free(dir);
	}
	/* NOFOLLOW write: never redirect through a planted symlink. */
	if (!tazterm_write_file_nofollow(path, integration_sh, -1, 0644)) {
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
	} else if (tazterm_debug()) {
		g_printerr("tazterm: keep user ENV=%s\n", old);
	}
}

VteTerminal *
tazterm_term_new(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override)
{
	return tazterm_term_new_cmd(cfg, shell_override, workdir_override,
	    NULL);
}

VteTerminal *
tazterm_term_new_cmd(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override,
    const char *command)
{
	VteTerminal *term;
	PangoFontDescription *font;
	const char *shell;
	const char *workdir;
	char *argv[4];
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
	if (command && *command && !strchr(command, ' ')) {
		/* Agent: direct spawn, no race with the shell. */
		argv[0] = (char *) command;
		argv[1] = NULL;
	} else if (command && *command) {
		/* Compound command (tests): via shell -c. Always a real
		 * shell (cfg), never the -s override (often a script that
		 * ignores $1, e.g. e4mark.sh). */
		argv[0] = (char *) cfg->shell;
		argv[1] = "-c";
		argv[2] = (char *) command;
		argv[3] = NULL;
	} else {
		argv[0] = (char *) shell;
		argv[1] = NULL;
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
	if (!command || !*command)
		env_integrate_shell(&envv, shell);

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

static void
zoom_set(VteTerminal *term, gdouble scale)
{
	if (scale < TAZTERM_ZOOM_MIN)
		scale = TAZTERM_ZOOM_MIN;
	if (scale > TAZTERM_ZOOM_MAX)
		scale = TAZTERM_ZOOM_MAX;
	vte_terminal_set_font_scale(term, scale);
	if (tazterm_debug())
		g_printerr("tazterm: zoom scale=%.2f\n", scale);
}

void
tazterm_term_zoom_in(VteTerminal *term)
{
	zoom_set(term, vte_terminal_get_font_scale(term) + TAZTERM_ZOOM_STEP);
}

void
tazterm_term_zoom_out(VteTerminal *term)
{
	zoom_set(term, vte_terminal_get_font_scale(term) - TAZTERM_ZOOM_STEP);
}

void
tazterm_term_zoom_reset(VteTerminal *term)
{
	zoom_set(term, 1.0);
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
	/* Whole visible scrollback; callers slice what they need. */
	return vte_terminal_get_text(term, NULL, FALSE, NULL);
}

char *
tazterm_term_get_selected_text(VteTerminal *term)
{
	GtkClipboard *clip;
	char *text;

	/* VTE 0.56 exposes no direct "selected text" getter. Copy to
	 * PRIMARY (not CLIPBOARD) and read it back synchronously. */
	if (!vte_terminal_get_has_selection(term))
		return NULL;
	vte_terminal_copy_primary(term);
	clip = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
	text = gtk_clipboard_wait_for_text(clip);
	if (!text || !*text) {
		g_free(text);
		return NULL;
	}
	return text;
}

#define TAZTERM_PASTE_MAX (256 * 1024)

/* Keep tab/newline; drop other C0 and DEL (so ^C/^D/ESC cannot hijack
 * the agent). Lone CR becomes newline; CR+LF is one newline. */
static char *
paste_sanitize(const char *in, gsize *out_len)
{
	GString *gs;
	const unsigned char *p;

	gs = g_string_new(NULL);
	for (p = (const unsigned char *) in; *p; p++) {
		if (*p == '\t' || *p == '\n')
			g_string_append_c(gs, (char) *p);
		else if (*p == '\r') {
			if (p[1] != '\n')
				g_string_append_c(gs, '\n');
		} else if (*p >= 32 && *p != 127)
			g_string_append_c(gs, (char) *p);
	}
	if (out_len)
		*out_len = gs->len;
	return g_string_free(gs, FALSE);
}

static gboolean
feed_sanitized(VteTerminal *term, const char *text, gboolean newline)
{
	char *clean;
	gsize len;
	GString *gs;

	if (!term || !text || !*text)
		return FALSE;
	clean = paste_sanitize(text, &len);
	if (!clean || len == 0) {
		g_free(clean);
		return FALSE;
	}
	if (len > TAZTERM_PASTE_MAX) {
		if (tazterm_debug())
			g_printerr("tazterm: paste too large (%lu bytes)\n",
			    (unsigned long) len);
		g_free(clean);
		return FALSE;
	}
	gs = g_string_sized_new(len + 16);
	g_string_append(gs, "\033[200~");
	g_string_append_len(gs, clean, (gssize) len);
	g_string_append(gs, "\033[201~");
	if (newline)
		g_string_append_c(gs, '\n');
	g_free(clean);
	vte_terminal_feed_child(term, gs->str, (gssize) gs->len);
	g_string_free(gs, TRUE);
	return TRUE;
}

gboolean
tazterm_term_feed_paste(VteTerminal *term, const char *text)
{
	return feed_sanitized(term, text, TRUE);
}

/* Manual paste (keyboard/menu): same sanitizer as send-to-agent
 * (C0 stripped, bracketed) but WITHOUT the trailing newline, so
 * pasting never executes by itself. */
gboolean
tazterm_term_paste_clipboard(VteTerminal *term)
{
	GtkClipboard *clip;
	char *text;
	gboolean ok;

	if (!term)
		return FALSE;
	clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	text = gtk_clipboard_wait_for_text(clip);
	if (!text || !*text) {
		g_free(text);
		return FALSE;
	}
	ok = feed_sanitized(term, text, FALSE);
	g_free(text);
	return ok;
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
