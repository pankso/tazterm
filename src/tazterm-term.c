/* tazterm-term.c — E2: VteTerminal + spawn shell, zoom, recherche. */
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

static void
on_spawn_ready(VteTerminal *term, GPid pid, GError *error, gpointer user_data)
{
	(void) user_data;

	if (!term)
		return;
	if (error) {
		g_warning("tazterm: spawn failed: %s", error->message);
		vte_terminal_feed(term,
		    "\r\n[tazterm] impossible de lancer le shell.\r\n",
		    -1);
		return;
	}
	/* VTE 0.56 surveille deja le fils de spawn_async en interne et emet
	 * "child-exited". Ne PAS appeler vte_terminal_watch_child ici : un
	 * second watch provoque ECHILD + double emission et corrompt le
	 * dispose (crash intermittent a la fermeture d'un panneau). */
	(void) pid;
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
		/* Agent : spawn direct, pas de course avec le shell. */
		argv[0] = (char *) command;
		argv[1] = NULL;
	} else if (command && *command) {
		/* Commande composee (tests) : via shell -c. Toujours un
		 * vrai shell (cfg), jamais le -s override (souvent un
		 * script qui ignore $1, ex. e4mark.sh). */
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
		g_warning("tazterm: dossier '%s' absent, repli sur $HOME",
		    workdir);
		workdir = g_get_home_dir();
	}

	if (tazterm_debug())
		g_printerr("tazterm: spawn argv0='%s'%s cwd='%s'\n", argv[0],
		    argv[1] ? " (+args)" : "", workdir);

	envv = g_get_environ();

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

	/* VTE exige PCRE2_MULTILINE pour la recherche, sinon
	 * search_set_regex echoue (runtime check cote VTE). */
	regex = vte_regex_new_for_search(text, -1, PCRE2_MULTILINE, &err);
	if (!regex) {
		g_warning("tazterm: regex invalide: %s",
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
	/* E2 : tout le scrollback visible ; E4 affinera (range + bornes). */
	return vte_terminal_get_text(term, NULL, FALSE, NULL);
}
