/* util.h - filesystem paths, string helpers, and safe process spawning */
#ifndef appache_UTIL_H
#define appache_UTIL_H

/* xstrdup: strdup() that aborts on OOM instead of returning NULL */
char *xstrdup(const char *s);

/* xasprintf: asprintf() that aborts on OOM; returns the formatted string */
char *xasprintf(const char *fmt, ...);

/* mkdir_p: recursively create a directory, like `mkdir -p`. Returns 0 on
 * success (including "already exists"), -1 on error. */
int mkdir_p(const char *path);

/* rm_rf: recursively remove a file or directory tree. Returns 0 on success. */
int rm_rf(const char *path);

/* slugify: turn an arbitrary display name into a filesystem/id-safe slug
 * (lowercase alnum and '-' only). Caller frees the result. */
char *slugify(const char *name);

/* unique_slug: given a base slug, return one that doesn't collide with any
 * file already at dir/base.suffix (appending -2, -3, ... as needed). */
char *unique_slug(const char *dir, const char *base, const char *suffix);

/* path helpers - each returns a newly allocated string the caller must free */
char *appache_data_dir(void);      /* $XDG_DATA_HOME/appache or ~/.local/share/appache */
char *appache_apps_dir(void);      /* <data_dir>/appimages                       */
char *appache_icons_dir(void);     /* <data_dir>/icons                            */
char *appache_store_path(void);    /* $XDG_CONFIG_HOME/store.json                       */
char *appache_desktop_dir(void);   /* $XDG_DATA_HOME/applications                 */
char *appache_tmp_dir(void);       /* <data_dir>/tmp, created fresh per call      */

/* ensure_dirs: make sure all appache data directories exist. Returns 0 on ok. */
int ensure_dirs(void);

/* run_argv: fork+exec argv[0] with no shell involved, wait for it to finish.
 * Returns the process exit status (0 == success), or -1 on spawn failure.
 * If cwd is non-NULL, the child chdir()s there first. Stdout/stderr of the
 * child are suppressed unless quiet is 0. */
int run_argv(char *const argv[], const char *cwd, int quiet);

/* file_exists / is_executable */
int file_exists(const char *path);
int is_executable(const char *path);

/* str_ends_with_ci: case-insensitive suffix check */
int str_ends_with_ci(const char *s, const char *suffix);

/* strip_leading_v: "v1.2.3" -> "1.2.3" (returns pointer into the same string) */
const char *strip_leading_v(const char *s);

#endif /* appache_UTIL_H */
