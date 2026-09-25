/*
 * TazTerm - light GTK3/VTE terminal for SliTaz, made for AI agents
 * Copyright (C) 2026 SliTaz GNU/Linux - BSD License, see COPYING
 *
 * Engineer: Christophe Lincoln <pankso@slitaz.org>
 * Coding assistants: OpenCode & Claude
 */
/* tazterm-keys.c — Shortcut table and [keys] parsing. */
#include "tazterm-keys.h"

#include <glib/gi18n.h>
#include <string.h>

#define MODS (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK | \
    GDK_MOD4_MASK)  /* Super: Mod4 on X11 */
#define MAX_BINDINGS 4

/* Names in [keys], default bindings, what they do, help group (0
 * panes, 1 terminal, 2 agents). Several defaults cover keypads and
 * layouts where + needs Shift. */
static const struct {
	const char *name;
	const char *def;
	const char *desc;
	int group;
} actions[TAZTERM_KEY_N] = {
	[TAZTERM_KEY_COPY] = { "copy", "Ctrl+Shift+C",
	    N_("Copy"), 1 },
	[TAZTERM_KEY_PASTE] = { "paste", "Ctrl+Shift+V",
	    N_("Paste"), 1 },
	[TAZTERM_KEY_QUIT] = { "quit", "Ctrl+Shift+Q",
	    N_("Close the window"), 1 },
	[TAZTERM_KEY_FIND] = { "find", "Ctrl+Shift+F",
	    N_("Find in the scrollback"), 1 },
	[TAZTERM_KEY_SPLIT_SIDE] = { "split_side", "Ctrl+Shift+E",
	    N_("Split side by side"), 0 },
	[TAZTERM_KEY_SPLIT_STACKED] = { "split_stacked", "Ctrl+Shift+O",
	    N_("Split stacked"), 0 },
	[TAZTERM_KEY_CLOSE] = { "close", "Ctrl+Shift+W",
	    N_("Close the pane"), 0 },
	[TAZTERM_KEY_ZOOM_PANE] = { "zoom_pane", "Ctrl+Shift+Z",
	    N_("Zoom the pane (again: restore)"), 0 },
	[TAZTERM_KEY_EQUALIZE] = { "equalize", "Ctrl+Shift+B",
	    N_("Equalize pane sizes"), 0 },
	[TAZTERM_KEY_FOCUS_LEFT] = { "focus_left", "Alt+Left Alt+KP_Left",
	    N_("Go to the pane on the left"), 0 },
	[TAZTERM_KEY_FOCUS_RIGHT] = { "focus_right", "Alt+Right Alt+KP_Right",
	    N_("Go to the pane on the right"), 0 },
	[TAZTERM_KEY_FOCUS_UP] = { "focus_up", "Alt+Up Alt+KP_Up",
	    N_("Go to the pane above"), 0 },
	[TAZTERM_KEY_FOCUS_DOWN] = { "focus_down", "Alt+Down Alt+KP_Down",
	    N_("Go to the pane below"), 0 },
	[TAZTERM_KEY_RESIZE_LEFT] = { "resize_left", "Alt+Shift+Left",
	    N_("Move the pane border left"), 0 },
	[TAZTERM_KEY_RESIZE_RIGHT] = { "resize_right", "Alt+Shift+Right",
	    N_("Move the pane border right"), 0 },
	[TAZTERM_KEY_RESIZE_UP] = { "resize_up", "Alt+Shift+Up",
	    N_("Move the pane border up"), 0 },
	[TAZTERM_KEY_RESIZE_DOWN] = { "resize_down", "Alt+Shift+Down",
	    N_("Move the pane border down"), 0 },
	[TAZTERM_KEY_PROMPT_PREV] = { "prompt_prev", "Ctrl+Shift+Up",
	    N_("Previous prompt (bash)"), 1 },
	[TAZTERM_KEY_PROMPT_NEXT] = { "prompt_next", "Ctrl+Shift+Down",
	    N_("Next prompt (bash)"), 1 },
	[TAZTERM_KEY_AGENT] = { "agent", "Ctrl+Shift+A",
	    N_("Open an agent split"), 2 },
	[TAZTERM_KEY_SEND] = { "send_to_agent", "Ctrl+Shift+T",
	    N_("Send the selection to the agent"), 2 },
	[TAZTERM_KEY_EXPLAIN] = { "explain", "Ctrl+Shift+X",
	    N_("Send the last command to the agent"), 2 },
	[TAZTERM_KEY_SCROLLBACK] = { "copy_scrollback", "Ctrl+Shift+S",
	    N_("Copy the scrollback"), 2 },
	[TAZTERM_KEY_FONT_BIGGER] = { "font_bigger",
	    "Ctrl+plus Ctrl+Shift+plus Ctrl+equal Ctrl+KP_Add",
	    N_("Bigger font"), 1 },
	[TAZTERM_KEY_FONT_SMALLER] = { "font_smaller",
	    "Ctrl+minus Ctrl+KP_Subtract",
	    N_("Smaller font"), 1 },
	[TAZTERM_KEY_FONT_RESET] = { "font_reset", "Ctrl+0 Ctrl+KP_0",
	    N_("Default font size"), 1 },
	[TAZTERM_KEY_FULLSCREEN] = { "fullscreen", "F11",
	    N_("Fullscreen"), 1 },
	[TAZTERM_KEY_HELP] = { "help", "F1",
	    N_("Keyboard shortcuts (this help)"), 1 },
};

