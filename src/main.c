/* main.c — Options --working-directory / --shell / --version.
 * Shell precedence: --shell > TAZTERM_SHELL > config > /bin/sh.
 * Directory precedence: --working-directory > config > $HOME. */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include <glib/gi18n.h>

#include "tazterm-config.h"
#include "tazterm-term.h"
#include "tazterm-window.h"

#define TAZTERM_VERSION "0.5"

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "tazterm"
#endif
#ifndef LOCALEDIR
#define LOCALEDIR "/usr/share/locale"
#endif

int
main(int argc, char *argv[])
{
	GtkWidget *win;
	TaztermConfig *cfg;
	GOptionContext *ctx;
	GError *err = NULL;
	char *opt_workdir = NULL;
	char *opt_shell = NULL;
	gboolean opt_version = FALSE;
	GOptionEntry entries[] = {
		{ "working-directory", 'd', 0, G_OPTION_ARG_STRING,
		  &opt_workdir, N_("Dossier de demarrage du shell"),
		  N_("DOSSIER") },
		{ "shell", 's', 0, G_OPTION_ARG_STRING,
		  &opt_shell, N_("Shell a lancer (defaut : /bin/sh)"),
		  N_("SHELL") },
		{ "version", 'v', 0, G_OPTION_ARG_NONE,
		  &opt_version, N_("Afficher la version"), NULL },
		{ NULL }
	};

	/* Shell integration hook: emit OSC 7 and exit, no GTK. */
	if (argc >= 2 && strcmp(argv[1], "--osc7") == 0)
		return tazterm_term_osc7_emit() ? 0 : 1;

	/* SliTaz has no accessibility bus: cut the at-spi bridge to avoid
	 * the "Couldn't connect to accessibility bus" warning. */
	setenv("NO_AT_BRIDGE", "1", 0);

	setlocale(LC_ALL, "");
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	ctx = g_option_context_new(_("- terminal leger GTK3/VTE"));
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
		g_free(opt_workdir);
		g_free(opt_shell);
		return 0;
	}

	if (!gtk_init_check(&argc, &argv)) {
		g_printerr("tazterm: cannot open display\n");
		g_free(opt_workdir);
		g_free(opt_shell);
		return 1;
	}

	cfg = tazterm_config_load();

	/* Window/taskbar icon (openbox decor): the menu icon comes from
	 * the .desktop Icon= via hicolor, but the X window needs
	 * _NET_WM_ICON, which GTK only sets from the window icon. */
	g_set_prgname("tazterm");
	gtk_window_set_default_icon_name("tazterm");

	win = tazterm_window_new(cfg, opt_shell, opt_workdir);
	gtk_widget_show_all(win);

	gtk_main();

	tazterm_config_free(cfg);
	g_free(opt_workdir);
	g_free(opt_shell);
	return 0;
}
