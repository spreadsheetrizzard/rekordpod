/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "rbprep_grid.h"

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

static uint32_t read_u32(const unsigned char *data)
{
    return data[0] | (data[1] << 8) | (data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

void rbprep_grid_init(struct rbprep_grid_reader *reader,
                      const struct plugin_api *api, void *workspace,
                      size_t workspace_bytes)
{
    size_t page_bytes;
    int page_count;
    int page;

    api->memset(reader, 0, sizeof(*reader));
    reader->api = api;
    reader->fd = -1;
    if (!workspace || workspace_bytes < RBPREP_GRID_RECORD_BYTES)
        return;

    page_count = MIN(RBPREP_GRID_MAX_PAGES,
                     (int)(workspace_bytes / 512));
    if (page_count <= 0)
        page_count = 1;
    page_bytes = workspace_bytes / page_count;
    page_bytes &= ~(size_t)(RBPREP_GRID_RECORD_BYTES - 1);
    if (!page_bytes)
        return;
    reader->page_bytes = page_bytes;
    reader->page_count = page_count;
    for (page = 0; page < page_count; page++)
        reader->pages[page].data =
            (unsigned char *)workspace + page * page_bytes;
}

void rbprep_grid_close(struct rbprep_grid_reader *reader)
{
    int page;

    if (reader->fd >= 0)
        reader->api->close(reader->fd);
    reader->fd = -1;
    reader->beat_count = 0;
    reader->stamp = 0;
    reader->fully_resident = false;
    for (page = 0; page < reader->page_count; page++)
        reader->pages[page].valid = false;
}

bool rbprep_grid_open(struct rbprep_grid_reader *reader, const char *path,
                      uint32_t beat_count, off_t data_offset)
{
    rbprep_grid_close(reader);
    if (!beat_count || !reader->page_count)
        return false;
    reader->fd = reader->api->open(path, O_RDONLY);
    if (reader->fd < 0)
        return false;
    reader->beat_count = beat_count;
    reader->data_offset = data_offset;
    reader->cache_hits = 0;
    reader->cache_misses = 0;
    reader->fully_resident = false;
    return true;
}

static struct rbprep_grid_page *cached_page(
    struct rbprep_grid_reader *reader, uint32_t index)
{
    int page;

    for (page = 0; page < reader->page_count; page++) {
        struct rbprep_grid_page *candidate = &reader->pages[page];

        if (candidate->valid && index >= candidate->first &&
            index - candidate->first < candidate->count)
            return candidate;
    }
    return NULL;
}

static void decode_beat(const struct rbprep_grid_page *page, uint32_t index,
                        struct rbprep_grid_beat *beat)
{
    const unsigned char *source = page->data +
        (index - page->first) * RBPREP_GRID_RECORD_BYTES;

    beat->time_ms = read_u32(source);
    beat->number = MAX(1, MIN(4, source[4]));
}

static struct rbprep_grid_page *find_page(struct rbprep_grid_reader *reader,
                                           uint32_t index)
{
    struct rbprep_grid_page *victim = NULL;
    int page;

    for (page = 0; page < reader->page_count; page++) {
        struct rbprep_grid_page *candidate = &reader->pages[page];

        if (candidate->valid && index >= candidate->first &&
            index - candidate->first < candidate->count) {
            candidate->stamp = ++reader->stamp;
            reader->cache_hits++;
            return candidate;
        }
        if (!victim || !candidate->valid ||
            (victim->valid && candidate->stamp < victim->stamp))
            victim = candidate;
    }
    return victim;
}

static bool load_page(struct rbprep_grid_reader *reader,
                      struct rbprep_grid_page *page, uint32_t index)
{
    uint32_t records_per_page = reader->page_bytes /
                                RBPREP_GRID_RECORD_BYTES;
    uint32_t first = index / records_per_page * records_per_page;
    uint32_t count = MIN(records_per_page, reader->beat_count - first);
    off_t offset = reader->data_offset +
                   (off_t)first * RBPREP_GRID_RECORD_BYTES;

    page->valid = false;
    if (reader->api->lseek(reader->fd, offset, SEEK_SET) < 0 ||
        !read_exact(reader->api, reader->fd, page->data,
                    count * RBPREP_GRID_RECORD_BYTES))
        return false;
    page->first = first;
    page->count = count;
    page->stamp = ++reader->stamp;
    page->valid = true;
    reader->cache_misses++;
    return true;
}

bool rbprep_grid_beat_cached(struct rbprep_grid_reader *reader,
                             uint32_t index,
                             struct rbprep_grid_beat *beat)
{
    struct rbprep_grid_page *page;

    if (index >= reader->beat_count)
        return false;
    page = cached_page(reader, index);
    if (!page)
        return false;
    page->stamp = ++reader->stamp;
    reader->cache_hits++;
    decode_beat(page, index, beat);
    return true;
}

bool rbprep_grid_range_cached(struct rbprep_grid_reader *reader,
                              uint32_t begin, uint32_t end)
{
    uint32_t cursor;

    if (begin >= end || end > reader->beat_count)
        return false;
    cursor = begin;
    while (cursor < end) {
        const struct rbprep_grid_page *page = cached_page(reader, cursor);
        uint32_t page_end;

        if (!page)
            return false;
        page_end = page->first + page->count;
        if (page_end <= cursor)
            return false;
        cursor = page_end;
    }
    return true;
}

bool rbprep_grid_cache_all(struct rbprep_grid_reader *reader)
{
    uint32_t records_per_page;
    uint32_t required_pages;
    uint32_t cursor = 0;

    reader->fully_resident = false;
    if (reader->fd < 0 || reader->page_count <= 0 || !reader->beat_count)
        return false;
    records_per_page = reader->page_bytes / RBPREP_GRID_RECORD_BYTES;
    if (!records_per_page)
        return false;
    required_pages = (reader->beat_count + records_per_page - 1) /
                     records_per_page;
    if (required_pages > (uint32_t)reader->page_count)
        return false;

    while (cursor < reader->beat_count) {
        struct rbprep_grid_page *page = find_page(reader, cursor);

        if (!page || !(page->valid && cursor >= page->first &&
                       cursor - page->first < page->count)) {
            if (!page || !load_page(reader, page, cursor))
                return false;
        }
        cursor = page->first + page->count;
    }
    reader->fully_resident =
        rbprep_grid_range_cached(reader, 0, reader->beat_count);
    return reader->fully_resident;
}

bool rbprep_grid_fully_resident(const struct rbprep_grid_reader *reader)
{
    return reader->fully_resident;
}

bool rbprep_grid_beat_at(struct rbprep_grid_reader *reader, uint32_t index,
                         struct rbprep_grid_beat *beat)
{
    struct rbprep_grid_page *page;

    if (reader->fd < 0 || index >= reader->beat_count)
        return false;
    page = find_page(reader, index);
    if (!page || !(page->valid && index >= page->first &&
                   index - page->first < page->count)) {
        if (!page || !load_page(reader, page, index))
            return false;
    }
    decode_beat(page, index, beat);
    return true;
}
