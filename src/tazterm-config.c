/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
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
#define TAZTERM_DEFAULT_SHELL "auto"
#define TAZTERM_DEFAULT_SCROLLBACK 10000
#define TAZTERM_DEFAULT_EXPLAIN 200
#define TAZTERM_DEFAULT_CAPTURE 2000

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

/* "auto": bash when installed (it has a prompt hook, busybox ash has
 * none: no command blocks there), else /bin/sh. Anything else is kept
 * as given. Returns a new string. */
static char *
shell_resolve(const char *shell)
{
	char *bash;

	if (strcmp(shell, "auto") != 0)
		return g_strdup(shell);
	bash = g_find_program_in_path("bash");
	return bash ? bash : g_strdup("/bin/sh");
}

/* Every key, so the user can discover them; optional ones commented. */
static const char default_conf[] =
"# TazTerm configuration (created with defaults).\n"
"[terminal]\n"
"font=" TAZTERM_DEFAULT_FONT "\n"
"# auto: bash when installed (command blocks, ctl read -l / wait,\n"
"# exit codes in the status bar), else /bin/sh (busybox ash)\n"
"shell=" TAZTERM_DEFAULT_SHELL "\n"
"scrollback_lines=" G_STRINGIFY(TAZTERM_DEFAULT_SCROLLBACK) "\n"
"# Colors: slitaz | tango | solarized-dark | solarized-light | vte\n"
"# (VTE's own). foreground= / background= override the theme's.\n"
"theme=slitaz\n"
"#foreground=#e6e8ed\n"
"#background=#1c1e22\n"
"# Cursor: block | ibeam | underline\n"
"#cursor_shape=block\n"
"# Bold text in bright colors (older apps expect it)\n"
"#bold_is_bright=false\n"
"#working_directory=/home/user\n"
"# Terminal editor for Ctrl+click on file:line (default: $VISUAL,\n"
"# else $EDITOR when it runs in a terminal, else vi)\n"
"#editor=nano\n"
"# One-line status under each pane: process, cwd, agent state\n"
"status_bar=true\n"
"# Ask before closing a pane/window where a program still runs\n"
"confirm_close=true\n"
"# A command running this long (seconds, bash) that ends in a pane you\n"
"# are not looking at: orange outline + urgency hint. 0 = never\n"
"notify_after=30\n"
"\n"
"[ai]\n"
"# auto (first found) | opencode | claude | navette, or a command with\n"
"# its arguments: agent=claude --continue\n"
"agent=auto\n"
"# Lines sent by \"explain\" / copied by \"copy scrollback\"\n"
"explain_lines=" G_STRINGIFY(TAZTERM_DEFAULT_EXPLAIN) "\n"
"capture_lines=" G_STRINGIFY(TAZTERM_DEFAULT_CAPTURE) "\n"
"# Mask keys, tokens and passwords in text handed to agents\n"
"redact=true\n";

/* Color themes: foreground, background, cursor ("" = VTE's), then
 * the 16 palette colors (8 normal, 8 bright). */
static const struct {
	const char *name;
	const char *colors[19];
} themes[] = {
	{ "slitaz", { "#e6e8ed", "#1c1e22", "#d45500",
	    "#2a2d33", "#cc3e28", "#6aa84f", "#d49a00",
	    "#4a90d9", "#9b6fbf", "#2aa1b3", "#c8ccd4",
	    "#5c6370", "#ef5b43", "#8ae234", "#fcd34d",
	    "#7ab8f5", "#c49be0", "#56d4e6", "#f2f4f8" } },
	{ "tango", { "#d3d7cf", "#2e3436", "",
	    "#2e3436", "#cc0000", "#4e9a06", "#c4a000",
	    "#3465a4", "#75507b", "#06989a", "#d3d7cf",
	    "#555753", "#ef2929", "#8ae234", "#fce94f",
	    "#729fcf", "#ad7fa8", "#34e2e2", "#eeeeec" } },
	{ "solarized-dark", { "#839496", "#002b36", "#93a1a1",
	    "#073642", "#dc322f", "#859900", "#b58900",
	    "#268bd2", "#d33682", "#2aa198", "#eee8d5",
	    "#002b36", "#cb4b16", "#586e75", "#657b83",
	    "#839496", "#6c71c4", "#93a1a1", "#fdf6e3" } },
	{ "solarized-light", { "#657b83", "#fdf6e3", "#586e75",
	    "#073642", "#dc322f", "#859900", "#b58900",
	    "#268bd2", "#d33682", "#2aa198", "#eee8d5",
	    "#002b36", "#cb4b16", "#586e75", "#657b83",
	    "#839496", "#6c71c4", "#93a1a1", "#fdf6e3" } },
};

