/* tazkey: XTest key combo + text typing for GUI tests.
 * Usage:
 *   tazkey combo ctrl+shift f   (modifiers: ctrl shift alt, key = keysym name)
 *   tazkey key Return            (single key, no modifiers)
 *   tazkey type "hello world"    (types ascii text into focused window)
 * Temp test tool (like clicker/superkey). */
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *d;

static int
kc(const char *name)
{
	KeySym ks = XStringToKeysym(name);
	int code;

	if (ks == NoSymbol) {
		fprintf(stderr, "tazkey: unknown keysym '%s'\n", name);
		exit(1);
	}
	code = XKeysymToKeycode(d, ks);
	if (!code) {
		fprintf(stderr, "tazkey: no keycode for '%s'\n", name);
		exit(1);
	}
	return code;
}

static void
press(int code, int down)
{
	XTestFakeKeyEvent(d, code, down, CurrentTime);
	XFlush(d);
	usleep(15000);
}

static int
modcode(const char *m)
{
	if (!strcmp(m, "ctrl"))
		return kc("Control_L");
	if (!strcmp(m, "shift"))
		return kc("Shift_L");
	if (!strcmp(m, "alt"))
		return kc("Alt_L");
	fprintf(stderr, "tazkey: unknown modifier '%s'\n", m);
	exit(1);
	return 0;
}

static void
do_combo(char *spec, const char *key)
{
	int mods[8];
	int nmods = 0;
	char *tok;
	char *save = NULL;
	int mainkc;
	int i;

	for (tok = strtok_r(spec, "+", &save); tok;
	    tok = strtok_r(NULL, "+", &save)) {
		if (!strcmp(tok, "none"))
			continue;
		mods[nmods++] = modcode(tok);
	}
	mainkc = kc(key);
	for (i = 0; i < nmods; i++)
		press(mods[i], 1);
	press(mainkc, 1);
	press(mainkc, 0);
	for (i = nmods - 1; i >= 0; i--)
		press(mods[i], 0);
}

static void
do_key(const char *key)
{
	int code = kc(key);

	press(code, 1);
	press(code, 0);
}

static const char *
punct_name(unsigned char c)
{
	switch (c) {
	case '/': return "slash";
	case '-': return "minus";
	case '.': return "period";
	case ',': return "comma";
	case ':': return "colon";
	case ';': return "semicolon";
	case '_': return "underscore";
	case '=': return "equal";
	case '+': return "plus";
	case '*': return "asterisk";
	case '?': return "question";
	case '!': return "exclam";
	case '@': return "at";
	case '#': return "numbersign";
	case '$': return "dollar";
	case '%': return "percent";
	case '&': return "ampersand";
	case '(': return "parenleft";
	case ')': return "parenright";
	case '[': return "bracketleft";
	case ']': return "bracketright";
	case '{': return "braceleft";
	case '}': return "braceright";
	case '\'': return "apostrophe";
	case '"': return "quotedbl";
	case '\\': return "backslash";
	case '|': return "bar";
	case '~': return "asciitilde";
	case '^': return "asciicircum";
	case '`': return "grave";
	case '<': return "less";
	case '>': return "greater";
	default: return NULL;
	}
}

static void
do_type(const char *text)
{
	const unsigned char *p;
	int shift = kc("Shift_L");

	for (p = (const unsigned char *) text; *p; p++) {
		char name[2] = { 0, 0 };
		const char *ksname = name;
		int code;
		int need_shift = 0;

		if (*p == ' ') {
			do_key("space");
			continue;
		}
		if (isupper(*p)) {
			name[0] = (char) tolower(*p);
			need_shift = 1;
		} else if (islower(*p) || isdigit(*p)) {
			name[0] = (char) *p;
		} else {
			ksname = punct_name(*p);
			if (!ksname) {
				fprintf(stderr,
				    "tazkey: cannot type 0x%02x\n", *p);
				exit(1);
			}
		}
		code = kc(ksname);
		if (need_shift)
			press(shift, 1);
		press(code, 1);
		press(code, 0);
		if (need_shift)
			press(shift, 0);
	}
}

int
main(int argc, char **argv)
{
	d = XOpenDisplay(NULL);
	if (!d) {
		fprintf(stderr, "tazkey: no display\n");
		return 1;
	}
	if (argc < 3) {
		fprintf(stderr,
		    "usage: tazkey combo MODS KEY | tazkey key KEY | tazkey type TEXT\n");
		return 1;
	}
	if (!strcmp(argv[1], "combo") && argc == 4)
		do_combo(argv[2], argv[3]);
	else if (!strcmp(argv[1], "key") && argc == 3)
		do_key(argv[2]);
	else if (!strcmp(argv[1], "type") && argc == 3)
		do_type(argv[2]);
	else {
		fprintf(stderr, "tazkey: bad args\n");
		return 1;
	}
	XCloseDisplay(d);
	return 0;
}
