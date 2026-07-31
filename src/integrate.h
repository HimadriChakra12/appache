/* integrate.h - extract, register, and remove AppImages */
#ifndef APPACHE_INTEGRATE_H
#define APPACHE_INTEGRATE_H

#include "appache.h"

/* integrate_appimage: given a path to a downloaded/local .AppImage file,
 * copy it into appache's managed apps dir, extract its embedded icon and
 * .desktop metadata (via `--appimage-extract`), write a new .desktop
 * launcher, and return a fully populated AppEntry (id/name/version/paths
 * set; source_type left as SRC_NONE for the caller to fill in).
 *
 * `src_path` is consumed: on success the original file is removed after
 * being copied into place (safe even if src_path is already inside a temp
 * download directory).
 *
 * Returns the new entry, or NULL on failure (err_out, if non-NULL, receives
 * a human-readable message the caller must free). */
AppEntry *integrate_appimage(const char *src_path, char **err_out);

/* reintegrate_appimage: re-run extraction against an already-installed
 * AppEntry's appimage_path (used after an update download replaces the
 * file) to refresh name/icon/version/desktop entry in place. Returns 0 on
 * success. */
int reintegrate_appimage(AppEntry *entry, char **err_out);

/* remove_appimage: trash the managed AppImage, its .desktop file and its
 * icon (best-effort per file), but does not free or unlink the AppEntry
 * struct itself. Returns 0 if all three were removed (or already gone). */
int remove_appimage(AppEntry *entry);

#endif /* APPACHE_INTEGRATE_H */
