/* tazterm-term.c — VteTerminal wrapper: spawn, zoom, search. */
#include "tazterm-term.h"

#include <stdlib.h>
#include <string.h>

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

/* Shell integration: the shell reports its cwd via OSC 7, which VTE
 * exposes through vte_terminal_get_current_directory_uri().
 * /etc/profile.d/vte.sh only covers login bash/zsh; here we target ash
 * (the SliTaz default) via $ENV, and bash via PROMPT_COMMAND when unset.
 * File owned by tazterm: rewritten when the version marker differs.
 * Users may append custom code BELOW the marker line.
 */
#define TAZTERM_INTEGRATION_VERSION 1

static const char integration_sh[] =
"# tazterm shell integration v2 (managed by tazterm, do not edit above)\n"
"# ash/dash: sourced via $ENV. Reports cwd with OSC 7 for split panes.\n"
"# bash is covered by PROMPT_COMMAND (see tazterm) or /etc/profile.d/vte.sh.\n"
"# Custom code may go BELOW the marker line.\n"
"# TAZTERM-CUSTOM-BELOW\n"
"if [ -z \"$TAZTERM_OSC7\" ] && [ -n \"$PS1\" ]; then\n"
"    TAZTERM_OSC7=1\n"
"    export TAZTERM_OSC7\n"
"    __tazterm_osc7() {\n"
"        printf '\\033]7;file://localhost%s\\033\\\\' \"${PWD// /%20}\"\n"
"    }\n"
"    cd() { command cd \"$@\" && __tazterm_osc7; }\n"
"    __tazterm_osc7\n"
"fi\n";

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
	    strstr(old, "tazterm shell integration v2")) {
		g_free(old);
		g_free(path);
		return;
	}
	g_free(old);
	if (g_file_set_contents(path, integration_sh, -1, NULL) &&
	    tazterm_debug())
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

/* Hook OSC 7 reporting into shell panes so splits can inherit the
 * active pane's cwd. Respects existing user settings: never overrides
 * an already-set ENV or PROMPT_COMMAND. */
static void
env_integrate_shell(char ***envv, const char *shell)
{
	const char *base;
	const char *old;

	base = shell ? strrchr(shell, '/') : NULL;
	base = base ? base + 1 : shell;
	if (base && strstr(base, "bash")) {
		old = g_environ_getenv(*envv, "PROMPT_COMMAND");
		if (!old || !*old) {
			*envv = g_environ_setenv(*envv, "PROMPT_COMMAND",
			    "printf '\\033]7;file://localhost%s\\033\\\\' "
			    "\"${PWD// /%20}\"",
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

gboolean
tazterm_term_search(VteTerminal *term, const char *text)
{
	VteRegex *regex;
	GError *err = NULL;

	if (!text || !*text)
		return FALSE;

	/* VTE requires PCRE2_MULTILINE for search, otherwise
	 * search_set_regex fails (VTE-side runtime check). */
	regex = vte_regex_new_for_search(text, -1, PCRE2_MULTILINE, &err);
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
		g_printerr("tazterm: search pattern='%s'\n", text);
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

/* Active shell's cwd. Sources, in order: OSC 7 reported by our shell
 * integration (works everywhere), /proc/<pid>/cwd (same-uid kernels;
 * blocked on some hardened setups), NULL when unknown. Caller frees. */
char *
tazterm_term_get_cwd(VteTerminal *term)
{
	const char *uri;
	char *path = NULL;
	gpointer p;
	char *link;

	uri = vte_terminal_get_current_directory_uri(term);
	if (uri && *uri) {
		path = g_filename_from_uri(uri, NULL, NULL);
		if (path && !g_file_test(path, G_FILE_TEST_IS_DIR)) {
			g_free(path);
			path = NULL;
		}
		if (path)
			return path;
	}

	p = g_object_get_data(G_OBJECT(term), "tazterm-pid");
	if (!p)
		return NULL;
	link = g_strdup_printf("/proc/%d/cwd", GPOINTER_TO_INT(p));
	path = g_file_read_link(link, NULL);
	g_free(link);
	return path; /* NULL if the process is gone */
}
