# levr

A small, pure-C GTK3 AppImage manager. Same job as [Gearlever](https://github.com/mijorus/gearlever),
much less underneath it: no Flatpak runtime, no Python/GJS/Vala, no
framework beyond GTK3 + libcurl + a single vendored JSON parser.

## What it does

- **Integrate** an AppImage: drag it in, pick it from a file chooser, or
  paste a direct download URL. levr extracts the embedded icon and
  `.desktop` metadata (via the AppImage's own `--appimage-extract`, no
  FUSE/squashfuse dependency needed), copies the AppImage into
  `~/.local/share/levr/appimages/`, and generates a proper menu entry.
- **List** everything you've integrated, with icon, name, and installed
  version, in one window.
- **Launch** straight from the list.
- **Update**: attach a GitHub repo (`owner/repo`) to an app and levr checks
  `releases/latest` for a `.AppImage` asset matching your architecture,
  shows an "update available" badge, and can pull + swap it in place.
  Direct-URL sources can be manually re-pulled the same way.
- **Remove**: trashes the AppImage, its `.desktop` file, and its icon
  (via `g_file_trash`, so it's recoverable, not `rm -f`).
- State lives in one JSON file (`~/.local/share/levr/store.json`).

Not included (deliberately, to stay small): a CLI, GitLab/Gitea/FTP update
sources, side-by-side multiple versions of the same app, i18n. All of
that is Gearlever's territory if you need it.

## Build

Dependencies: GTK3 dev headers, libcurl dev headers, a C compiler.

```sh
# Debian/Ubuntu
sudo apt install libgtk-3-dev libcurl4-openssl-dev build-essential pkg-config

# Arch
sudo pacman -S gtk3 curl base-devel pkgconf

make
./levr
```

`make install` (as root, or with `PREFIX=~/.local`) installs the binary
and a `.desktop` launcher.

## Layout

```
src/
  main.c       entry point
  ui.c         GTK3 window, list, dialogs, background job plumbing
  integrate.c  extract -> copy -> desktop-entry pipeline
  desktop.c    .desktop parsing/writing, icon discovery
  update.c     GitHub release checks + update/download application
  net.c        thin libcurl wrappers (GET to buffer, GET to file)
  store.c      JSON persistence (cJSON)
  util.c       paths, string helpers, safe fork/exec, recursive rm
vendor/
  cJSON.*      single-file vendored JSON library (MIT)
```

Every write to disk goes through `run_argv()` (a plain `fork()`+`execvp()`,
no shell) or direct `libc`/`GIO` calls — no `system()`/shell-string
construction anywhere, so there's no injection surface from AppImage
filenames, GitHub repo strings, or URLs.

## Notes on how integration works

An AppImage is just an ELF executable with an embedded squashfs image and
a well-known `--appimage-extract` flag that self-extracts to
`./squashfs-root/` without needing FUSE or root. levr runs that in a
throwaway temp dir, reads the `.desktop` file it finds at the top level
for `Name=`/`Icon=`/`Categories=`/`X-AppImage-Version=`, resolves the
icon (top-level file, then `usr/share/icons/hicolor/*/apps/`, then any
top-level image as a last resort), copies both into levr's data dir, and
throws the temp extraction away. The generated `.desktop` file's `Exec=`
points straight at the managed AppImage copy.