typedef struct {
	guint keyval;      /* lower case */
	GdkModifierType mods;
} Binding;

struct TaztermKeys {
	Binding b[TAZTERM_KEY_N][MAX_BINDINGS];
	int n[TAZTERM_KEY_N];
	char *text[TAZTERM_KEY_N];  /* first binding as written, or NULL */
};

/* "Ctrl+Shift+E" -> keyval e, Ctrl|Shift. FALSE when malformed. */
static gboolean
binding_parse(const char *s, Binding *out)
{
	char **part;
	guint i, n;
	gboolean ok = TRUE;

	part = g_strsplit(s, "+", -1);
	n = g_strv_length(part);
	/* "Ctrl++": the key is '+'. */
	if (n >= 2 && !*part[n - 1] && !*part[n - 2]) {
		g_free(part[n - 2]);
		part[n - 2] = g_strdup("plus");
		n--;
	}
	out->mods = 0;
	for (i = 0; i + 1 < n && ok; i++) {
		if (!g_ascii_strcasecmp(part[i], "Ctrl") ||
		    !g_ascii_strcasecmp(part[i], "Control"))
			out->mods |= GDK_CONTROL_MASK;
		else if (!g_ascii_strcasecmp(part[i], "Shift"))
			out->mods |= GDK_SHIFT_MASK;
		else if (!g_ascii_strcasecmp(part[i], "Alt"))
			out->mods |= GDK_MOD1_MASK;
		else if (!g_ascii_strcasecmp(part[i], "Super"))
			out->mods |= GDK_MOD4_MASK;
		else
			ok = FALSE;
	}
	out->keyval = ok && n ? gdk_keyval_from_name(part[n - 1]) :
	    GDK_KEY_VoidSymbol;
	/* Single letters: "e" and "E" are the same key. */
	if (out->keyval == GDK_KEY_VoidSymbol && ok && n &&
	    strlen(part[n - 1]) == 1)
		out->keyval = gdk_unicode_to_keyval(
		    (guchar) part[n - 1][0]);
	g_strfreev(part);
	if (!ok || out->keyval == GDK_KEY_VoidSymbol || !out->keyval)
		return FALSE;
	out->keyval = gdk_keyval_to_lower(out->keyval);
	return TRUE;
}

/* Bindings of one action from a space separated list. */
static void
action_set(TaztermKeys *keys, int a, const char *list)
{
	char **item;
	guint i;

	keys->n[a] = 0;
	item = g_strsplit_set(list, " \t", -1);
	for (i = 0; item[i]; i++) {
		Binding b;

		if (!*item[i])
			continue;
		if (!binding_parse(item[i], &b)) {
			g_warning("tazterm: [keys] %s: bad key '%s'",
			    actions[a].name, item[i]);
			continue;
		}
		if (!keys->text[a])
			keys->text[a] = g_strdup(item[i]);
		if (keys->n[a] < MAX_BINDINGS)
			keys->b[a][keys->n[a]++] = b;
	}
	g_strfreev(item);
}

