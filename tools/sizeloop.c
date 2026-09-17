/* sizeloop: redraw getmaxyx-sized box every 2s, NEVER calls getch.
 * Proves whether ncurses size goes stale without input/resizeterm. */
#include <ncurses.h>
#include <unistd.h>
#include <stdio.h>

int
main(void)
{
	int n = 0;

	initscr();
	cbreak();
	noecho();
	for (;;) {
		int h, w;

		getmaxyx(stdscr, h, w);
		erase();
		mvprintw(0, 0, "iter %d size %dx%d", n++, w, h);
		box(stdscr, 0, 0);
		refresh();
		sleep(2);
	}
	return 0;
}
