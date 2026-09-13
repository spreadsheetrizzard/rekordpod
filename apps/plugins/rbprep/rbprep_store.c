/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "rbprep_store.h"

#define RBPREP_ROOT_DIR "/.rockbox/rekordpod"
#define RBPREP_STATE_DIR "/.rockbox/rekordpod/state"

static bool read_exact(const struct plugin_api *api, int fd,
                       void *buffer, size_t size)
{
    unsigned char *cursor = buffer;

    while (size > 0) {
        ssize_t count = api->read(fd, cursor, size);

        if (count <= 0)
            return false;
        cursor += count;
        size -= count;
    }
    return true;
}

bool rbprep_store_read(const struct plugin_api *api, const char *path,
                       unsigned char *data, size_t capacity, int *size)
{
    int fd = api->open(path, O_RDONLY);
    int length;
    bool ok;

    if (fd < 0)
        return false;
    length = api->filesize(fd);
    ok = length >= 0 && (size_t)length <= capacity &&
         read_exact(api, fd, data, length);
    if (api->close(fd) < 0)
        ok = false;
    if (ok)
        *size = length;
    return ok;
}

bool rbprep_store_write_verified(const struct plugin_api *api,
                                 const char *path,
                                 const unsigned char *data, size_t size,
    unsigned char *verify, size_t capacity)
{
    int verify_size = 0;
    int fd;
    bool write_ok;

    if (size > capacity)
        return false;
    fd = api->open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    write_ok = api->write(fd, data, size) == (ssize_t)size;
    if (api->close(fd) < 0)
        write_ok = false;
    if (!write_ok)
        return false;
    return rbprep_store_read(api, path, verify, capacity, &verify_size) &&
           verify_size == (int)size && !api->memcmp(data, verify, size);
}

void rbprep_store_ensure_state_dir(const struct plugin_api *api)
{
    if (!api->dir_exists(RBPREP_ROOT_DIR))
        api->mkdir(RBPREP_ROOT_DIR);
    if (!api->dir_exists(RBPREP_STATE_DIR))
        api->mkdir(RBPREP_STATE_DIR);
}