TaztermKeys *
tazterm_keys_new(GKeyFile *kf)
{
	TaztermKeys *keys;
	int a;

	keys = g_new0(TaztermKeys, 1);
	for (a = 0; a < TAZTERM_KEY_N; a++) {
		char *s = kf ? g_key_file_get_string(kf, "keys",
		    actions[a].name, NULL) : NULL;

		action_set(keys, a, s ? s : actions[a].def);
		g_free(s);
	}
	return keys;
}

void
tazterm_keys_free(TaztermKeys *keys)
{
	int a;

	if (!keys)
		return;
	for (a = 0; a < TAZTERM_KEY_N; a++)
		g_free(keys->text[a]);
	g_free(keys);
}

int
tazterm_keys_lookup(const TaztermKeys *keys, guint keyval,
    GdkModifierType state)
{
	int a, i;

	if (!keys)
		return -1;
	keyval = gdk_keyval_to_lower(keyval);
	state &= MODS;
	for (a = 0; a < TAZTERM_KEY_N; a++)
		for (i = 0; i < keys->n[a]; i++)
			if (keys->b[a][i].keyval == keyval &&
			    keys->b[a][i].mods == state)
				return a;
	return -1;
}

char *
tazterm_keys_default_conf(void)
{
	GString *out;
	int a;

	out = g_string_new("\n[keys]\n"
	    "# Shortcuts: Ctrl, Shift, Alt, Super + a GDK key name, several\n"
	    "# separated by spaces. Empty value: the key goes to the\n"
	    "# application (e.g. focus_left= gives Alt+Left back to it).\n");
	for (a = 0; a < TAZTERM_KEY_N; a++)
		g_string_append_printf(out, "#%s=%s\n", actions[a].name,
		    actions[a].def);
	return g_string_free(out, FALSE);
}

const char *
tazterm_keys_text(const TaztermKeys *keys, int action)
{
	if (!keys || action < 0 || action >= TAZTERM_KEY_N ||
	    !keys->text[action])
		return "";
	return keys->text[action];
}

const char *
tazterm_keys_desc(int action)
{
	if (action < 0 || action >= TAZTERM_KEY_N)
		return "";
	return _(actions[action].desc);
}

int
tazterm_keys_group(int action)
{
	return action >= 0 && action < TAZTERM_KEY_N ?
	    actions[action].group : 0;
}

const char *
tazterm_keys_group_title(int group)
{
	static const char *const titles[TAZTERM_KEY_GROUPS] = {
		N_("Panes"), N_("Terminal"), N_("Agents")
	};

	return group >= 0 && group < TAZTERM_KEY_GROUPS ?
	    _(titles[group]) : "";
}

/* Not configurable: mouse, and keys that only act in one state. */
static const struct {
	const char *key;
	const char *desc;
} fixed[] = {
	{ N_("Ctrl+click"), N_("Open a URL or file:line") },
	{ N_("Right click"), N_("Menu") },
	{ N_("Click on exit N"), N_("Send the failed command to the agent") },
	{ N_("Enter"), N_("Restart an agent that quit (in its pane)") },
	{ N_("Esc"), N_("Close the search bar") },
};

gboolean
tazterm_keys_fixed(int i, const char **key, const char **desc)
{
	if (i < 0 || i >= (int) G_N_ELEMENTS(fixed))
		return FALSE;
	*key = _(fixed[i].key);
	*desc = _(fixed[i].desc);
	return TRUE;
}

char *
tazterm_keys_help(const TaztermKeys *keys)
{
	GString *out;
	const char *key, *desc;
	int g, a;

	out = g_string_new(NULL);
	for (g = 0; g < TAZTERM_KEY_GROUPS; g++) {
		g_string_append_printf(out, "%s%s\n", g ? "\n" : "",
		    tazterm_keys_group_title(g));
		for (a = 0; a < TAZTERM_KEY_N; a++) {
			const char *t = tazterm_keys_text(keys, a);

			if (actions[a].group != g || !*t)
				continue;
			g_string_append_printf(out, "  %-20s %s\n", t,
			    tazterm_keys_desc(a));
		}
	}
	g_string_append_printf(out, "\n%s\n", _("Mouse and fixed keys"));
	for (a = 0; tazterm_keys_fixed(a, &key, &desc); a++)
		g_string_append_printf(out, "  %-20s %s\n", key, desc);
	return g_string_free(out, FALSE);
}
