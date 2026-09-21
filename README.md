# TazTerm — terminal GTK3/VTE pour SliTaz, avec splits et copilote IA

Terminal moderne construit depuis zéro en **C + GTK3 + VTE** :
léger comme la stack SliTaz actuelle, splits multi-shell dans une seule
fenêtre (`GtkPaned` récursif), et pragmatiquement **AI friendly**
(détection `opencode` / `claude` / `navette`, split agent dédié, capture
du scrollback pour expliquer une erreur).

État : **E5 — packagé** (`tazterm 0.5`, 8 fichiers, gettext + icône).
Voir `BOOTSTRAP.md` pour la feuille de route E1→E5.

## Install

```sh
# Depuis le wok (recette dans /home/slitaz/wok/tazterm)
sudo cook tazterm
sudo tazpkg install /home/slitaz/packages/tazterm-0.5-x86_64.tazpkg

# Ou build direct
sudo spk-add gtk+3-dev vte291-dev pkg-config gettext-tools
cd src && make && ./tazterm
```

## Usage (E5)

- `Ctrl+Shift+C` / `Ctrl+Shift+V` : copier / coller (panneau actif)
- `Ctrl+Shift+Q` : quitter (tous les panneaux)
- `Ctrl+Shift+E` : diviser côte à côte
- `Ctrl+Shift+O` : diviser empilés
- `Ctrl+Shift+W` : fermer le panneau courant (dernier → quitte)
- `Alt+Flèches` : focus au panneau voisin (bordure bleue = actif)
- `Ctrl+Shift+F` : afficher / masquer la recherche (suit le panneau actif)
- `F11` ou clic droit → `Plein écran` : basculer le plein écran
- `Ctrl+Shift+A` : ouvrir un split agent IA (agent par défaut)
- `Ctrl+Shift+T` ou clic droit → `Envoyer à l'agent` : envoyer la sélection du panneau courant au panneau agent (avec Entrée)
- Clic droit → `Ouvrir un split agent (…)` : défaut, ou `Split agent : claude`
  / `Split agent : navette` pour les autres agents détectés
- `Ctrl+Shift+S` : copier le scrollback (presse-papier + `/tmp/tazterm-capture-*.log`)
- `Ctrl+Shift+X` : expliquer la dernière erreur (`/tmp/tazterm-explain-*.md` + presse-papier)

## Agents IA : opencode, claude, navette

Détection automatique au PATH (`agents: ...` dans `TAZTERM_DEBUG=1`).
`Ctrl+Shift+A` ouvre un split et y lance l'agent par défaut
(config `[ai] agent=auto|opencode|claude|navette`, `TAZTERM_AGENT_CMD`
force la commande — pratique pour tester). La capture lit le texte brut
du pty : aucun changement requis côté agent, `navette` (one-shot comme
TUI) marche tel quel.

- `Ctrl+Shift+C` / `Ctrl+Shift+V` : copier / coller
- `Ctrl+Shift+Q` : quitter
- `Ctrl+Shift+F` : afficher / masquer la recherche (Entrée = suivant, Shift+Entrée = précédent)
- Fermer la recherche : `Échap` (dans le champ comme dans le terminal), bouton ✕, ou `Ctrl+Shift+F`
- `Ctrl+Plus` / `Ctrl+Moins` / `Ctrl+0` : zoom avant / arrière / taille normale
- Clic droit : menu complet (copier, coller, tout sélectionner, recherche, zoom)
- `exit` dans le shell : ferme la fenêtre
- Shell : `/bin/sh` (BusyBox ash) par défaut, `TAZTERM_SHELL=/bin/bash` pour forcer un autre
- Options : `tazterm -d DOSSIER` (dossier de démarrage), `-s SHELL`, `-v` (version)
- Debug : `TAZTERM_DEBUG=1 tazterm` (logs spawn, zoom, recherche sur stderr)

## Config : `~/.config/tazterm/tazterm.conf` (créé avec les défauts)

```ini
[terminal]
font=Monospace 12
shell=/bin/sh
scrollback_lines=10000
foreground=#e6e8ed
background=#1c1e22
#working_directory=/home/tux

[ai]
agent=auto
explain_lines=200
capture_lines=2000
```
- Shell : `/bin/sh` (BusyBox ash) par défaut, `TAZTERM_SHELL=/bin/bash` pour forcer un autre

## Layout

```
tazterm/
├── BOOTSTRAP.md   <- feuille de route par étapes
├── README.md      <- ce fichier
├── AGENTS.md      <- notes session/agent
├── receipt        <- paquet SliTaz (wok), PACKAGE=tazterm
├── data/          <- tazterm.desktop
├── po/            <- traductions (E5)
└── src/
    ├── Makefile
    ├── main.c
    ├── tazterm-window.[ch]  <- fenêtre, raccourcis, menu
    ├── tazterm-term.[ch]    <- wrapper VteTerminal : spawn, scrollback
    ├── tazterm-split.[ch]   <- (E3) arbre GtkPaned
    ├── tazterm-config.[ch]  <- (E2) ~/.config/tazterm/tazterm.conf
    └── tazterm-ai.[ch]      <- (E4) opencode/claude, capture output
```

## License

GPL-3.0-or-later.
