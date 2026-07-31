/* net.h - thin libcurl wrappers used for GitHub API calls and downloads */
#ifndef LEVR_NET_H
#define LEVR_NET_H

#include <stddef.h>

/* net_get: HTTP GET `url` into a newly allocated, NUL-terminated buffer.
 * Returns 0 on success (2xx response), writing the buffer to *out_data and
 * its length (excluding the NUL) to *out_len. Non-zero on transport error
 * or non-2xx status. Caller frees *out_data. */
int net_get(const char *url, char **out_data, size_t *out_len);

/* progress callback: frac in [0,1], or -1 if the total size is unknown */
typedef void (*net_progress_fn)(double frac, void *user_data);

/* net_download: HTTP GET `url`, streaming the body to `dest_path`.
 * Returns 0 on success. */
int net_download(const char *url, const char *dest_path,
                  net_progress_fn progress, void *user_data);

#endif /* LEVR_NET_H */
