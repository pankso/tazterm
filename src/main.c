/* main.c — Options --working-directory / --shell / --version.
 * Shell precedence: --shell > TAZTERM_SHELL > config > /bin/sh.
 * Directory precedence: --working-directory > config > $HOME. */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include <glib/gi18n.h>

#include "tazterm-config.h"
#include "tazterm-ctl.h"
#include "tazterm-term.h"
#include "tazterm-window.h"

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "tazterm"
#endif
#ifndef LOCALEDIR
#define LOCALEDIR "/usr/share/locale"
#endif

/* xterm-style single-dash long options (-geometry, -title, -hold,
 * -help), as passed by SliTaz's /usr/bin/terminal wrapper: GOption only
 * knows the double-dash form. */
static void
argv_normalize(int argc, char **argv)
{
	static const char *const longs[] = {
		"-geometry", "-title", "-hold", "-help", "-version", NULL
	};
	int i, j;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-e"))
			break;
		for (j = 0; longs[j]; j++)
			if (!strcmp(argv[i], longs[j]))
				argv[i] = g_strconcat("-", longs[j], NULL);
	}
}

/* -e COMMAND [ARGS...]: everything after -e is the command (xterm).
 * A single argument with spaces ("htop -d 5", sakura/lxterminal
 * habit) goes through tazterm_command_argv. Cuts argv at -e. */
static char **
argv_take_command(int *argc, char **argv, TaztermConfig *cfg)
{
	int i, n;
	char **cmd;

	for (i = 1; i < *argc; i++)
		if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--command") ||
		    !strcmp(argv[i], "-x"))
			break;
	if (i >= *argc)
		return NULL;
	n = *argc - i - 1;
	*argc = i;
	if (n <= 0)
		return NULL;
	if (n == 1 && strchr(argv[i + 1], ' '))
		return tazterm_command_argv(cfg, argv[i + 1]);
	cmd = g_new0(char *, n + 1);
	for (i = 0; i < n; i++)
		cmd[i] = g_strdup(argv[*argc + 1 + i]);
	return cmd;
}

int
main(int argc, char *argv[])
{
	GtkWidget *win;
	TaztermConfig *cfg;
	GOptionContext *ctx;
	GError *err = NULL;
	TaztermWinOpts opts = { 0 };
	char *opt_workdir = NULL;
	char *opt_shell = NULL;
	char *opt_title = NULL;
	char *opt_geometry = NULL;
	gboolean opt_hold = FALSE;
	gboolean opt_version = FALSE;
	char **command;
	GOptionEntry entries[] = {
		{ "working-directory", 'd', 0, G_OPTION_ARG_STRING,
		  &opt_workdir, N_("Dossier de demarrage du shell"),
		  N_("DOSSIER") },
		{ "shell", 's', 0, G_OPTION_ARG_STRING,
		  &opt_shell, N_("Shell a lancer (defaut : /bin/sh)"),
		  N_("SHELL") },
		{ "title", 'T', 0, G_OPTION_ARG_STRING,
		  &opt_title, N_("Titre de la fenetre"), N_("TITRE") },
		{ "geometry", 0, 0, G_OPTION_ARG_STRING,
		  &opt_geometry, N_("Taille en caracteres (80x24[+X+Y])"),
		  N_("GEOMETRIE") },
		{ "hold", 'H', 0, G_OPTION_ARG_NONE,
		  &opt_hold, N_("Garder la fenetre apres la fin de -e"), NULL },
		{ "version", 'v', 0, G_OPTION_ARG_NONE,
		  &opt_version, N_("Afficher la version"), NULL },
		{ NULL }
	};

	/* Shell integration hook: emit OSC 7 and exit, no GTK. */
	if (argc >= 2 && strcmp(argv[1], "--osc7") == 0)
		return tazterm_term_osc7_emit() ? 0 : 1;
	/* Agent/script client: talks to a running tazterm, no GTK. */
	if (argc >= 2 && strcmp(argv[1], "ctl") == 0)
		return tazterm_ctl_client(argc - 2, argv + 2);

	/* SliTaz has no accessibility bus: cut the at-spi bridge to avoid
	 * the "Couldn't connect to accessibility bus" warning. */
	setenv("NO_AT_BRIDGE", "1", 0);

	setlocale(LC_ALL, "");
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	/* Before parsing: --name / --class (GTK options) override it. */
	g_set_prgname("tazterm");
	cfg = tazterm_config_load();
	argv_normalize(argc, argv);
	command = argv_take_command(&argc, argv, cfg);

	ctx = g_option_context_new(_("[-e COMMANDE [ARGS...]] - terminal "
	    "leger GTK3/VTE"));
	g_option_context_add_main_entries(ctx, entries, GETTEXT_PACKAGE);
	/* FALSE: no display needed to parse (-v works over SSH);
	 * gtk_init_check() opens it below. */
	g_option_context_add_group(ctx, gtk_get_option_group(FALSE));
	if (!g_option_context_parse(ctx, &argc, &argv, &err)) {
		g_printerr("tazterm: %s\n", err->message);
		g_clear_error(&err);
		g_option_context_free(ctx);
		return 1;
	}
	g_option_context_free(ctx);

	if (opt_version) {
		g_print("tazterm %s (gtk %d.%d.%d, vte %u.%u.%u)\n",
		    TAZTERM_VERSION,
		    gtk_get_major_version(), gtk_get_minor_version(),
		    gtk_get_micro_version(), vte_get_major_version(),
		    vte_get_minor_version(), vte_get_micro_version());
		return 0;
	}

	if (!gtk_init_check(&argc, &argv)) {
		g_printerr("tazterm: cannot open display\n");
		return 1;
	}

	/* Window/taskbar icon (openbox decor): the menu icon comes from
	 * the .desktop Icon= via hicolor, but the X window needs
	 * _NET_WM_ICON, which GTK only sets from the window icon. */
	gtk_window_set_default_icon_name("tazterm");

	opts.shell = opt_shell;
	opts.workdir = opt_workdir;
	opts.command = command;
	opts.title = opt_title;
	opts.geometry = opt_geometry;
	opts.hold = opt_hold;
	win = tazterm_window_new(cfg, &opts);
	gtk_widget_show_all(win);

	gtk_main();

	tazterm_config_free(cfg);
	g_strfreev(command);
	g_free(opt_workdir);
	g_free(opt_shell);
	g_free(opt_title);
	g_free(opt_geometry);
	return 0;
}