/* Theme colors into cfg; FALSE for an unknown name. "vte": nothing. */
static gboolean
theme_apply(TaztermConfig *cfg, const char *name)
{
	guint i;
	int c;

	if (!strcmp(name, "vte"))
		return TRUE;
	for (i = 0; i < G_N_ELEMENTS(themes); i++) {
		if (strcmp(themes[i].name, name) != 0)
			continue;
		gdk_rgba_parse(&cfg->foreground, themes[i].colors[0]);
		gdk_rgba_parse(&cfg->background, themes[i].colors[1]);
		cfg->fg_set = cfg->bg_set = TRUE;
		cfg->cursor_set = *themes[i].colors[2] &&
		    gdk_rgba_parse(&cfg->cursor, themes[i].colors[2]);
		for (c = 0; c < 16; c++)
			gdk_rgba_parse(&cfg->palette[c],
			    themes[i].colors[3 + c]);
		cfg->palette_set = TRUE;
		return TRUE;
	}
	return FALSE;
}

static void
config_save_defaults(const char *path)
{
	char *dir, *keys, *conf;

	dir = g_path_get_dirname(path);
	g_mkdir_with_parents(dir, 0755);
	g_free(dir);
	/* Fresh file in the user's own config dir; NOFOLLOW so a
	 * planted symlink cannot redirect the write elsewhere. */
	keys = tazterm_keys_default_conf();
	conf = g_strconcat(default_conf, keys, NULL);
	tazterm_write_file_nofollow(path, conf, -1, 0644);
	g_free(conf);
	g_free(keys);
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
	cfg->shell = shell_resolve(TAZTERM_DEFAULT_SHELL);
	cfg->scrollback = TAZTERM_DEFAULT_SCROLLBACK;
	cfg->workdir = NULL;
	cfg->fg_set = FALSE;
	cfg->bg_set = FALSE;
	cfg->ai_agent = g_strdup("auto");
	cfg->ai_explain_lines = TAZTERM_DEFAULT_EXPLAIN;
	cfg->ai_capture_lines = TAZTERM_DEFAULT_CAPTURE;
	cfg->ai_redact = TRUE;
	cfg->status_bar = TRUE;
	cfg->confirm_close = TRUE;
	cfg->notify_after = 30;
	cfg->cursor_shape = -1;
	cfg->bold_is_bright = -1;

	path = tazterm_config_path();
	kf = g_key_file_new();
	if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
		/* No config: create it with defaults. */
		g_clear_error(&err);
		config_save_defaults(path);
		theme_apply(cfg, "slitaz"); /* as in the file just written */
		cfg->keys = tazterm_keys_new(NULL);
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
		cfg->shell = shell_resolve(s);
		g_free(s);
	} else {
		g_free(s);
	}

	s = g_key_file_get_string(kf, "terminal", "editor", NULL);
	if (s && *s && !overlong(s, TAZTERM_MAX_PATH))
		cfg->editor = s;
	else
		g_free(s);

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

	/* Theme first: foreground= / background= then override it. */
	s = g_key_file_get_string(kf, "terminal", "theme", NULL);
	if (s && *s && !theme_apply(cfg, g_strstrip(s)))
		g_warning("tazterm: unknown theme '%s'", s);
	g_free(s);

	s = g_key_file_get_string(kf, "terminal", "cursor_shape", NULL);
	if (s && !g_strcmp0(g_strstrip(s), "block"))
		cfg->cursor_shape = VTE_CURSOR_SHAPE_BLOCK;
	else if (s && !strcmp(s, "ibeam"))
		cfg->cursor_shape = VTE_CURSOR_SHAPE_IBEAM;
	else if (s && !strcmp(s, "underline"))
		cfg->cursor_shape = VTE_CURSOR_SHAPE_UNDERLINE;
	g_free(s);

	if (g_key_file_has_key(kf, "terminal", "bold_is_bright", NULL))
		cfg->bold_is_bright = g_key_file_get_boolean(kf, "terminal",
		    "bold_is_bright", NULL);

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

	if (g_key_file_has_key(kf, "terminal", "status_bar", NULL))
		cfg->status_bar = g_key_file_get_boolean(kf, "terminal",
		    "status_bar", NULL);

	if (g_key_file_has_key(kf, "terminal", "confirm_close", NULL))
		cfg->confirm_close = g_key_file_get_boolean(kf, "terminal",
		    "confirm_close", NULL);

	if (g_key_file_has_key(kf, "terminal", "notify_after", NULL)) {
		cfg->notify_after = g_key_file_get_integer(kf, "terminal",
		    "notify_after", NULL);
		if (cfg->notify_after < 0)
			cfg->notify_after = 0;
	}

	if (g_key_file_has_key(kf, "ai", "redact", NULL))
		cfg->ai_redact = g_key_file_get_boolean(kf, "ai", "redact",
		    NULL);

	cfg->keys = tazterm_keys_new(kf);

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
	g_free(cfg->editor);
	g_free(cfg->ai_agent);
	tazterm_keys_free(cfg->keys);
	g_free(cfg);
}
