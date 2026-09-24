/* tazterm-config.h — ~/.config/tazterm/tazterm.conf configuration. */
#ifndef TAZTERM_CONFIG_H
#define TAZTERM_CONFIG_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct {
	char *font_desc;   /* e.g. "Monospace 12" */
	char *shell;       /* default: /bin/sh (BusyBox ash) */
	char *workdir;     /* NULL = $HOME */
	long scrollback;   /* lines, default 10000 */
	GdkRGBA foreground;
	GdkRGBA background;
	gboolean fg_set;
	gboolean bg_set;
	gboolean status_bar;   /* per-pane status line, default TRUE */
	char *ai_agent;    /* "auto" or opencode|claude|navette */
	int ai_explain_lines;  /* default 200 */
	int ai_capture_lines;  /* default 2000 */
	gboolean ai_redact;    /* mask secrets sent to agents, default TRUE */
} TaztermConfig;

/* Load the config (file created with defaults when missing).
 * Never returns NULL (defaults on failure). */
TaztermConfig *tazterm_config_load(void);

void tazterm_config_free(TaztermConfig *cfg);

/* Config file path (free with g_free). */
char *tazterm_config_path(void);

G_END_DECLS

#endif /* TAZTERM_CONFIG_H */
