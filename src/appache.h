/* appache.h - shared types for appache, a minimal AppImage manager */
#ifndef APPACHE_H
#define APPACHE_H

typedef enum {
    SRC_NONE = 0,
    SRC_GITHUB,
    SRC_DIRECT
} SourceType;

typedef struct AppEntry {
    char *id;               /* stable slug, also used for filenames        */
    char *name;              /* display name                                */
    char *version;            /* currently installed version (may be empty)  */
    char *appimage_path;       /* managed copy of the .AppImage               */
    char *desktop_path;         /* generated .desktop file                     */
    char *icon_path;              /* extracted icon, absolute path               */

    SourceType source_type;
    char *source_repo;      /* "owner/repo", used when source_type == SRC_GITHUB */
    char *source_url;       /* direct download URL, used for SRC_DIRECT          */

    char *latest_version;   /* filled in by an update check, may be NULL */
    char *latest_asset_url; /* download URL for the latest asset, may be NULL */
    int   update_available; /* 0/1, valid after an update check */
    int   checking;         /* 0/1, transient UI state while a check is in flight */

    struct AppEntry *next;
} AppEntry;

typedef struct {
    AppEntry *head;
    int count;
} AppList;

#endif /* APPACHE_H */
