/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef RBPREP_STORE_H
#define RBPREP_STORE_H

#include "plugin.h"

bool rbprep_store_read(const struct plugin_api *api, const char *path,
                       unsigned char *data, size_t capacity, int *size);
bool rbprep_store_write_verified(const struct plugin_api *api,
                                 const char *path,
                                 const unsigned char *data, size_t size,
                                 unsigned char *verify, size_t capacity);
void rbprep_store_ensure_state_dir(const struct plugin_api *api);

#endif
