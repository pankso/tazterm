/* unit.c — Pure functions, no display: redaction, paste sanitizer,
 * command parsing. Exit status = number of failures. */
#include <stdio.h>
#include <string.h>

#include "tazterm-ai.h"
#include "tazterm-config.h"
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

	return fails;
}
