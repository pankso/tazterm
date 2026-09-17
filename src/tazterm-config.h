/* tazterm-config.h — E2: configuration ~/.config/tazterm/tazterm.conf. */
#ifndef TAZTERM_CONFIG_H
#define TAZTERM_CONFIG_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct {
	char *font_desc;   /* ex. "Monospace 10" */
	char *shell;       /* defaut : /bin/sh (BusyBox ash) */
	char *workdir;     /* NULL = $HOME */
	long scrollback;   /* lignes, defaut 10000 */
	GdkRGBA foreground;
	GdkRGBA background;
	gboolean fg_set;
	gboolean bg_set;
	char *ai_agent;    /* "auto" ou opencode|claude|navette */
	int ai_explain_lines;  /* defaut 200 */
	int ai_capture_lines;  /* defaut 2000 */
} TaztermConfig;

/* Charge la config (fichier cree avec les defauts s'il manque).
 * Ne retourne jamais NULL (defauts en cas d'echec). */
TaztermConfig *tazterm_config_load(void);

void tazterm_config_free(TaztermConfig *cfg);

/* Chemin du fichier de config (a liberer avec g_free). */
char *tazterm_config_path(void);

G_END_DECLS

#endif /* TAZTERM_CONFIG_H */
