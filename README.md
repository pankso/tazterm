# TazTerm — terminal GTK3/VTE pour SliTaz, avec splits et copilote IA

Terminal moderne construit depuis zéro en **C + GTK3 + VTE** :
léger comme la stack SliTaz actuelle, splits multi-shell dans une seule
fenêtre (`GtkPaned` récursif), et pragmatiquement **AI friendly**
(split agent, barre d'état par panneau, blocs de commande bash,
`tazterm ctl` : l'agent lit les panneaux, en lecture seule).

État : **0.6** — collage sûr, capture du vrai scrollback, `tazterm ctl`,
blocs de commande, barre d'état, liens `Ctrl+clic`, compatibilité xterm
(`-e`), `make check`. Historique E1→E5 : `BOOTSTRAP.md`.

## Install

```sh
# Depuis le wok (recette dans /home/slitaz/wok/tazterm)
sudo cook tazterm
sudo spk-add /home/slitaz/packages/tazterm-0.6-x86_64.tazpkg

# Ou build direct
sudo spk-add gtk+3-dev vte291-dev pkg-config gettext-tools
make && ./src/tazterm
make check            # tests (Xvfb, headless)
sudo make install     # installation live, hors tazpkg
```

## Usage

- `Ctrl+Shift+C` / `Ctrl+Shift+V` : copier / coller (panneau actif)
- `Ctrl+Shift+Q` : quitter (tous les panneaux)
- `Ctrl+Shift+E` : diviser côte à côte
- `Ctrl+Shift+O` : diviser empilés
- `Ctrl+Shift+W` : fermer le panneau courant (dernier → quitte)
- `Alt+Flèches` : focus au panneau voisin (bordure bleue = actif)
- `Ctrl+clic` sur une URL : `$BROWSER` (sinon l'application GIO par défaut) ; sur `fichier:ligne[:col]` (gcc, grep -n, traceback, agents) : ouvre l'éditeur dans un split, depuis le dossier du panneau (`[terminal] editor=`, sinon `$VISUAL`, sinon `$EDITOR` s'il tourne en terminal, sinon `vi`)
- Fermer un panneau (`Ctrl+Shift+W`) ou la fenêtre (`Ctrl+Shift+Q`, bouton du WM) où un programme tourne encore (agent, vim, build) demande confirmation (`[terminal] confirm_close=false` pour désactiver)
- Barre d'état sous chaque panneau : `id · rôle · processus · dossier` et, à droite, l'activité (● actif / ● travaille pour un agent, en attente · 3m, ● attend une réponse après un BEL, terminé (code N)). `[terminal] status_bar=false` pour la masquer
- Un panneau en arrière-plan qui sonne (BEL : agent qui attend une permission ou a fini, `make; printf '\a'`) prend un contour orange jusqu'à ce qu'on y aille ; fenêtre sans focus → urgence (barre des tâches). Claude Code : `/config` → notifications = `terminal_bell`
- `Ctrl+Shift+F` : afficher / masquer la recherche (suit le panneau actif)
- `F11` ou clic droit → `Plein écran` : basculer le plein écran
- `Ctrl+Shift+A` : ouvrir un split agent IA (agent par défaut)
- `Ctrl+Shift+T` ou clic droit → `Envoyer à l'agent` : coller la sélection du panneau courant dans le panneau agent (sans Entrée : on ajoute sa question puis on valide)
- Clic droit → `Ouvrir un split agent (…)` : défaut, ou `Split agent : claude`
  / `Split agent : navette` pour les autres agents détectés
- `Ctrl+Shift+S` : copier le scrollback (`capture_lines` dernières lignes, presse-papier seulement, rien sur disque)
- `Ctrl+Shift+X` : expliquer la dernière erreur (`explain_lines` dernières lignes, collées dans le panneau agent sans Entrée + presse-papier)
- Collage (`Ctrl+Shift+V`) : caractères de contrôle retirés ; un collage multi-ligne dans un shell sans bracketed paste (busybox ash) demande confirmation, car chaque ligne s'y exécuterait

## Agents IA : opencode, claude, navette

Détection automatique au PATH (`agents: ...` dans `TAZTERM_DEBUG=1`).
`Ctrl+Shift+A` ouvre un split et y lance l'agent par défaut
(config `[ai] agent=auto|opencode|claude|navette`, `TAZTERM_AGENT_CMD`
force la commande — pratique pour tester). La capture lit le texte brut
du pty : aucun changement requis côté agent, `navette` (one-shot comme
TUI) marche tel quel.

## tazterm ctl : l'agent lit les panneaux

Chaque panneau exporte `TAZTERM_SOCKET`, `TAZTERM_PANE` (id du panneau)
et `TERM_PROGRAM=tazterm`. Tout agent qui sait lancer une commande
(claude, opencode, navette) peut lire ce que l'utilisateur voit, sans
copier-coller :

```sh
tazterm ctl ls              # id, rôle (shell/agent/cmd), état, processus, cwd, titre
tazterm ctl read            # 200 dernières lignes du panneau d'où vient l'utilisateur
tazterm ctl read -p 1 -n 50 # panneau 1, 50 lignes (-a : tout le scrollback)
tazterm ctl notify "fini"   # contour orange du panneau + fenêtre en urgence
tazterm ctl read -l         # dernière commande : commande, sortie, [exit N, durée]
tazterm ctl blocks          # commandes récentes : n°, code, secondes, commande
tazterm ctl wait -t 600     # attend la fin de la prochaine commande, sort avec son code
```

**Blocs de commande (panneaux bash)** : tazterm lance bash avec
`--rcfile ~/.config/tazterm/bash-integration.sh`, qui source `~/.bashrc`
puis marque chaque prompt (OSC 6 avec un jeton par panneau : un `cat`
de fichier ne peut pas forger de faux blocs). Donne `read -l`, `blocks`,
`wait`, `Ctrl+Shift+X` sur la dernière commande exacte,
`Ctrl+Shift+↑/↓` pour sauter de prompt en prompt, et « ✗ code N » /
« en cours · 2m » dans la barre d'état. busybox ash n'a aucun hook de
prompt : pas de blocs, `read -n` reste disponible.

- **Lecture seule** : un agent ne peut jamais taper dans un panneau.
  La sortie terminal n'est pas fiable (curl, logs, README cloné) et un
  agent qui la lit peut être manipulé : écrire reste un geste humain.
- Secrets masqués (`[REDACTED]` : clés privées, jetons `sk-`/`ghp_`/`AKIA`…,
  `password=`…) ; `[ai] redact=false` pour désactiver.
- Socket `$XDG_RUNTIME_DIR/tazterm/PID.sock` (sinon `~/.cache/tazterm/`),
  dossier 0700, uid du pair vérifié, supprimé à la fermeture.
- Hors d'un panneau (agent lancé ailleurs) : `ctl` trouve le seul
  tazterm lancé, ou demande `TAZTERM_SOCKET` s'il y en a plusieurs.

À mettre dans le `CLAUDE.md` / `AGENTS.md` d'un projet :

```
If TERM_PROGRAM=tazterm, run `tazterm ctl guide` once: it explains how
to read the user's panes instead of asking them to paste output.
```

`tazterm ctl guide` affiche le mode d'emploi pour agents (markdown,
anglais), intégré au binaire donc toujours à jour.

## Raccourcis de base

- `Ctrl+Shift+C` / `Ctrl+Shift+V` : copier / coller
- `Ctrl+Shift+Q` : quitter
- `Ctrl+Shift+F` : afficher / masquer la recherche (Entrée = suivant, Shift+Entrée = précédent)
- Fermer la recherche : `Échap` (dans le champ comme dans le terminal), bouton ✕, ou `Ctrl+Shift+F`
- `Ctrl+Plus` / `Ctrl+Moins` / `Ctrl+0` : zoom avant / arrière / taille normale
- Clic droit : menu complet (copier, coller, tout sélectionner, recherche, zoom)
- `exit` dans le shell : ferme la fenêtre
- Shell : `shell=auto` par défaut = bash s'il est installé (blocs de commande, `ctl read -l`/`wait`, codes retour), sinon `/bin/sh` (busybox ash) ; `-s SHELL` ou `TAZTERM_SHELL` pour forcer
- Options : `-d DOSSIER`, `-s SHELL`, `-v`, et compatibles xterm (wrapper SliTaz `terminal`) : `-T TITRE`, `-geometry 80x24[+X+Y]`, `-hold`, `-e COMMANDE ARGS…` (tout ce qui suit `-e`, ou une seule chaîne `"htop -d 5"`), `--class`/`--name` (WM_CLASS)
- Debug : `TAZTERM_DEBUG=1 tazterm` (logs spawn, zoom, recherche sur stderr)

## Config : `~/.config/tazterm/tazterm.conf` (créé avec les défauts)

```ini
[terminal]
font=Monospace 12
shell=auto
scrollback_lines=10000
foreground=#e6e8ed
background=#1c1e22
#working_directory=/home/tux

[ai]
agent=auto
explain_lines=200
capture_lines=2000
redact=true
```
- Shell : `shell=auto` par défaut = bash s'il est installé (blocs de commande, `ctl read -l`/`wait`, codes retour), sinon `/bin/sh` (busybox ash) ; `-s SHELL` ou `TAZTERM_SHELL` pour forcer

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
    ├── tazterm-ai.[ch]      <- (E4) opencode/claude, capture, redaction
    └── tazterm-ctl.[ch]     <- tazterm ctl : socket + client (lecture seule)
```

## License

GPL-3.0-or-later.
