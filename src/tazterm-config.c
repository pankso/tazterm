/* tazterm-config.c — GKeyFile, [terminal] and [ai] groups.
 *
 * File: ~/.config/tazterm/tazterm.conf
 *   [terminal]
 *   font=Monospace 12
 *   shell=/bin/sh
 *   scrollback_lines=10000
 *   foreground=#e6e8ed
 *   background=#1c1e22
 *   working_directory=/home/tux
 *   [ai]
 *   agent=auto
 *   explain_lines=200
 *   capture_lines=2000
 */
#include "tazterm-config.h"
#include "tazterm-term.h"

#define TAZTERM_DEFAULT_FONT "Monospace 12"
#define TAZTERM_DEFAULT_SHELL "/bin/sh"
#define TAZTERM_DEFAULT_SCROLLBACK 10000

/* Config values come from a user-writable file: cap lengths so a
 * bloated entry cannot balloon memory. Over-long = rejected
 * (keep default), not truncated (a cut path would mislead). */
#define TAZTERM_MAX_FONT 256
#define TAZTERM_MAX_PATH 4096
#define TAZTERM_MAX_AGENT 64

static gboolean
overlong(const char *s, gsize max)
{
	return s && strlen(s) > max;
}

char *
tazterm_config_path(void)
{
	return g_build_filename(g_get_user_config_dir(), "tazterm",
	    "tazterm.conf", NULL);
}

static void
config_save_defaults(const char *path, TaztermConfig *cfg)
{
	GKeyFile *kf;
	char *data;
	gsize len;
	char *dir;

	kf = g_key_file_new();
	g_key_file_set_string(kf, "terminal", "font", cfg->font_desc);
	g_key_file_set_string(kf, "terminal", "shell", cfg->shell);
	g_key_file_set_integer(kf, "terminal", "scrollback_lines",
	    (gint) cfg->scrollback);
	if (cfg->workdir)
		g_key_file_set_string(kf, "terminal", "working_directory",
		    cfg->workdir);

	dir = g_path_get_dirname(path);
	g_mkdir_with_parents(dir, 0755);
	g_free(dir);

	data = g_key_file_to_data(kf, &len, NULL);
	if (data) {
		/* Fresh file in the user's own config dir; NOFOLLOW so a
		 * planted symlink cannot redirect the write elsewhere. */
		tazterm_write_file_nofollow(path, data, (gssize) len,
		    0644);
		g_free(data);
	}
	g_key_file_free(kf);
}

TaztermConfig *
tazterm_config_load(void)
{
	TaztermConfig *cfg;
	GKeyFile *kf;
	GError *err = NULL;
	char *path;
	char *s;

	cfg = g_new0(TaztermConfig, 1);
	cfg->font_desc = g_strdup(TAZTERM_DEFAULT_FONT);
	cfg->shell = g_strdup(TAZTERM_DEFAULT_SHELL);
	cfg->scrollback = TAZTERM_DEFAULT_SCROLLBACK;
	cfg->workdir = NULL;
	cfg->fg_set = FALSE;
	cfg->bg_set = FALSE;
	cfg->ai_agent = g_strdup("auto");
	cfg->ai_explain_lines = 200;
	cfg->ai_capture_lines = 2000;

	path = tazterm_config_path();
	kf = g_key_file_new();
	if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
		/* No config: create it with defaults. */
		g_clear_error(&err);
		config_save_defaults(path, cfg);
		g_free(path);
		g_key_file_free(kf);
		return cfg;
	}
	g_free(path);

	s = g_key_file_get_string(kf, "terminal", "font", NULL);
	if (s && *s && !overlong(s, TAZTERM_MAX_FONT)) {
		g_free(cfg->font_desc);
		cfg->font_desc = s;
	} else {
		g_free(s);
	}

	s = g_key_file_get_string(kf, "terminal", "shell", NULL);
	if (s && *s && !overlong(s, TAZTERM_MAX_PATH)) {
		g_free(cfg->shell);
		cfg->shell = s;
	} else {
		g_free(s);
	}

	s = g_key_file_get_string(kf, "terminal", "working_directory", NULL);
	if (s && *s && !overlong(s, TAZTERM_MAX_PATH)) {
		cfg->workdir = s;
	} else {
		g_free(s);
	}

	if (g_key_file_has_key(kf, "terminal", "scrollback_lines", NULL)) {
		cfg->scrollback = (long) g_key_file_get_integer(kf, "terminal",
		    "scrollback_lines", NULL);
		if (cfg->scrollback < 0)
			cfg->scrollback = 0;
		if (cfg->scrollback > 100000)
			cfg->scrollback = 100000;
	}

	s = g_key_file_get_string(kf, "terminal", "foreground", NULL);
	if (s && *s && gdk_rgba_parse(&cfg->foreground, s))
		cfg->fg_set = TRUE;
	g_free(s);

	s = g_key_file_get_string(kf, "terminal", "background", NULL);
	if (s && *s && gdk_rgba_parse(&cfg->background, s))
		cfg->bg_set = TRUE;
	g_free(s);

	s = g_key_file_get_string(kf, "ai", "agent", NULL);
	if (s && *s && !overlong(s, TAZTERM_MAX_AGENT)) {
		g_free(cfg->ai_agent);
		cfg->ai_agent = s;
	} else {
		g_free(s);
	}

	if (g_key_file_has_key(kf, "ai", "explain_lines", NULL))
		cfg->ai_explain_lines =
		    g_key_file_get_integer(kf, "ai", "explain_lines",
		    NULL);
	if (cfg->ai_explain_lines < 10)
		cfg->ai_explain_lines = 10;
	if (cfg->ai_explain_lines > 10000)
		cfg->ai_explain_lines = 10000;

	if (g_key_file_has_key(kf, "ai", "capture_lines", NULL))
		cfg->ai_capture_lines =
		    g_key_file_get_integer(kf, "ai", "capture_lines",
		    NULL);
	if (cfg->ai_capture_lines < 10)
		cfg->ai_capture_lines = 10;
	if (cfg->ai_capture_lines > 100000)
		cfg->ai_capture_lines = 100000;

	g_key_file_free(kf);
	return cfg;
}

void
tazterm_config_free(TaztermConfig *cfg)
{
	if (!cfg)
		return;
	g_free(cfg->font_desc);
	g_free(cfg->shell);
	g_free(cfg->workdir);
	g_free(cfg->ai_agent);
	g_free(cfg);
}
