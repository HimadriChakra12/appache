/* desktop.h - .desktop file parsing/writing and icon discovery */
#ifndef LEVR_DESKTOP_H
#define LEVR_DESKTOP_H

typedef struct {
    char *name;
    char *icon;      /* raw Icon= value from the source .desktop, may be a bare name */
    char *exec;      /* raw Exec= value, unused for the generated entry but kept    */
    char *categories;
    char *comment;
    char *version;   /* X-AppImage-Version=, if present */
} DesktopInfo;

/* parse_desktop_file: read [Desktop Entry] Name/Icon/Exec/Categories/Comment/
 * X-AppImage-Version from `path`. Returns a freshly allocated DesktopInfo
 * (never NULL; missing keys are left as NULL) or NULL on read failure. */
DesktopInfo *parse_desktop_file(const char *path);
void desktop_info_free(DesktopInfo *info);

/* find_desktop_file: locate the first *.desktop file directly inside `root`
 * (the extracted squashfs-root of an AppImage). Caller frees. */
char *find_desktop_file(const char *root);

/* find_icon_file: given the Icon= value from a source .desktop and the
 * extraction root, locate the actual icon image file (tries top-level
 * <icon>.{png,svg,xpm}, then usr/share/icons/hicolor/SIZE/apps/<icon>.*,
 * then falls back to any top-level .png/.svg/.diricon). Caller frees. */
char *find_icon_file(const char *root, const char *icon_key);

/* write_desktop_entry: generate a new .desktop file at `dest_path` that
 * launches `appimage_path` and shows `icon_path`. Returns 0 on success. */
int write_desktop_entry(const char *dest_path, const char *name,
                         const char *appimage_path, const char *icon_path,
                         const char *categories, const char *version);

/* refresh_desktop_db: best-effort call to update-desktop-database and
 * gtk-update-icon-cache; failures are silently ignored. */
void refresh_desktop_db(void);

#endif /* LEVR_DESKTOP_H */
