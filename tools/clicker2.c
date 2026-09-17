/* clicker2: XTest click at X Y [ms] (recreation post-reboot). Temp tool. */
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	Display *d;
	int x, y, hold = 100, button = 1;

	if (argc < 3) {
		fprintf(stderr, "usage: clicker2 X Y [ms] [button]\n");
		return 1;
	}
	x = atoi(argv[1]);
	y = atoi(argv[2]);
	if (argc > 3)
		hold = atoi(argv[3]);
	if (argc > 4)
		button = atoi(argv[4]);
	d = XOpenDisplay(NULL);
	if (!d) {
		fprintf(stderr, "clicker2: no display\n");
		return 1;
	}
	XTestFakeMotionEvent(d, DefaultScreen(d), x, y, CurrentTime);
	XFlush(d);
	usleep(50000);
	XTestFakeButtonEvent(d, (unsigned) button, True, CurrentTime);
	XFlush(d);
	usleep((unsigned) hold * 1000);
	XTestFakeButtonEvent(d, (unsigned) button, False, CurrentTime);
	XFlush(d);
	XCloseDisplay(d);
	return 0;
}
