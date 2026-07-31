/* store.h - load/save the list of installed apps as JSON */
#ifndef APPACHE_STORE_H
#define APPACHE_STORE_H

#include "appache.h"

/* store_load: read appache_store_path() into a freshly built AppList. Returns
 * an empty (but valid) list if the file doesn't exist yet. */
AppList *store_load(void);

/* store_save: write `list` out to appache_store_path() as JSON. Returns 0 on
 * success. */
int store_save(const AppList *list);

/* list_append: take ownership of `entry` and append it to the list. */
void list_append(AppList *list, AppEntry *entry);

/* list_remove: unlink and free the entry with the given id. Returns 1 if
 * found and removed, 0 otherwise. */
int list_remove(AppList *list, const char *id);

/* list_find: borrow a pointer to the entry with the given id, or NULL. */
AppEntry *list_find(AppList *list, const char *id);

/* list_free: free every entry and the list container itself. */
void list_free(AppList *list);

/* entry_free: free a single detached AppEntry (not linked into a list). */
void entry_free(AppEntry *entry);

#endif /* APPACHE_STORE_H */
