/* tazterm-ctl.h — `tazterm ctl`: agents read the panes.
 *
 * Server: one Unix socket per tazterm process (one window), exported
 * to panes as TAZTERM_SOCKET. Client: `tazterm ctl ls|read|notify`,
 * no GTK, usable from any agent's shell tool.
 */
#ifndef TAZTERM_CTL_H
#define TAZTERM_CTL_H

#include <gtk/gtk.h>

#include "tazterm-config.h"

G_BEGIN_DECLS

/* Open the socket and export its path to panes spawned afterwards.
 * FALSE (logged) when the socket cannot be set up safely. */
gboolean tazterm_ctl_start(TaztermConfig *cfg);

/* Pane container the requests act on (set once the split exists). */
void tazterm_ctl_set_split(GtkWidget *split);

/* Close and unlink the socket. */
void tazterm_ctl_stop(void);

/* `tazterm ctl ARGS...` entry point (argv[0] = subcommand).
 * Returns the process exit status. */
int tazterm_ctl_client(int argc, char **argv);

G_END_DECLS

#endif /* TAZTERM_CTL_H */
