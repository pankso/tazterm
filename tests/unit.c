/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
/* unit.c — Pure functions, no display: redaction, paste sanitizer,
 * command parsing. Exit status = number of failures. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "tazterm-ai.h"
#include "tazterm-config.h"
#include "tazterm-keys.h"
#include "tazterm-term.h"

static int fails;

static void
check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		fails++;
}

static void
redact(const char *in, const char *want, guint want_n)
{
	guint n;
	char *out = tazterm_ai_redact(in, &n);
	char *what = g_strdup_printf("redact '%s'", in);

	check(!strcmp(out, want) && n == want_n, what);
	if (strcmp(out, want))
		printf("     got '%s'\n", out);
	g_free(what);
	g_free(out);
}

int
main(void)
{
	TaztermConfig cfg = { 0 };
	char *s;
	char **argv;

	/* Secrets masked, ordinary output kept. */
	redact("export API_KEY=abcd1234efgh", "export API_KEY=[REDACTED]", 1);
	redact("password=\"s3cr3t!x\"", "password=[REDACTED]", 1);
	redact("ghp_abcdefghijklmnopqrstuvwxyz0123456789", "[REDACTED]", 1);
	redact("AKIAABCDEFGHIJKLMNOP", "[REDACTED]", 1);
	redact("-----BEGIN OPENSSH PRIVATE KEY-----\nb3Bl\n"
	    "-----END OPENSSH PRIVATE KEY-----", "[REDACTED]", 1);
	redact("Password: ", "Password: ", 0);
	redact("used 1234 tokens: ok", "used 1234 tokens: ok", 0);
	redact("pip install scikit-learn", "pip install scikit-learn", 0);
	redact("make: *** [all] Error 2", "make: *** [all] Error 2", 0);

	/* Paste sanitizer: C0 (ESC) and C1 (U+009B CSI) gone, CR -> LF,
	 * tab/newline kept, invalid UTF-8 repaired. */
	s = tazterm_text_sanitize("a\033[201~b\302\233c\r\nd\te\rf");
	check(s && !strcmp(s, "a[201~bc\nd\te\nf"), "sanitize controls");
	g_free(s);
	s = tazterm_text_sanitize("x\377y");
	check(s && g_utf8_validate(s, -1, NULL), "sanitize invalid utf-8");
	g_free(s);
	check(tazterm_text_sanitize("\033\001") == NULL, "sanitize empty");

	/* Commands: plain -> argv, shell syntax -> shell -c. */
	cfg.shell = "/bin/sh";
	argv = tazterm_command_argv(&cfg, "claude --continue");
	check(argv && !strcmp(argv[0], "claude") &&
	    !strcmp(argv[1], "--continue") && !argv[2], "argv plain");
	g_strfreev(argv);
	argv = tazterm_command_argv(&cfg, "make; notify");
	check(argv && !strcmp(argv[0], "/bin/sh") && !strcmp(argv[1], "-c"),
	    "argv shell syntax");
	g_strfreev(argv);

	/* Agents: known names, configured command with arguments. */
	check(tazterm_ai_is_agent_name("claude", "auto"), "agent known");
	check(!tazterm_ai_is_agent_name("bash", "auto"), "agent not bash");
	check(tazterm_ai_is_agent_name("aider", "/opt/bin/aider --yes"),
	    "agent configured");
	check(!strcmp(tazterm_ai_launch_cmd("claude", "claude --continue"),
	    "claude --continue"), "launch with arguments");
	check(!strcmp(tazterm_ai_launch_cmd("opencode", "claude --continue"),
	    "opencode"), "launch other agent bare");

	/* Themes: palette from the theme, background= wins over it. */
	{
		char *dir = g_dir_make_tmp("tazterm-unit-XXXXXX", NULL);
		char *conf = g_build_filename(dir, "tazterm", "tazterm.conf",
		    NULL);
		char *sub = g_path_get_dirname(conf);
		TaztermConfig *c;

		g_mkdir_with_parents(sub, 0700);
		g_file_set_contents(conf, "[terminal]\ntheme=solarized-dark\n"
		    "background=#000000\ncursor_shape=ibeam\n", -1, NULL);
		g_setenv("XDG_CONFIG_HOME", dir, TRUE);
		c = tazterm_config_load();
		check(c->palette_set && (int) (c->palette[1].red * 255 + .5) ==
		    0xdc, "theme palette");
		check(c->bg_set && c->background.red == 0 &&
		    c->background.blue == 0, "background overrides theme");
		check(c->cursor_shape == VTE_CURSOR_SHAPE_IBEAM, "cursor shape");
		check(c->bold_is_bright == -1, "bold_is_bright unset");
		tazterm_config_free(c);
		unlink(conf);
		rmdir(sub);
		rmdir(dir);
		g_free(conf);
		g_free(sub);
		g_free(dir);
	}

	/* Shortcuts: defaults, case of letters, rebinding, disabling. */
	{
		GKeyFile *kf = g_key_file_new();
		TaztermKeys *k = tazterm_keys_new(NULL);

		check(tazterm_keys_lookup(k, GDK_KEY_E, GDK_CONTROL_MASK |
		    GDK_SHIFT_MASK | GDK_MOD2_MASK) == TAZTERM_KEY_SPLIT_SIDE,
		    "keys default, NumLock ignored");
		check(tazterm_keys_lookup(k, GDK_KEY_e, GDK_CONTROL_MASK) == -1,
		    "keys modifiers exact");
		check(tazterm_keys_lookup(k, GDK_KEY_plus, GDK_CONTROL_MASK |
		    GDK_SHIFT_MASK) == TAZTERM_KEY_FONT_BIGGER, "keys Ctrl++");
		tazterm_keys_free(k);
		g_key_file_load_from_data(kf, "[keys]\nfocus_left=\n"
		    "split_side=Super+Return ctrl+alt+s\n", -1, 0, NULL);
		k = tazterm_keys_new(kf);
		check(tazterm_keys_lookup(k, GDK_KEY_Left, GDK_MOD1_MASK) == -1,
		    "keys disabled");
		check(tazterm_keys_lookup(k, GDK_KEY_Return, GDK_MOD4_MASK) ==
		    TAZTERM_KEY_SPLIT_SIDE && tazterm_keys_lookup(k, GDK_KEY_s,
		    GDK_CONTROL_MASK | GDK_MOD1_MASK) == TAZTERM_KEY_SPLIT_SIDE,
		    "keys rebound");
		check(tazterm_keys_lookup(k, GDK_KEY_E, GDK_CONTROL_MASK |
		    GDK_SHIFT_MASK) == -1, "keys old binding gone");
		tazterm_keys_free(k);
		g_key_file_free(kf);
	}

	/* JSON strings: quotes, backslash, controls escaped, UTF-8 kept. */
	{
		GString *js = g_string_new(NULL);

		tazterm_json_string(js, "a\"b\\c\nd\t\033é");
		check(!strcmp(js->str, "\"a\\\"b\\\\c\\nd\\t\\u001bé\""),
		    "json escape");
		g_string_truncate(js, 0);
		tazterm_json_string(js, NULL);
		check(!strcmp(js->str, "null"), "json null");
		g_string_truncate(js, 0);
		tazterm_json_string(js, "x\377y");
		check(g_utf8_validate(js->str, -1, NULL), "json invalid utf-8");
		g_string_free(js, TRUE);
	}

	return fails;
}
