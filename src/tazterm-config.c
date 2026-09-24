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
"#foreground=#e6e8ed\n"
"#background=#1c1e22\n"
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

static void
config_save_defaults(const char *path)
{
	char *dir;

	dir = g_path_get_dirname(path);
	g_mkdir_with_parents(dir, 0755);
	g_free(dir);
	/* Fresh file in the user's own config dir; NOFOLLOW so a
	 * planted symlink cannot redirect the write elsewhere. */
	tazterm_write_file_nofollow(path, default_conf, -1, 0644);
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

	path = tazterm_config_path();
	kf = g_key_file_new();
	if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
		/* No config: create it with defaults. */
		g_clear_error(&err);
		config_save_defaults(path);
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
	g_free(cfg);
}
