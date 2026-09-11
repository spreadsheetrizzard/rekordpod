#include "rbprep_wave_index.h"

#define RBX_HEADER_SIZE 64
#define RBX_VERSION 1
#define RBX_DIR "/.rockbox/rbprep/wave-index"

static const uint16_t level_blocks[RBPREP_WAVE_INDEX_LEVELS] = {
    16, 64, 256, 1024
};

static uint16_t read_u16(const unsigned char *data)
{
    return data[0] | (data[1] << 8);
}

static uint32_t read_u32(const unsigned char *data)
{
    return data[0] | (data[1] << 8) | (data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void write_u16(unsigned char *data, uint16_t value)
{
    data[0] = value;
    data[1] = value >> 8;
}

static void write_u32(unsigned char *data, uint32_t value)
{
    data[0] = value;
    data[1] = value >> 8;
    data[2] = value >> 16;
    data[3] = value >> 24;
}

static uint32_t checksum_update(uint32_t checksum,
                                const unsigned char *data, size_t size)
{
    size_t i;

    for (i = 0; i < size; i++) {
        checksum ^= data[i];
        checksum *= 16777619u;
    }
    return checksum;
}

uint32_t rbprep_wave_index_fingerprint(const unsigned char *header,
                                       size_t header_size,
                                       uint32_t source_size)
{
    uint32_t checksum = checksum_update(2166136261u, header, header_size);

    return checksum_update(checksum, (const unsigned char *)&source_size,
                           sizeof(source_size));
}

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

static bool write_exact(const struct plugin_api *api, int fd,
                        const void *buffer, size_t size)
{
    const unsigned char *cursor = buffer;

    while (size > 0) {
        ssize_t count = api->write(fd, cursor, size);

        if (count <= 0)
            return false;
        cursor += count;
        size -= count;
    }
    return true;
}

static bool configure_layout(struct rbprep_wave_index *index,
                             uint32_t source_points)
{
    size_t offset = RBPREP_WAVE_INDEX_OVERVIEW * 4;
    int level;

    for (level = 0; level < RBPREP_WAVE_INDEX_LEVELS; level++) {
        uint32_t block = level_blocks[level];

        index->level_counts[level] =
            (source_points + block - 1) / block;
        index->level_offsets[level] = offset;
        offset += index->level_counts[level] * 4;
    }
    index->payload_size = offset;
    return offset <= index->capacity;
}

void rbprep_wave_index_init(struct rbprep_wave_index *index,
                            const struct plugin_api *api, void *workspace,
                            size_t workspace_bytes, size_t io_slice_bytes)
{
    api->memset(index, 0, sizeof(*index));
    index->api = api;
    index->payload = workspace;
    index->capacity = workspace_bytes;
    index->io_slice_bytes = MAX((size_t)512, io_slice_bytes);
    index->fd = -1;
}

void rbprep_wave_index_close(struct rbprep_wave_index *index)
{
    if (index->fd >= 0)
        index->api->close(index->fd);
    index->fd = -1;
    index->stage = RBPREP_WAVE_INDEX_IDLE;
    index->overview = NULL;
    index->valid = false;
}

static bool header_matches(struct rbprep_wave_index *index,
                           const unsigned char *header)
{
    int level;

    if (index->api->memcmp(header, "RBX1", 4) ||
        read_u16(header + 4) != RBX_VERSION ||
        read_u16(header + 6) != RBX_HEADER_SIZE ||
        read_u32(header + 8) != index->track_id ||
        read_u32(header + 12) != index->source_points ||
        read_u32(header + 16) != index->source_size ||
        read_u32(header + 20) != index->source_fingerprint ||
        read_u16(header + 24) != RBPREP_WAVE_INDEX_OVERVIEW ||
        read_u16(header + 26) != RBPREP_WAVE_INDEX_LEVELS ||
        read_u32(header + 28) != index->payload_size)
        return false;
    for (level = 0; level < RBPREP_WAVE_INDEX_LEVELS; level++)
        if (read_u32(header + 36 + level * 4) !=
            index->level_counts[level])
            return false;
    index->payload_checksum = read_u32(header + 32);
    return true;
}

static void prepare_identity(struct rbprep_wave_index *index,
                             const char *path, uint32_t track_id,
                             uint32_t source_points, uint32_t source_size,
                             uint32_t source_fingerprint,
                             unsigned char (*overview)[4])
{
    index->track_id = track_id;
    index->source_points = source_points;
    index->source_size = source_size;
    index->source_fingerprint = source_fingerprint;
    index->overview = overview;
    index->api->strlcpy(index->path, path, sizeof(index->path));
    index->api->snprintf(index->temporary, sizeof(index->temporary),
                         "%s.tmp", path);
}

bool rbprep_wave_index_open(struct rbprep_wave_index *index,
                            const char *path, uint32_t track_id,
                            uint32_t source_points, uint32_t source_size,
                            uint32_t source_fingerprint,
                            unsigned char (*overview)[4])
{
    unsigned char header[RBX_HEADER_SIZE];
    uint32_t checksum;
    int fd;
    bool ok;

    rbprep_wave_index_close(index);
    prepare_identity(index, path, track_id, source_points, source_size,
                     source_fingerprint, overview);
    if (!index->payload || !configure_layout(index, source_points))
        return false;
    fd = index->api->open(path, O_RDONLY);
    if (fd < 0)
        return false;
    ok = index->api->filesize(fd) ==
             (off_t)(RBX_HEADER_SIZE + index->payload_size) &&
         read_exact(index->api, fd, header, sizeof(header)) &&
         header_matches(index, header) &&
         read_exact(index->api, fd, index->payload, index->payload_size);
    if (index->api->close(fd) < 0)
        ok = false;
    checksum = ok ? checksum_update(2166136261u, index->payload,
                                    index->payload_size) : 0;
    if (!ok || checksum != index->payload_checksum)
        return false;
    index->api->memcpy(overview, index->payload,
                       RBPREP_WAVE_INDEX_OVERVIEW * 4);
    index->valid = true;
    return true;
}

bool rbprep_wave_index_start(struct rbprep_wave_index *index,
                             const char *path, uint32_t track_id,
                             uint32_t source_points, uint32_t source_size,
                             uint32_t source_fingerprint,
                             const char *source_path,
                             off_t source_data_offset,
                             unsigned char (*overview)[4])
{
    rbprep_wave_index_close(index);
    prepare_identity(index, path, track_id, source_points, source_size,
                     source_fingerprint, overview);
    if (!index->payload || !configure_layout(index, source_points) ||
        index->capacity - index->payload_size < 512)
        return false;
    index->api->memset(index->payload, 0, index->payload_size);
    index->api->memset(overview, 0, RBPREP_WAVE_INDEX_OVERVIEW * 4);
    index->fd = index->api->open(source_path, O_RDONLY);
    if (index->fd < 0 ||
        index->api->lseek(index->fd, source_data_offset, SEEK_SET) < 0) {
        if (index->fd >= 0)
            index->api->close(index->fd);
        index->fd = -1;
        return false;
    }
    index->source_data_offset = source_data_offset;
    index->scan_position = 0;
    index->transfer_position = 0;
    index->stage = RBPREP_WAVE_INDEX_SCAN;
    return true;
}

static void merge_peak(unsigned char *target,
                       const struct rbprep_wave_sample *sample)
{
    if (sample->amplitude > target[0]) {
        target[0] = sample->amplitude;
        target[1] = sample->red;
        target[2] = sample->green;
        target[3] = sample->blue;
    }
}

static bool service_scan(struct rbprep_wave_index *index)
{
    unsigned char *input = index->payload + index->payload_size;
    size_t input_capacity = index->capacity - index->payload_size;
    uint32_t remaining = index->source_points - index->scan_position;
    size_t bytes = MIN(input_capacity, index->io_slice_bytes);
    uint32_t samples;
    uint32_t processed = 0;

    bytes = MIN(bytes, (size_t)remaining * 4);
    bytes &= ~(size_t)3;
    if (!bytes || !read_exact(index->api, index->fd, input, bytes)) {
        index->api->close(index->fd);
        index->fd = -1;
        index->stage = RBPREP_WAVE_INDEX_FAILED;
        return false;
    }
    samples = bytes / 4;

    while (samples-- > 0) {
        struct rbprep_wave_sample sample;
        uint32_t point = index->scan_position;
        const unsigned char *raw = input + processed * 4;
        uint32_t overview_column =
            (uint64_t)point * RBPREP_WAVE_INDEX_OVERVIEW /
            index->source_points;
        int level;

        sample.amplitude = raw[0];
        sample.red = raw[1];
        sample.green = raw[2];
        sample.blue = raw[3];
        merge_peak(index->payload + overview_column * 4, &sample);
        merge_peak(index->overview[overview_column], &sample);
        for (level = 0; level < RBPREP_WAVE_INDEX_LEVELS; level++) {
            size_t offset = index->level_offsets[level] +
                (point / level_blocks[level]) * 4;

            merge_peak(index->payload + offset, &sample);
        }
        index->scan_position++;
        processed++;
    }
    if (index->scan_position == index->source_points) {
        if (index->api->close(index->fd) < 0) {
            index->fd = -1;
            index->stage = RBPREP_WAVE_INDEX_FAILED;
            return false;
        }
        index->fd = -1;
        index->payload_checksum = checksum_update(
            2166136261u, index->payload, index->payload_size);
        index->stage = RBPREP_WAVE_INDEX_OPEN_WRITE;
    }
    return true;
}

static void build_header(const struct rbprep_wave_index *index,
                         unsigned char *header)
{
    int level;

    index->api->memset(header, 0, RBX_HEADER_SIZE);
    index->api->memcpy(header, "RBX1", 4);
    write_u16(header + 4, RBX_VERSION);
    write_u16(header + 6, RBX_HEADER_SIZE);
    write_u32(header + 8, index->track_id);
    write_u32(header + 12, index->source_points);
    write_u32(header + 16, index->source_size);
    write_u32(header + 20, index->source_fingerprint);
    write_u16(header + 24, RBPREP_WAVE_INDEX_OVERVIEW);
    write_u16(header + 26, RBPREP_WAVE_INDEX_LEVELS);
    write_u32(header + 28, index->payload_size);
    write_u32(header + 32, index->payload_checksum);
    for (level = 0; level < RBPREP_WAVE_INDEX_LEVELS; level++)
        write_u32(header + 36 + level * 4,
                  index->level_counts[level]);
}

static bool service_write(struct rbprep_wave_index *index)
{
    size_t count = MIN(index->io_slice_bytes,
                       index->payload_size - index->transfer_position);

    if (!write_exact(index->api, index->fd,
                     index->payload + index->transfer_position, count)) {
        index->api->close(index->fd);
        index->fd = -1;
        index->stage = RBPREP_WAVE_INDEX_FAILED;
        return false;
    }
    index->transfer_position += count;
    if (index->transfer_position == index->payload_size) {
        if (index->api->close(index->fd) < 0) {
            index->fd = -1;
            index->stage = RBPREP_WAVE_INDEX_FAILED;
            return false;
        }
        index->fd = -1;
        index->stage = RBPREP_WAVE_INDEX_OPEN_VERIFY;
    }
    return false;
}

static bool service_verify(struct rbprep_wave_index *index)
{
    unsigned char verify[512];
    size_t budget = index->io_slice_bytes;

    while (budget > 0 && index->transfer_position < index->payload_size) {
        size_t count = MIN(sizeof(verify), budget);

        count = MIN(count, index->payload_size - index->transfer_position);
        if (!read_exact(index->api, index->fd, verify, count) ||
            index->api->memcmp(verify,
                index->payload + index->transfer_position, count)) {
            index->api->close(index->fd);
            index->fd = -1;
            index->stage = RBPREP_WAVE_INDEX_FAILED;
            return false;
        }
        index->verify_checksum = checksum_update(index->verify_checksum,
                                                 verify, count);
        index->transfer_position += count;
        budget -= count;
    }
    if (index->transfer_position == index->payload_size) {
        bool ok = index->verify_checksum == index->payload_checksum &&
                  index->api->close(index->fd) >= 0;

        index->fd = -1;
        index->stage = ok ? RBPREP_WAVE_INDEX_PUBLISH
                          : RBPREP_WAVE_INDEX_FAILED;
    }
    return false;
}

bool rbprep_wave_index_service(struct rbprep_wave_index *index)
{
    unsigned char header[RBX_HEADER_SIZE];

    switch (index->stage) {
    case RBPREP_WAVE_INDEX_SCAN:
        return service_scan(index);
    case RBPREP_WAVE_INDEX_OPEN_WRITE:
        if (!index->api->dir_exists(RBX_DIR))
            index->api->mkdir(RBX_DIR);
        index->fd = index->api->open(index->temporary,
                                     O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (index->fd < 0) {
            index->stage = RBPREP_WAVE_INDEX_FAILED;
            return false;
        }
        build_header(index, header);
        if (!write_exact(index->api, index->fd, header, sizeof(header))) {
            index->api->close(index->fd);
            index->fd = -1;
            index->stage = RBPREP_WAVE_INDEX_FAILED;
            return false;
        }
        index->transfer_position = 0;
        index->stage = RBPREP_WAVE_INDEX_WRITE;
        return false;
    case RBPREP_WAVE_INDEX_WRITE:
        return service_write(index);
    case RBPREP_WAVE_INDEX_OPEN_VERIFY:
        index->fd = index->api->open(index->temporary, O_RDONLY);
        if (index->fd < 0 ||
            !read_exact(index->api, index->fd, header, sizeof(header)) ||
            !header_matches(index, header)) {
            if (index->fd >= 0)
                index->api->close(index->fd);
            index->fd = -1;
            index->stage = RBPREP_WAVE_INDEX_FAILED;
            return false;
        }
        index->transfer_position = 0;
        index->verify_checksum = 2166136261u;
        index->stage = RBPREP_WAVE_INDEX_VERIFY;
        return false;
    case RBPREP_WAVE_INDEX_VERIFY:
        return service_verify(index);
    case RBPREP_WAVE_INDEX_PUBLISH:
        if (index->api->file_exists(index->path))
            index->api->remove(index->path);
        if (index->api->rename(index->temporary, index->path) < 0) {
            index->stage = RBPREP_WAVE_INDEX_FAILED;
            return false;
        }
        index->valid = true;
        index->stage = RBPREP_WAVE_INDEX_IDLE;
        return true;
    default:
        return false;
    }
}

bool rbprep_wave_index_range_peak(const struct rbprep_wave_index *index,
                                  uint32_t begin, uint32_t end,
                                  struct rbprep_wave_sample *peak)
{
    uint32_t width;
    uint32_t first;
    uint32_t last;
    int level = -1;
    int candidate;
    uint32_t entry;

    if (!index->valid || begin >= end || end > index->source_points)
        return false;
    width = end - begin;
    for (candidate = 0; candidate < RBPREP_WAVE_INDEX_LEVELS;
         candidate++) {
        if (level_blocks[candidate] <= width)
            level = candidate;
        else
            break;
    }
    /* During active playback the UI never touches the raw RBW stream. For a
       zoom level finer than the smallest summary bucket, repeat that 16-point
       bucket across adjacent pixels. Paused/scrubbed views may replace it
       with exact raw detail without competing with Rockbox's codec reads. */
    if (level < 0)
        level = 0;
    first = begin / level_blocks[level];
    last = (end - 1) / level_blocks[level];
    if (last >= index->level_counts[level])
        last = index->level_counts[level] - 1;
    peak->amplitude = 0;
    peak->red = peak->green = peak->blue = 0;
    for (entry = first; entry <= last; entry++) {
        const unsigned char *sample = index->payload +
            index->level_offsets[level] + entry * 4;

        if (sample[0] > peak->amplitude) {
            peak->amplitude = sample[0];
            peak->red = sample[1];
            peak->green = sample[2];
            peak->blue = sample[3];
        }
    }
    return true;
}
