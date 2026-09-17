/* main.c — E2: options --working-directory / --shell / --version.
 * Precedence shell : --shell > TAZTERM_SHELL > config > /bin/sh.
 * Precedence dossier : --working-directory > config > $HOME. */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <locale.h>

#include <glib/gi18n.h>

#include "tazterm-config.h"
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

	/* SliTaz n'a pas de bus accessibilite : coupe le pont at-spi pour
	 * eviter le warning "Couldn't connect to accessibility bus". */
	setenv("NO_AT_BRIDGE", "1", 0);

	setlocale(LC_ALL, "");
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	ctx = g_option_context_new(_("- terminal leger GTK3/VTE"));
	g_option_context_add_main_entries(ctx, entries, GETTEXT_PACKAGE);
	g_option_context_add_group(ctx, gtk_get_option_group(TRUE));
	if (!g_option_context_parse(ctx, &argc, &argv, &err)) {
		g_printerr("tazterm: %s\n", err->message);
		g_clear_error(&err);
		g_option_context_free(ctx);
		return 1;
	}
	g_option_context_free(ctx);

	if (opt_version) {
		g_print("tazterm %s (gtk %d.%d.%d, vte %s)\n", TAZTERM_VERSION,
		    gtk_get_major_version(), gtk_get_minor_version(),
		    gtk_get_micro_version(), "2.91");
		g_free(opt_workdir);
		g_free(opt_shell);
		return 0;
	}

	cfg = tazterm_config_load();

	win = tazterm_window_new(cfg, opt_shell, opt_workdir);
	gtk_widget_show_all(win);
	gtk_widget_grab_focus(GTK_WIDGET(win));

	gtk_main();

	tazterm_config_free(cfg);
	g_free(opt_workdir);
	g_free(opt_shell);
	return 0;
}
