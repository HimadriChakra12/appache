/* update.h - check for and apply AppImage updates */
#ifndef APPACHE_UPDATE_H
#define APPACHE_UPDATE_H

#include "appache.h"

/* check_update: for SRC_GITHUB entries, query the latest GitHub release and
 * look for a .AppImage asset (preferring one matching this machine's
 * architecture); fills entry->latest_version, latest_asset_url and
 * update_available. For SRC_DIRECT entries this just marks the entry as
 * "always updatable" via a fresh download of source_url (no version
 * comparison is possible without more metadata). SRC_NONE entries are a
 * no-op. Returns 0 on success, -1 on network/parse failure (entry is left
 * unchanged other entries than update_available=0). */
int check_update(AppEntry *entry);

/* apply_update: download the update (latest_asset_url for GitHub sources,
 * source_url for direct sources) to a temp file, then re-run the
 * integration pipeline in place (see reintegrate_appimage). Returns 0 on
 * success. */
int apply_update(AppEntry *entry, char **err_out);

/* add_from_url: download `url` to a temp file and integrate it as a new
 * app, tagging it SRC_DIRECT with source_url = url. Returns the new entry
 * or NULL (err_out receives a message the caller must free). */
AppEntry *add_from_url(const char *url, char **err_out);

#endif /* APPACHE_UPDATE_H */
