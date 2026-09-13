/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef RBPREP_GRID_H
#define RBPREP_GRID_H

#include "plugin.h"

#define RBPREP_GRID_RECORD_BYTES 8
#define RBPREP_GRID_MAX_PAGES 4

struct rbprep_grid_beat {
    uint32_t time_ms;
    unsigned char number;
};

struct rbprep_grid_page {
    uint32_t first;
    uint32_t count;
    uint32_t stamp;
    unsigned char *data;
    bool valid;
};

struct rbprep_grid_reader {
    const struct plugin_api *api;
    int fd;
    off_t data_offset;
    uint32_t beat_count;
    uint32_t stamp;
    size_t page_bytes;
    int page_count;
    bool fully_resident;
    struct rbprep_grid_page pages[RBPREP_GRID_MAX_PAGES];
    uint32_t cache_hits;
    uint32_t cache_misses;
};

void rbprep_grid_init(struct rbprep_grid_reader *reader,
                      const struct plugin_api *api, void *workspace,
                      size_t workspace_bytes);
bool rbprep_grid_open(struct rbprep_grid_reader *reader, const char *path,
                      uint32_t beat_count, off_t data_offset);
void rbprep_grid_close(struct rbprep_grid_reader *reader);
bool rbprep_grid_beat_at(struct rbprep_grid_reader *reader, uint32_t index,
                         struct rbprep_grid_beat *beat);
/* Cached-only accessors never seek or read from storage. */
bool rbprep_grid_beat_cached(struct rbprep_grid_reader *reader,
                             uint32_t index,
                             struct rbprep_grid_beat *beat);
bool rbprep_grid_range_cached(struct rbprep_grid_reader *reader,
                              uint32_t begin, uint32_t end);
/* Preload succeeds only when the reader workspace can hold every beat. */
bool rbprep_grid_cache_all(struct rbprep_grid_reader *reader);
bool rbprep_grid_fully_resident(const struct rbprep_grid_reader *reader);

#endif
