/* tazterm-ai.c — E4: detection agents, capture scrollback, prompt erreur.
 *
 * Tout passe par du texte brut : le pty ne distingue pas un agent d'un
 * compilateur, donc opencode / claude / navette marchent sans adaptation
 * (navette one-shot comme TUI : stdout texte, stderr fusionne a l'ecran).
 */
#include "tazterm-ai.h"

#include "tazterm-term.h"

#include <unistd.h>

const char *const tazterm_ai_known_agents[] = {
	"opencode", "claude", "navette", NULL
};

char **
tazterm_ai_detect(const char *cfg_agent, char **def_out)
{
	GPtrArray *found;
	char *def = NULL;
	int i;

	found = g_ptr_array_new();
	for (i = 0; tazterm_ai_known_agents[i]; i++) {
		const char *name = tazterm_ai_known_agents[i];
		char *path;

		path = g_find_program_in_path(name);
		if (path) {
			g_ptr_array_add(found, g_strdup(name));
			g_free(path);
			if (!def && (!cfg_agent ||
			    !g_strcmp0(cfg_agent, "auto") ||
			    !g_strcmp0(cfg_agent, name)))
				def = g_strdup(name);
		}
	}
	/* Choix explicite mais binaire absent : repli sur le premier. */
	if (!def && found->len > 0)
		def = g_strdup(g_ptr_array_index(found, 0));
	g_ptr_array_add(found, NULL);

	*def_out = def;
	return (char **) g_ptr_array_free(found, FALSE);
}

const char *
tazterm_ai_launch_cmd(const char *agent)
{
	const char *env;

	env = g_getenv("TAZTERM_AGENT_CMD");
	if (env && *env)
		return env;
	if (agent && *agent)
		return agent;
	return "opencode";
}

char *
tazterm_ai_last_lines(VteTerminal *term, int n)
{
	char *full;
	char **lines;
	int total, start, i;
	GString *gs;

	full = tazterm_term_get_visible_text(term);
	if (!full || !*full) {
		g_free(full);
		return g_strdup("");
	}
	if (n <= 0)
		return full;

	lines = g_strsplit(full, "\n", -1);
	g_free(full);
	total = (int) g_strv_length(lines);
	start = total - n;
	if (start < 0)
		start = 0;

	gs = g_string_new(NULL);
	for (i = start; i < total; i++) {
		if (i > start)
			g_string_append_c(gs, '\n');
		g_string_append(gs, lines[i]);
	}
	g_strfreev(lines);
	return g_string_free(gs, FALSE);
}

gboolean
tazterm_ai_looks_like_error(const char *text)
{
	static GRegex *re = NULL;
	GError *err = NULL;

	if (!text || !*text)
		return FALSE;
	if (!re) {
		re = g_regex_new(
		    "(error|traceback|fail|fatal|undefined|not found|"
		    "no such file|cannot open|could not|exception|"
		    "erreur|échec)",
		    G_REGEX_CASELESS, 0, &err);
		if (!re) {
			g_warning("tazterm: bad error regex: %s",
			    err ? err->message : "?");
			g_clear_error(&err);
			return FALSE;
		}
	}
	return g_regex_match(re, text, 0, NULL);
}

char *
tazterm_ai_save_capture(GtkWidget *win, const char *text,
    const char *prefix)
{
	GtkClipboard *clip;
	char *path;

	(void) win;

	if (!text)
		text = "";
	clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text(clip, text, -1);

	path = g_strdup_printf("/tmp/tazterm-%s-%d.log", prefix,
	    (int) getpid());
	if (!g_file_set_contents(path, text, -1, NULL)) {
		g_free(path);
		return NULL;
	}
	/* Log fait par l'appelant (fenetre) pour eviter les doublons. */
	return path;
}

char *
tazterm_ai_explain(GtkWidget *win, VteTerminal *term, int nlines)
{
	char *last;
	GString *prompt;
	char *path;
	GtkClipboard *clip;

	last = tazterm_ai_last_lines(term, nlines);
	prompt = g_string_new(NULL);
	g_string_append(prompt,
	    "# Erreur a expliquer (capture TazTerm)\n\n"
	    "Ci-dessous les dernieres lignes du terminal. ");
	if (tazterm_ai_looks_like_error(last))
		g_string_append(prompt,
		    "Ca ressemble a une erreur : explique la cause "
		    "et propose un correctif.\n\n");
	else
		g_string_append(prompt,
		    "Pas de motif d'erreur evident detecte : decris "
		    "quand meme ce que fait cette sortie.\n\n");
	g_string_append(prompt, "```\n");
	g_string_append(prompt, last);
	g_string_append(prompt, "\n```\n");
	g_free(last);

	clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text(clip, prompt->str, -1);

	path = g_strdup_printf("/tmp/tazterm-explain-%d.md",
	    (int) getpid());
	if (!g_file_set_contents(path, prompt->str, -1, NULL)) {
		g_free(path);
		path = NULL;
	}
	/* Log fait par l'appelant (fenetre) pour eviter les doublons. */
	g_string_free(prompt, TRUE);

	(void) win;
	return path;
}
