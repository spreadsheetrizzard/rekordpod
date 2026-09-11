#include "rbprep_wave.h"

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

void rbprep_wave_init(struct rbprep_wave_reader *reader,
                      const struct plugin_api *api, void *workspace,
                      size_t workspace_bytes, size_t io_slice_bytes)
{
    size_t page_bytes;
    int page_count;
    int page;

    api->memset(reader, 0, sizeof(*reader));
    reader->api = api;
    reader->fd = -1;
    if (!workspace || workspace_bytes < RBPREP_WAVE_SAMPLE_BYTES)
        return;

    page_bytes = MIN(workspace_bytes, io_slice_bytes);
    page_bytes &= ~(size_t)(RBPREP_WAVE_SAMPLE_BYTES - 1);
    if (!page_bytes)
        return;
    page_count = MIN((int)(workspace_bytes / page_bytes),
                     RBPREP_WAVE_MAX_PAGES);
    reader->page_bytes = page_bytes;
    reader->page_count = page_count;
    for (page = 0; page < page_count; page++)
        reader->pages[page].data =
            (unsigned char *)workspace + page * page_bytes;
}

void rbprep_wave_close(struct rbprep_wave_reader *reader)
{
    int page;

    if (reader->fd >= 0)
        reader->api->close(reader->fd);
    reader->fd = -1;
    reader->point_count = 0;
    reader->stamp = 0;
    for (page = 0; page < reader->page_count; page++)
        reader->pages[page].valid = false;
}

bool rbprep_wave_open(struct rbprep_wave_reader *reader, const char *path,
                      uint32_t point_count, off_t data_offset)
{
    rbprep_wave_close(reader);
    if (!point_count || !reader->page_count)
        return false;
    reader->fd = reader->api->open(path, O_RDONLY);
    if (reader->fd < 0)
        return false;
    reader->point_count = point_count;
    reader->data_offset = data_offset;
    reader->cache_hits = 0;
    reader->cache_misses = 0;
    return true;
}

static struct rbprep_wave_page *find_page(struct rbprep_wave_reader *reader,
                                           uint32_t index)
{
    struct rbprep_wave_page *victim = NULL;
    int page;

    for (page = 0; page < reader->page_count; page++) {
        struct rbprep_wave_page *candidate = &reader->pages[page];

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

static bool load_page(struct rbprep_wave_reader *reader,
                      struct rbprep_wave_page *page, uint32_t index)
{
    uint32_t points_per_page = reader->page_bytes /
                               RBPREP_WAVE_SAMPLE_BYTES;
    uint32_t first = index / points_per_page * points_per_page;
    uint32_t count = MIN(points_per_page, reader->point_count - first);
    off_t offset = reader->data_offset +
                   (off_t)first * RBPREP_WAVE_SAMPLE_BYTES;

    page->valid = false;
    if (reader->api->lseek(reader->fd, offset, SEEK_SET) < 0 ||
        !read_exact(reader->api, reader->fd, page->data,
                    count * RBPREP_WAVE_SAMPLE_BYTES))
        return false;
    page->first = first;
    page->count = count;
    page->stamp = ++reader->stamp;
    page->valid = true;
    reader->cache_misses++;
    return true;
}

bool rbprep_wave_sample_at(struct rbprep_wave_reader *reader, uint32_t index,
                           struct rbprep_wave_sample *sample)
{
    struct rbprep_wave_page *page;
    const unsigned char *source;

    if (reader->fd < 0 || index >= reader->point_count)
        return false;
    page = find_page(reader, index);
    if (!page || !(page->valid && index >= page->first &&
                   index - page->first < page->count)) {
        if (!page || !load_page(reader, page, index))
            return false;
    }
    source = page->data + (index - page->first) * RBPREP_WAVE_SAMPLE_BYTES;
    sample->amplitude = source[0];
    sample->red = source[1];
    sample->green = source[2];
    sample->blue = source[3];
    return true;
}

uint32_t rbprep_wave_resident_points(const struct rbprep_wave_reader *reader)
{
    return reader->page_count *
           (reader->page_bytes / RBPREP_WAVE_SAMPLE_BYTES);
}

bool rbprep_wave_build_overview(struct rbprep_wave_reader *reader,
                                unsigned char (*overview)[4], int columns)
{
    int column;

    if (reader->fd < 0 || !reader->point_count || columns <= 0)
        return false;
    reader->api->memset(overview, 0, columns * RBPREP_WAVE_SAMPLE_BYTES);
    for (column = 0; column < columns; column++) {
        uint32_t begin = (uint64_t)column * reader->point_count / columns;
        uint32_t end = (uint64_t)(column + 1) * reader->point_count / columns;
        struct rbprep_wave_sample peak;
        uint32_t index;

        if (end <= begin)
            end = begin + 1;
        if (!rbprep_wave_sample_at(reader, begin, &peak))
            return false;
        for (index = begin + 1; index < end; index++) {
            struct rbprep_wave_sample sample;

            if (!rbprep_wave_sample_at(reader, index, &sample))
                return false;
            if (sample.amplitude > peak.amplitude)
                peak = sample;
        }
        overview[column][0] = peak.amplitude;
        overview[column][1] = peak.red;
        overview[column][2] = peak.green;
        overview[column][3] = peak.blue;
    }
    return true;
}
