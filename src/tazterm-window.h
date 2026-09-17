/* tazterm-window.h — Main window + split + search + menu + zoom. */
#ifndef TAZTERM_WINDOW_H
#define TAZTERM_WINDOW_H

#include <gtk/gtk.h>

#include "tazterm-config.h"

G_BEGIN_DECLS

GtkWidget *tazterm_window_new(TaztermConfig *cfg,
    const char *shell_override, const char *workdir_override);

G_END_DECLS

#endif /* TAZTERM_WINDOW_H */
