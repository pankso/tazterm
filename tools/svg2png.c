/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
/* svg2png IN.svg OUT.png SIZE — gdk-pixbuf loader only. Temp tool. */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
	GdkPixbuf *pb;
	GError *err = NULL;
	int size;

	if (argc != 4) {
		fprintf(stderr, "usage: svg2png IN.svg OUT.png SIZE\n");
		return 1;
	}
	size = atoi(argv[3]);
	pb = gdk_pixbuf_new_from_file_at_size(argv[1], size, size, &err);
	if (!pb) {
		fprintf(stderr, "svg2png: %s\n",
		    err ? err->message : "?");
		return 1;
	}
	if (!gdk_pixbuf_save(pb, argv[2], "png", &err, NULL)) {
		fprintf(stderr, "svg2png: save: %s\n",
		    err ? err->message : "?");
		return 1;
	}
	return 0;
}
