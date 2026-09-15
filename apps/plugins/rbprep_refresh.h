/* SPDX-License-Identifier: GPL-2.0-or-later */

/* On-device DeviceSQL -> RBI3 refresh support.
 *
 * This header is included by rbprep.c immediately after rbprep_burn.h so it
 * can reuse the already-hardened DeviceSQL page reader and atomic file-swap
 * helpers.  Rekordbox's export.pdb is never modified here.  A complete new
 * compact index is written beside the live one, validated, and only then
 * renamed into place.
 */

#define RBPREP_SOURCE_STATE "/.rockbox/rekordpod/library-source.rbs"
#define RBPREP_SOURCE_STATE_TMP RBPREP_SOURCE_STATE ".tmp"
#define RBPREP_REFRESH_PENDING "/.rockbox/rekordpod/library-refresh.pending"
#define RBPREP_REFRESH_PENDING_TMP RBPREP_REFRESH_PENDING ".tmp"
#define RBPREP_GENRES_NEW RBPREP_GENRES ".rekordpod-new"
#define RBPREP_GENRES_PREV RBPREP_GENRES ".rekordpod-prev"
#define RBPREP_REFRESH_TEXT 512

struct rbprep_refresh_arena {
    unsigned char *cursor;
    unsigned char *end;
};

struct rbprep_refresh_named {
    uint32_t id;
    char *name;
};

struct rbprep_refresh_track {
    uint32_t id;
    uint32_t artist_id;
    uint32_t genre_id;
    uint32_t key_id;
    uint32_t color_id;
    uint16_t bpm_x100;
    uint16_t year;
    uint16_t import_date;
    unsigned char rating;
    unsigned char color;
    char *path;
    char *title;
    char *artist;
    char *genre;
    char *key;
    char *comments;
    char *tags;
    char *search;
    char *analysis_path;
    char *date_added;
    uint32_t path_offset;
    uint32_t title_offset;
    uint32_t artist_offset;
    uint32_t genre_offset;
    uint32_t key_offset;
    uint32_t comments_offset;
    uint32_t tags_offset;
    uint32_t search_offset;
};

struct rbprep_refresh_track_map {
    uint32_t id;
    uint32_t index;
};

struct rbprep_refresh_node {
    uint32_t parent_id;
    uint32_t source_id;
    uint32_t sort_order;
    uint32_t parent;
    uint32_t name_offset;
    uint32_t first_member;
    uint32_t member_count;
    bool folder;
    char *name;
};

struct rbprep_refresh_entry {
    uint32_t playlist_id;
    uint32_t ordinal;
    uint32_t track_id;
};

struct rbprep_refresh_source {
    uint32_t size;
    uint32_t fingerprint;
};

static struct rbprep_refresh_track *rbprep_refresh_sort_tracks;
static int rbprep_refresh_sort_field;

static void *rbprep_refresh_alloc(struct rbprep_refresh_arena *arena,
                                  size_t bytes)
{
    uintptr_t start = ((uintptr_t)arena->cursor + 3u) & ~(uintptr_t)3u;
    uintptr_t finish = start + bytes;

    if (finish < start || finish > (uintptr_t)arena->end)
        return NULL;
    arena->cursor = (unsigned char *)finish;
    rb->memset((void *)start, 0, bytes);
    return (void *)start;
}

static void *rbprep_refresh_alloc_array(struct rbprep_refresh_arena *arena,
                                        size_t count, size_t element_size)
{
    if (element_size && count > (size_t)-1 / element_size)
        return NULL;
    return rbprep_refresh_alloc(arena, count * element_size);
}

static char *rbprep_refresh_strdup(struct rbprep_refresh_arena *arena,
                                   const char *value)
{
    size_t bytes = rb->strlen(value) + 1;
    char *copy = rbprep_refresh_alloc(arena, bytes);

    if (copy)
        rb->memcpy(copy, value, bytes);
    return copy;
}

static bool rbprep_refresh_write_all(int fd, const void *data, size_t bytes)
{
    return rb->write(fd, data, bytes) == (ssize_t)bytes;
}

static bool rbprep_refresh_source_signature(
    struct rbprep_refresh_source *source)
{
    int fd = rb->open(RBPREP_PDB, O_RDONLY);
    off_t size;
    uint32_t hash = 2166136261u;
    int amount;

    if (fd < 0)
        return false;
    size = rb->filesize(fd);
    if (size <= 0 || (uint64_t)size > 0xffffffffu) {
        rb->close(fd);
        return false;
    }
    while ((amount = rb->read(fd, rbprep_burn_page,
                              sizeof(rbprep_burn_page))) > 0) {
        int i;
        for (i = 0; i < amount; i++) {
            hash ^= rbprep_burn_page[i];
            hash *= 16777619u;
        }
        rb->yield();
    }
    rb->close(fd);
    if (amount < 0)
        return false;
    source->size = (uint32_t)size;
    source->fingerprint = hash;
    return true;
}

static bool rbprep_refresh_read_source_state(
    struct rbprep_refresh_source *source)
{
    unsigned char raw[16];
    int fd = rb->open(RBPREP_SOURCE_STATE, O_RDONLY);
    bool valid = fd >= 0 && read_exact(fd, raw, sizeof(raw)) &&
                 rb->filesize(fd) == (off_t)sizeof(raw) &&
                 !rb->memcmp(raw, "RLS1", 4) && read_u16(raw + 4) == 1;

    if (fd >= 0)
        rb->close(fd);
    if (!valid)
        return false;
    source->size = read_u32(raw + 8);
    source->fingerprint = read_u32(raw + 12);
    return true;
}

static bool rbprep_refresh_write_source_state(
    const struct rbprep_refresh_source *source)
{
    unsigned char raw[16];
    int fd;
    bool ok;

    rb->memset(raw, 0, sizeof(raw));
    rb->memcpy(raw, "RLS1", 4);
    write_u16(raw + 4, 1);
    write_u16(raw + 6, sizeof(raw));
    write_u32(raw + 8, source->size);
    write_u32(raw + 12, source->fingerprint);
    rb->remove(RBPREP_SOURCE_STATE_TMP);
    fd = rb->open(RBPREP_SOURCE_STATE_TMP,
                  O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    ok = rbprep_refresh_write_all(fd, raw, sizeof(raw));
    if (rb->close(fd) < 0)
        ok = false;
    if (!ok) {
        rb->remove(RBPREP_SOURCE_STATE_TMP);
        return false;
    }
    rb->remove(RBPREP_SOURCE_STATE);
    if (rb->rename(RBPREP_SOURCE_STATE_TMP, RBPREP_SOURCE_STATE) < 0) {
        rb->remove(RBPREP_SOURCE_STATE_TMP);
        return false;
    }
    return true;
}

/* Written before mass storage is exposed.  If USB disconnects cleanly the
 * normal resume path consumes it; after a reboot it makes the next launch
 * verify export.pdb once without penalizing every ordinary startup. */
static bool rbprep_refresh_mark_pending(void)
{
    static const unsigned char marker[4] = { 'R', 'L', 'P', '1' };
    int fd;
    bool ok;

    rb->remove(RBPREP_REFRESH_PENDING_TMP);
    fd = rb->open(RBPREP_REFRESH_PENDING_TMP,
                  O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    ok = rbprep_refresh_write_all(fd, marker, sizeof(marker));
    if (rb->close(fd) < 0)
        ok = false;
    if (!ok) {
        rb->remove(RBPREP_REFRESH_PENDING_TMP);
        return false;
    }
    rb->remove(RBPREP_REFRESH_PENDING);
    if (rb->rename(RBPREP_REFRESH_PENDING_TMP,
                   RBPREP_REFRESH_PENDING) < 0) {
        rb->remove(RBPREP_REFRESH_PENDING_TMP);
        return false;
    }
    return true;
}

static void rbprep_refresh_clear_pending(void)
{
    rb->remove(RBPREP_REFRESH_PENDING);
    rb->remove(RBPREP_REFRESH_PENDING_TMP);
}

static bool rbprep_refresh_pdb_open(struct rbprep_pdb *pdb,
                                    const char *path)
{
    unsigned char header[28];
    int attempt;

    /* Refresh never modifies export.pdb.  In particular, do not inherit the
       burner's O_RDWR requirement: after USB, some storage adapters expose a
       readable database slightly before the volume accepts new write opens. */
    for (attempt = 0; attempt < 8; attempt++) {
        rb->memset(pdb, 0, sizeof(*pdb));
        pdb->fd = rb->open(path, O_RDONLY);
        if (pdb->fd >= 0 &&
            burn_read_at(pdb->fd, 0, header, sizeof(header))) {
            pdb->page_size = read_u32(header + 4);
            pdb->table_count = read_u32(header + 8);
            if (read_u32(header) == 0 && pdb->page_size == 4096 &&
                pdb->page_size <= RBPREP_PDB_PAGE_MAX &&
                pdb->table_count > 0 && pdb->table_count <= 64)
                return true;
        }
        if (pdb->fd >= 0)
            rb->close(pdb->fd);
        pdb->fd = -1;
        rb->sleep(MAX(1, HZ / 20));
    }
    return false;
}

static bool rbprep_refresh_table_bounds(struct rbprep_pdb *pdb,
                                        uint32_t type, uint32_t *first,
                                        uint32_t *last)
{
    uint32_t entry;
    unsigned char raw[16];

    if (!pdb_table_entry(pdb, type, &entry) ||
        !burn_read_at(pdb->fd, entry, raw, sizeof(raw)))
        return false;
    *first = read_u32(raw + 8);
    *last = read_u32(raw + 12);
    return true;
}

static int rbprep_refresh_count_rows(struct rbprep_pdb *pdb, uint32_t type)
{
    uint32_t page;
    uint32_t last;
    uint32_t guard = 0;
    int count = 0;

    if (!rbprep_refresh_table_bounds(pdb, type, &page, &last))
        return type == 6 ? 0 : -1;
    while (guard++ < 100000) {
        int slot;
        int slots;

        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return -1;
        slots = pdb_slot_count(rbprep_burn_page);
        if (!(rbprep_burn_page[0x1b] & 0x40))
            for (slot = 0; slot < slots; slot++)
                if (pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    count++;
        if (page == last)
            return count;
        if (!pdb_next_page(pdb, page, &page))
            return -1;
    }
    return -1;
}

static uint32_t rbprep_refresh_row_address(struct rbprep_pdb *pdb,
                                           uint32_t page, int slot)
{
    return page * pdb->page_size + 0x28 +
           pdb_row_heap_offset(rbprep_burn_page, pdb->page_size, slot);
}

static bool rbprep_refresh_decode_page_string(
    struct rbprep_pdb *pdb, uint32_t page, uint32_t row, uint16_t relative,
    char *buffer, size_t size)
{
    uint32_t page_start = page * pdb->page_size;
    uint32_t offset;
    int bytes;
    size_t used = 0;
    unsigned char kind;

    if (!size || row < page_start || row - page_start >= pdb->page_size)
        return false;
    offset = row - page_start + relative;
    if (offset >= pdb->page_size)
        return false;
    kind = rbprep_burn_page[offset];
    if (kind & 1) {
        bytes = (kind >> 1) - 1;
        if (bytes < 0 || offset + 1 + (uint32_t)bytes > pdb->page_size)
            return false;
        bytes = MIN(bytes, (int)size - 1);
        rb->memcpy(buffer, rbprep_burn_page + offset + 1, bytes);
        buffer[bytes] = '\0';
        return true;
    }
    if (offset + 4 > pdb->page_size)
        return false;
    bytes = read_u16(rbprep_burn_page + offset + 1) - 4;
    if (bytes < 0 || offset + 4 + (uint32_t)bytes > pdb->page_size)
        return false;
    if (kind == 0x40) {
        bytes = MIN(bytes, (int)size - 1);
        rb->memcpy(buffer, rbprep_burn_page + offset + 4, bytes);
        buffer[bytes] = '\0';
        return true;
    }
    if (kind == 0x90) {
        int i;
        for (i = 0; i + 1 < bytes && used + 1 < size; i += 2) {
            uint16_t code = read_u16(rbprep_burn_page + offset + 4 + i);
            if (code < 0x80) {
                buffer[used++] = code;
            } else if (code < 0x800 && used + 2 < size) {
                buffer[used++] = 0xc0 | (code >> 6);
                buffer[used++] = 0x80 | (code & 0x3f);
            } else if (used + 3 < size) {
                buffer[used++] = 0xe0 | (code >> 12);
                buffer[used++] = 0x80 | ((code >> 6) & 0x3f);
                buffer[used++] = 0x80 | (code & 0x3f);
            }
        }
        buffer[used] = '\0';
        return true;
    }
    return false;
}

static int rbprep_refresh_named_compare(const void *left, const void *right)
{
    const struct rbprep_refresh_named *a = left;
    const struct rbprep_refresh_named *b = right;
    return a->id < b->id ? -1 : a->id != b->id;
}

static const char *rbprep_refresh_named_find(
    const struct rbprep_refresh_named *values, int count, uint32_t id)
{
    int low = 0;
    int high = count;

    while (low < high) {
        int middle = low + (high - low) / 2;
        if (values[middle].id < id)
            low = middle + 1;
        else
            high = middle;
    }
    return low < count && values[low].id == id ? values[low].name : "";
}

static bool rbprep_refresh_collect_named(
    struct rbprep_pdb *pdb, uint32_t type,
    struct rbprep_refresh_named *values, int expected,
    struct rbprep_refresh_arena *arena)
{
    uint32_t page;
    uint32_t last;
    uint32_t guard = 0;
    int used = 0;

    if (!expected)
        return true;
    if (!rbprep_refresh_table_bounds(pdb, type, &page, &last))
        return false;
    while (guard++ < 100000) {
        int slot;
        int slots;

        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return false;
        slots = pdb_slot_count(rbprep_burn_page);
        if (!(rbprep_burn_page[0x1b] & 0x40)) {
            for (slot = 0; slot < slots; slot++) {
                unsigned char fixed[24];
                uint32_t row;
                uint32_t id;
                uint16_t relative;
                char decoded[RBPREP_REFRESH_TEXT];

                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                if (used >= expected)
                    return false;
                row = rbprep_refresh_row_address(pdb, page, slot);
                if (!burn_read_at(pdb->fd, row, fixed, sizeof(fixed)))
                    return false;
                if (type == 2) {
                    id = read_u32(fixed + 4);
                    relative = read_u16(fixed) == 0x64
                             ? read_u16(fixed + 0x0a) : fixed[9];
                } else if (type == 6) {
                    id = read_u16(fixed + 5);
                    relative = 8;
                } else {
                    id = read_u32(fixed);
                    relative = type == 5 ? 8 : 4;
                }
                decoded[0] = '\0';
                if (!rbprep_refresh_decode_page_string(
                        pdb, page, row, relative, decoded, sizeof(decoded)))
                    return false;
                values[used].id = id;
                values[used].name = rbprep_refresh_strdup(arena, decoded);
                if (!values[used].name)
                    return false;
                used++;
            }
        }
        if (page == last)
            break;
        if (!pdb_next_page(pdb, page, &page))
            return false;
    }
    if (used != expected)
        return false;
    rb->qsort(values, used, sizeof(*values), rbprep_refresh_named_compare);
    return true;
}

static uint16_t rbprep_refresh_pack_date(const char *text)
{
    static const unsigned char days_per_month[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int year;
    int month;
    int day;
    int i;
    int maximum_day;

    if (!text || rb->strlen(text) < 10 || text[4] != '-' || text[7] != '-')
        return 0;
    for (i = 0; i < 10; i++) {
        if (i == 4 || i == 7)
            continue;
        if (text[i] < '0' || text[i] > '9')
            return 0;
    }
    year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 +
           (text[2] - '0') * 10 + text[3] - '0';
    month = (text[5] - '0') * 10 + text[6] - '0';
    day = (text[8] - '0') * 10 + text[9] - '0';
    if (year < 1980 || year > 2107 || month < 1 || month > 12)
        return 0;
    maximum_day = days_per_month[month - 1];
    if (month == 2 && (!(year % 4) && ((year % 100) || !(year % 400))))
        maximum_day++;
    if (day < 1 || day > maximum_day)
        return 0;
    return ((year - 1980) << 9) | (month << 5) | day;
}

static char *rbprep_refresh_tags(struct rbprep_refresh_arena *arena,
                                 const char *comments)
{
    size_t length = rb->strlen(comments);
    char *result = rbprep_refresh_alloc(arena, length + 1);
    size_t source = 0;
    size_t output = 0;

    if (!result)
        return NULL;
    while (source < length) {
        size_t start;
        while (source < length &&
               (comments[source] == ' ' || comments[source] == '\t' ||
                comments[source] == '\r' || comments[source] == '\n'))
            source++;
        start = source;
        while (source < length && comments[source] != ' ' &&
               comments[source] != '\t' && comments[source] != '\r' &&
               comments[source] != '\n')
            source++;
        if (source > start && comments[start] == '#') {
            if (output)
                result[output++] = ' ';
            rb->memcpy(result + output, comments + start, source - start);
            output += source - start;
        }
    }
    result[output] = '\0';
    return result;
}

static char *rbprep_refresh_search(struct rbprep_refresh_arena *arena,
                                   struct rbprep_refresh_track *track,
                                   const char *date)
{
    const char *parts[7];
    size_t total = 7;
    size_t cursor = 0;
    char *result;
    int i;

    parts[0] = track->title;
    parts[1] = track->artist;
    parts[2] = track->genre;
    parts[3] = track->key;
    parts[4] = track->tags;
    parts[5] = track->comments;
    parts[6] = date;
    for (i = 0; i < 7; i++)
        total += rb->strlen(parts[i]);
    result = rbprep_refresh_alloc(arena, total);
    if (!result)
        return NULL;
    for (i = 0; i < 7; i++) {
        size_t length = rb->strlen(parts[i]);
        if (length) {
            rb->memcpy(result + cursor, parts[i], length);
            cursor += length;
        }
        if (i != 6)
            result[cursor++] = 0x1f;
    }
    result[cursor] = '\0';
    return result;
}

static unsigned char rbprep_refresh_track_color(const char *name)
{
    static const char * const labels[] = {
        "sample", "opener", "builder", "pivoter", "maintainer",
        "peak", "reset", "tool"
    };
    int i;

    for (i = 0; i < (int)ARRAYLEN(labels); i++)
        if (!rb->strcasecmp(name, labels[i]))
            return i;
    return 8;
}

static bool rbprep_refresh_decode_track_string(
    struct rbprep_pdb *pdb, uint32_t page, uint32_t row,
    const unsigned char *fixed, int slot, char *buffer, size_t size)
{
    uint16_t relative = read_u16(fixed + 0x5e + slot * 2);

    buffer[0] = '\0';
    return rbprep_refresh_decode_page_string(pdb, page, row, relative,
                                              buffer, size);
}

static bool rbprep_refresh_collect_tracks(
    struct rbprep_pdb *pdb, struct rbprep_refresh_track *tracks,
    int expected, const struct rbprep_refresh_named *artists,
    int artist_count, const struct rbprep_refresh_named *genres,
    int genre_count, const struct rbprep_refresh_named *keys, int key_count,
    const struct rbprep_refresh_named *colors, int color_count,
    struct rbprep_refresh_arena *arena)
{
    uint32_t page;
    uint32_t last;
    uint32_t guard = 0;
    int used = 0;

    if (!rbprep_refresh_table_bounds(pdb, 0, &page, &last))
        return false;
    while (guard++ < 100000) {
        int slot;
        int slots;

        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return false;
        slots = pdb_slot_count(rbprep_burn_page);
        if (!(rbprep_burn_page[0x1b] & 0x40)) {
            for (slot = 0; slot < slots; slot++) {
                struct rbprep_refresh_track *track;
                unsigned char fixed[0x88];
                char path[MAX_PATH];
                char title[RBPREP_REFRESH_TEXT];
                char comments[RBPREP_REFRESH_TEXT];
                char analysis[MAX_PATH];
                char date[32];
                uint32_t row;
                const char *name;

                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                if (used >= expected)
                    return false;
                row = rbprep_refresh_row_address(pdb, page, slot);
                if (!burn_read_at(pdb->fd, row, fixed, sizeof(fixed)) ||
                    !rbprep_refresh_decode_track_string(
                        pdb, page, row, fixed, 20, path, sizeof(path)) ||
                    !rbprep_refresh_decode_track_string(
                        pdb, page, row, fixed, 17, title, sizeof(title)) ||
                    !rbprep_refresh_decode_track_string(
                        pdb, page, row, fixed, 16, comments,
                        sizeof(comments)) ||
                    !rbprep_refresh_decode_track_string(
                        pdb, page, row, fixed, 14, analysis,
                        sizeof(analysis)) ||
                    !rbprep_refresh_decode_track_string(
                        pdb, page, row, fixed, 10, date, sizeof(date)))
                    return false;

                track = &tracks[used++];
                track->key_id = read_u32(fixed + 0x20);
                track->bpm_x100 = MIN(65535u, read_u32(fixed + 0x38));
                track->genre_id = read_u32(fixed + 0x3c);
                track->artist_id = read_u32(fixed + 0x44);
                track->id = read_u32(fixed + 0x48);
                track->year = read_u16(fixed + 0x50);
                track->color_id = fixed[0x58];
                track->rating = MIN(5, fixed[0x59]);
                track->import_date = rbprep_refresh_pack_date(date);
                track->path = rbprep_refresh_strdup(arena, path);
                track->title = rbprep_refresh_strdup(arena, title);
                track->comments = rbprep_refresh_strdup(arena, comments);
                track->analysis_path = rbprep_refresh_strdup(arena, analysis);
                track->date_added = rbprep_refresh_strdup(arena, date);
                if (!track->path || !track->title || !track->comments ||
                    !track->analysis_path || !track->date_added)
                    return false;

                name = rbprep_refresh_named_find(artists, artist_count,
                                                  track->artist_id);
                track->artist = rbprep_refresh_strdup(arena, name);
                name = rbprep_refresh_named_find(genres, genre_count,
                                                  track->genre_id);
                if (!name[0])
                    name = "UNCLASSIFIED";
                track->genre = rbprep_refresh_strdup(arena, name);
                name = rbprep_refresh_named_find(keys, key_count,
                                                  track->key_id);
                track->key = rbprep_refresh_strdup(arena, name);
                name = rbprep_refresh_named_find(colors, color_count,
                                                  track->color_id);
                track->color = rbprep_refresh_track_color(name);
                track->tags = rbprep_refresh_tags(arena, track->comments);
                if (!track->artist || !track->genre || !track->key ||
                    !track->tags)
                    return false;
                track->search = rbprep_refresh_search(arena, track,
                                                      track->date_added);
                if (!track->search)
                    return false;
                if (!(used & 127)) {
                    activity_ticker_ping(MIN(450, used * 450 /
                                             MAX(1, expected)));
                    rb->yield();
                }
            }
        }
        if (page == last)
            break;
        if (!pdb_next_page(pdb, page, &page))
            return false;
    }
    return used == expected;
}

static int rbprep_refresh_track_title_compare(const void *left,
                                              const void *right)
{
    const struct rbprep_refresh_track *a = left;
    const struct rbprep_refresh_track *b = right;
    int comparison = rb->strcasecmp(a->title, b->title);

    if (!comparison)
        comparison = rb->strcasecmp(a->artist, b->artist);
    if (!comparison)
        comparison = a->id < b->id ? -1 : a->id != b->id;
    return comparison;
}

static int rbprep_refresh_track_map_compare(const void *left,
                                            const void *right)
{
    const struct rbprep_refresh_track_map *a = left;
    const struct rbprep_refresh_track_map *b = right;
    return a->id < b->id ? -1 : a->id != b->id;
}

static int rbprep_refresh_track_index(
    const struct rbprep_refresh_track_map *map, int count, uint32_t id)
{
    int low = 0;
    int high = count;

    while (low < high) {
        int middle = low + (high - low) / 2;
        if (map[middle].id < id)
            low = middle + 1;
        else
            high = middle;
    }
    return low < count && map[low].id == id ? (int)map[low].index : -1;
}

static bool rbprep_refresh_collect_nodes(
    struct rbprep_pdb *pdb, struct rbprep_refresh_node *nodes,
    int expected, struct rbprep_refresh_arena *arena)
{
    uint32_t page;
    uint32_t last;
    uint32_t guard = 0;
    int used = 0;

    if (!rbprep_refresh_table_bounds(pdb, 7, &page, &last))
        return false;
    while (guard++ < 100000) {
        int slot;
        int slots;

        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return false;
        slots = pdb_slot_count(rbprep_burn_page);
        if (!(rbprep_burn_page[0x1b] & 0x40)) {
            for (slot = 0; slot < slots; slot++) {
                unsigned char fixed[24];
                char name[RBPREP_REFRESH_TEXT];
                uint32_t row;

                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                if (used >= expected)
                    return false;
                row = rbprep_refresh_row_address(pdb, page, slot);
                if (!burn_read_at(pdb->fd, row, fixed, sizeof(fixed)) ||
                    !rbprep_refresh_decode_page_string(
                        pdb, page, row, 20, name, sizeof(name)))
                    return false;
                nodes[used].parent_id = read_u32(fixed);
                nodes[used].sort_order = read_u32(fixed + 8);
                nodes[used].source_id = read_u32(fixed + 12);
                nodes[used].folder = read_u32(fixed + 16) != 0;
                nodes[used].name = rbprep_refresh_strdup(arena, name);
                if (!nodes[used].name)
                    return false;
                used++;
            }
        }
        if (page == last)
            break;
        if (!pdb_next_page(pdb, page, &page))
            return false;
    }
    return used == expected;
}

static bool rbprep_refresh_collect_entries(
    struct rbprep_pdb *pdb, struct rbprep_refresh_entry *entries,
    int expected)
{
    uint32_t page;
    uint32_t last;
    uint32_t guard = 0;
    int used = 0;

    if (!rbprep_refresh_table_bounds(pdb, 8, &page, &last))
        return false;
    while (guard++ < 100000) {
        int slot;
        int slots;

        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return false;
        slots = pdb_slot_count(rbprep_burn_page);
        if (!(rbprep_burn_page[0x1b] & 0x40)) {
            for (slot = 0; slot < slots; slot++) {
                unsigned char fixed[12];
                uint32_t row;

                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                if (used >= expected)
                    return false;
                row = rbprep_refresh_row_address(pdb, page, slot);
                if (!burn_read_at(pdb->fd, row, fixed, sizeof(fixed)))
                    return false;
                entries[used].ordinal = read_u32(fixed);
                entries[used].track_id = read_u32(fixed + 4);
                entries[used].playlist_id = read_u32(fixed + 8);
                used++;
            }
        }
        if (page == last)
            break;
        if (!pdb_next_page(pdb, page, &page))
            return false;
    }
    return used == expected;
}

static int rbprep_refresh_entry_compare(const void *left, const void *right)
{
    const struct rbprep_refresh_entry *a = left;
    const struct rbprep_refresh_entry *b = right;

    if (a->playlist_id != b->playlist_id)
        return a->playlist_id < b->playlist_id ? -1 : 1;
    if (a->ordinal != b->ordinal)
        return a->ordinal < b->ordinal ? -1 : 1;
    return a->track_id < b->track_id ? -1 : a->track_id != b->track_id;
}

static int rbprep_refresh_node_index(const struct rbprep_refresh_node *nodes,
                                     int count, uint32_t source_id)
{
    int i;
    for (i = 0; i < count; i++)
        if (nodes[i].source_id == source_id)
            return i;
    return -1;
}

static int rbprep_refresh_entry_first(
    const struct rbprep_refresh_entry *entries, int count,
    uint32_t playlist_id)
{
    int low = 0;
    int high = count;

    while (low < high) {
        int middle = low + (high - low) / 2;
        if (entries[middle].playlist_id < playlist_id)
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

static int rbprep_refresh_text_compare(const char *a, const char *b)
{
    bool empty_a = !a || !a[0];
    bool empty_b = !b || !b[0];

    if (empty_a != empty_b)
        return empty_a ? 1 : -1;
    return rb->strcasecmp(a ? a : "", b ? b : "");
}

static int rbprep_refresh_index_compare(const void *left, const void *right)
{
    uint32_t ai = *(const uint32_t *)left;
    uint32_t bi = *(const uint32_t *)right;
    const struct rbprep_refresh_track *a = &rbprep_refresh_sort_tracks[ai];
    const struct rbprep_refresh_track *b = &rbprep_refresh_sort_tracks[bi];
    int result = 0;

    switch (rbprep_refresh_sort_field) {
    case TRACK_SORT_BPM:
        if (!a->bpm_x100 != !b->bpm_x100)
            result = !a->bpm_x100 ? 1 : -1;
        else if (a->bpm_x100 != b->bpm_x100)
            result = a->bpm_x100 < b->bpm_x100 ? -1 : 1;
        break;
    case TRACK_SORT_YEAR:
        if (!a->year != !b->year)
            result = !a->year ? 1 : -1;
        else if (a->year != b->year)
            result = a->year < b->year ? -1 : 1;
        break;
    case TRACK_SORT_KEY:
        result = rbprep_refresh_text_compare(a->key, b->key);
        break;
    case TRACK_SORT_COMMENTS:
        result = rbprep_refresh_text_compare(a->comments, b->comments);
        break;
    case TRACK_SORT_TAGS:
        result = rbprep_refresh_text_compare(a->tags, b->tags);
        break;
    case TRACK_SORT_IMPORTED:
        if (!a->import_date != !b->import_date)
            result = !a->import_date ? 1 : -1;
        else if (a->import_date != b->import_date)
            result = a->import_date < b->import_date ? -1 : 1;
        break;
    }
    if (!result)
        result = rbprep_refresh_track_title_compare(a, b);
    return result;
}

static bool rbprep_refresh_add_string_offset(const char *value,
                                             uint32_t *cursor,
                                             uint32_t *result)
{
    uint64_t next;

    if (!value || !value[0]) {
        *result = 0;
        return true;
    }
    *result = *cursor;
    next = (uint64_t)*cursor + rb->strlen(value) + 1;
    if (next > 0xffffffffu)
        return false;
    *cursor = (uint32_t)next;
    return true;
}

static bool rbprep_refresh_write_string(int fd, const char *value)
{
    size_t length;

    if (!value || !value[0])
        return true;
    length = rb->strlen(value) + 1;
    return rbprep_refresh_write_all(fd, value, length);
}

static bool rbprep_refresh_validate_index_once(const char *path)
{
    unsigned char header[RBPREP_INDEX_HEADER];
    uint64_t expected;
    uint32_t tracks;
    uint32_t nodes;
    uint32_t members;
    uint32_t cursor;
    int fd = rb->open(path, O_RDONLY);
    int i;
    bool valid = false;

    if (fd < 0 || !read_exact(fd, header, sizeof(header)) ||
        rb->memcmp(header, "RBI1", 4) || read_u16(header + 4) != 3 ||
        read_u16(header + 6) != RBPREP_INDEX_HEADER)
        goto done;
    tracks = read_u32(header + 8);
    nodes = read_u32(header + 12);
    members = read_u32(header + 16);
    if (read_u32(header + 20) != RBPREP_INDEX_HEADER)
        goto done;
    expected = RBPREP_INDEX_HEADER + (uint64_t)tracks * RBPREP_TRACK_RECORD;
    if (expected > 0xffffffffu || read_u32(header + 24) != expected)
        goto done;
    expected += (uint64_t)nodes * RBPREP_NODE_RECORD;
    if (expected > 0xffffffffu || read_u32(header + 28) != expected)
        goto done;
    expected += (uint64_t)members * 4;
    if (expected > 0xffffffffu)
        goto done;
    cursor = expected;
    for (i = TRACK_SORT_BPM; i < TRACK_SORT_COUNT; i++) {
        if (read_u32(header + 36 + i * 4) != cursor)
            goto done;
        expected += (uint64_t)tracks * 4;
        if (expected > 0xffffffffu)
            goto done;
        cursor = expected;
    }
    if (read_u32(header + 32) != cursor)
        goto done;
    expected += read_u32(header + 36);
    valid = expected <= 0xffffffffu && rb->filesize(fd) == (off_t)expected;
done:
    if (fd >= 0)
        rb->close(fd);
    return valid;
}

static bool rbprep_refresh_validate_index(const char *path)
{
    int attempt;

    /* A large FAT allocation can briefly retain stale directory/first-cluster
       state across close/reopen on HDD and multi-card adapters. */
    for (attempt = 0; attempt < 8; attempt++) {
        if (rbprep_refresh_validate_index_once(path))
            return true;
        rb->sleep(MAX(1, HZ / 20));
    }
    return false;
}

static bool rbprep_refresh_write_index(
    struct rbprep_refresh_track *tracks, int track_count,
    struct rbprep_refresh_track_map *track_map,
    struct rbprep_refresh_node *nodes, int node_count,
    struct rbprep_refresh_entry *entries, int entry_count,
    uint32_t *sort_order)
{
    unsigned char header[RBPREP_INDEX_HEADER];
    unsigned char raw[RBPREP_TRACK_RECORD];
    uint64_t layout;
    uint32_t track_offset = RBPREP_INDEX_HEADER;
    uint32_t node_offset;
    uint32_t member_offset;
    uint32_t sort_offsets[TRACK_SORT_COUNT];
    uint32_t string_offset;
    uint32_t string_size = 1;
    uint32_t member_total = 0;
    int fd = -1;
    int i;
    bool ok = false;

    rb->qsort(tracks, track_count, sizeof(*tracks),
              rbprep_refresh_track_title_compare);
    for (i = 0; i < track_count; i++) {
        track_map[i].id = tracks[i].id;
        track_map[i].index = i;
    }
    rb->qsort(track_map, track_count, sizeof(*track_map),
              rbprep_refresh_track_map_compare);
    rb->qsort(entries, entry_count, sizeof(*entries),
              rbprep_refresh_entry_compare);

    for (i = 0; i < node_count; i++) {
        int at;
        int parent = nodes[i].parent_id
                   ? rbprep_refresh_node_index(nodes, node_count,
                                               nodes[i].parent_id) : -1;
        nodes[i].parent = parent >= 0 ? (uint32_t)parent
                                      : RBPREP_ROOT_NODE;
        nodes[i].first_member = member_total;
        nodes[i].member_count = 0;
        if (nodes[i].folder)
            continue;
        at = rbprep_refresh_entry_first(entries, entry_count,
                                         nodes[i].source_id);
        while (at < entry_count &&
               entries[at].playlist_id == nodes[i].source_id) {
            if (rbprep_refresh_track_index(track_map, track_count,
                                            entries[at].track_id) >= 0) {
                nodes[i].member_count++;
                member_total++;
            }
            at++;
        }
    }

    for (i = 0; i < track_count; i++) {
        if (!rbprep_refresh_add_string_offset(tracks[i].path, &string_size,
                                              &tracks[i].path_offset) ||
            !rbprep_refresh_add_string_offset(tracks[i].title, &string_size,
                                              &tracks[i].title_offset) ||
            !rbprep_refresh_add_string_offset(tracks[i].artist, &string_size,
                                              &tracks[i].artist_offset) ||
            !rbprep_refresh_add_string_offset(tracks[i].genre, &string_size,
                                              &tracks[i].genre_offset) ||
            !rbprep_refresh_add_string_offset(tracks[i].key, &string_size,
                                              &tracks[i].key_offset) ||
            !rbprep_refresh_add_string_offset(tracks[i].comments,
                                              &string_size,
                                              &tracks[i].comments_offset) ||
            !rbprep_refresh_add_string_offset(tracks[i].tags, &string_size,
                                              &tracks[i].tags_offset) ||
            !rbprep_refresh_add_string_offset(tracks[i].search, &string_size,
                                              &tracks[i].search_offset))
            return false;
    }
    for (i = 0; i < node_count; i++)
        if (!rbprep_refresh_add_string_offset(nodes[i].name, &string_size,
                                              &nodes[i].name_offset))
            return false;

    layout = RBPREP_INDEX_HEADER +
             (uint64_t)track_count * RBPREP_TRACK_RECORD;
    if (layout > 0xffffffffu)
        return false;
    node_offset = layout;
    layout += (uint64_t)node_count * RBPREP_NODE_RECORD;
    if (layout > 0xffffffffu)
        return false;
    member_offset = layout;
    layout += (uint64_t)member_total * 4;
    sort_offsets[TRACK_SORT_TITLE] = 0;
    for (i = TRACK_SORT_BPM; i < TRACK_SORT_COUNT; i++) {
        if (layout > 0xffffffffu)
            return false;
        sort_offsets[i] = layout;
        layout += (uint64_t)track_count * 4;
    }
    if (layout > 0xffffffffu)
        return false;
    string_offset = layout;
    if (layout + string_size > 0xffffffffu)
        return false;

    rb->remove(RBPREP_INDEX_NEW);
    fd = rb->open(RBPREP_INDEX_NEW,
                  O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    rb->memset(header, 0, sizeof(header));
    rb->memcpy(header, "RBI1", 4);
    write_u16(header + 4, 3);
    write_u16(header + 6, RBPREP_INDEX_HEADER);
    write_u32(header + 8, track_count);
    write_u32(header + 12, node_count);
    write_u32(header + 16, member_total);
    write_u32(header + 20, track_offset);
    write_u32(header + 24, node_offset);
    write_u32(header + 28, member_offset);
    write_u32(header + 32, string_offset);
    write_u32(header + 36, string_size);
    for (i = TRACK_SORT_BPM; i < TRACK_SORT_COUNT; i++)
        write_u32(header + 36 + i * 4, sort_offsets[i]);
    if (!rbprep_refresh_write_all(fd, header, sizeof(header)))
        goto done;

    for (i = 0; i < track_count; i++) {
        rb->memset(raw, 0, sizeof(raw));
        write_u32(raw, tracks[i].id);
        write_u32(raw + 4, tracks[i].path_offset);
        write_u32(raw + 8, tracks[i].title_offset);
        write_u32(raw + 12, tracks[i].artist_offset);
        write_u32(raw + 16, tracks[i].genre_offset);
        write_u32(raw + 20, tracks[i].key_offset);
        write_u16(raw + 24, tracks[i].bpm_x100);
        raw[26] = tracks[i].rating;
        raw[27] = tracks[i].color;
        write_u16(raw + 28, tracks[i].year);
        write_u16(raw + 30, tracks[i].import_date);
        write_u32(raw + 32, tracks[i].comments_offset);
        write_u32(raw + 36, tracks[i].tags_offset);
        write_u32(raw + 40, tracks[i].search_offset);
        if (!rbprep_refresh_write_all(fd, raw, sizeof(raw)))
            goto done;
    }
    for (i = 0; i < node_count; i++) {
        rb->memset(raw, 0, RBPREP_NODE_RECORD);
        write_u32(raw, nodes[i].parent);
        write_u32(raw + 4, nodes[i].name_offset);
        write_u32(raw + 8, nodes[i].first_member);
        write_u32(raw + 12, nodes[i].member_count);
        raw[16] = nodes[i].folder ? 0 : 1;
        write_u32(raw + 20, nodes[i].source_id);
        if (!rbprep_refresh_write_all(fd, raw, RBPREP_NODE_RECORD))
            goto done;
    }
    for (i = 0; i < node_count; i++) {
        int at;
        if (nodes[i].folder)
            continue;
        at = rbprep_refresh_entry_first(entries, entry_count,
                                         nodes[i].source_id);
        while (at < entry_count &&
               entries[at].playlist_id == nodes[i].source_id) {
            int index = rbprep_refresh_track_index(track_map, track_count,
                                                   entries[at].track_id);
            if (index >= 0) {
                write_u32(raw, index);
                if (!rbprep_refresh_write_all(fd, raw, 4))
                    goto done;
            }
            at++;
        }
    }
    rbprep_refresh_sort_tracks = tracks;
    for (i = TRACK_SORT_BPM; i < TRACK_SORT_COUNT; i++) {
        int item;
        rbprep_refresh_sort_field = i;
        for (item = 0; item < track_count; item++)
            sort_order[item] = item;
        rb->qsort(sort_order, track_count, sizeof(*sort_order),
                  rbprep_refresh_index_compare);
        for (item = 0; item < track_count; item++) {
            write_u32(raw, sort_order[item]);
            if (!rbprep_refresh_write_all(fd, raw, 4))
                goto done;
        }
        activity_ticker_ping(500 + (i - TRACK_SORT_BPM + 1) * 45);
        rb->yield();
    }
    raw[0] = 0;
    if (!rbprep_refresh_write_all(fd, raw, 1))
        goto done;
    for (i = 0; i < track_count; i++) {
        if (!rbprep_refresh_write_string(fd, tracks[i].path) ||
            !rbprep_refresh_write_string(fd, tracks[i].title) ||
            !rbprep_refresh_write_string(fd, tracks[i].artist) ||
            !rbprep_refresh_write_string(fd, tracks[i].genre) ||
            !rbprep_refresh_write_string(fd, tracks[i].key) ||
            !rbprep_refresh_write_string(fd, tracks[i].comments) ||
            !rbprep_refresh_write_string(fd, tracks[i].tags) ||
            !rbprep_refresh_write_string(fd, tracks[i].search))
            goto done;
    }
    for (i = 0; i < node_count; i++)
        if (!rbprep_refresh_write_string(fd, nodes[i].name))
            goto done;
    ok = rb->filesize(fd) == (off_t)(layout + string_size);
done:
    if (fd >= 0 && rb->close(fd) < 0)
        ok = false;
    if (!ok || !rbprep_refresh_validate_index(RBPREP_INDEX_NEW)) {
        rb->remove(RBPREP_INDEX_NEW);
        return false;
    }
    return true;
}

static int rbprep_refresh_genre_compare(const void *left, const void *right)
{
    const char *a = *(const char * const *)left;
    const char *b = *(const char * const *)right;
    return rb->strcasecmp(a, b);
}

static bool rbprep_refresh_write_genres(
    const struct rbprep_refresh_track *tracks, int track_count,
    char **names)
{
    unsigned char raw[16];
    uint32_t string_size = 1;
    uint32_t cursor = 1;
    int unique = 0;
    int fd = -1;
    int i;
    bool ok = false;

    for (i = 0; i < track_count; i++)
        if (tracks[i].genre && tracks[i].genre[0])
            names[unique++] = tracks[i].genre;
    rb->qsort(names, unique, sizeof(*names), rbprep_refresh_genre_compare);
    if (unique > 0) {
        int output = 1;
        for (i = 1; i < unique; i++)
            if (rb->strcasecmp(names[i], names[output - 1]))
                names[output++] = names[i];
        unique = output;
    }
    for (i = 0; i < unique; i++) {
        uint64_t next = (uint64_t)string_size + rb->strlen(names[i]) + 1;
        if (next > 0xffffffffu)
            return false;
        string_size = next;
    }
    rb->remove(RBPREP_GENRES_NEW);
    fd = rb->open(RBPREP_GENRES_NEW,
                  O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    rb->memset(raw, 0, sizeof(raw));
    rb->memcpy(raw, "RBG1", 4);
    write_u16(raw + 4, 1);
    write_u16(raw + 6, 16);
    write_u32(raw + 8, unique);
    write_u32(raw + 12, string_size);
    if (!rbprep_refresh_write_all(fd, raw, sizeof(raw)))
        goto done;
    for (i = 0; i < unique; i++) {
        write_u32(raw, cursor);
        if (!rbprep_refresh_write_all(fd, raw, 4))
            goto done;
        cursor += rb->strlen(names[i]) + 1;
    }
    raw[0] = 0;
    if (!rbprep_refresh_write_all(fd, raw, 1))
        goto done;
    for (i = 0; i < unique; i++)
        if (!rbprep_refresh_write_string(fd, names[i]))
            goto done;
    ok = rb->filesize(fd) == (off_t)(16 + unique * 4 + string_size);
done:
    if (fd >= 0 && rb->close(fd) < 0)
        ok = false;
    if (!ok)
        rb->remove(RBPREP_GENRES_NEW);
    return ok;
}

static bool rbprep_refresh_replace(const char *path, const char *temporary,
                                   const char *previous)
{
    if (rb->file_exists(path))
        return burn_swap_keep_previous(path, temporary, previous);
    return rb->rename(temporary, path) >= 0;
}

/* Return 1 when an updated RBI3 was installed, 0 when the source is already
 * represented, and -1 when the old validated index had to be retained. */
static int rbprep_refresh_library_if_needed(bool check_source)
{
    struct rbprep_refresh_source current;
    struct rbprep_refresh_source saved;
    struct rbprep_refresh_arena arena;
    struct rbprep_pdb pdb;
    struct rbprep_refresh_track *tracks = NULL;
    struct rbprep_refresh_track_map *track_map = NULL;
    struct rbprep_refresh_node *nodes = NULL;
    struct rbprep_refresh_entry *entries = NULL;
    struct rbprep_refresh_named *artists = NULL;
    struct rbprep_refresh_named *genres = NULL;
    struct rbprep_refresh_named *keys = NULL;
    struct rbprep_refresh_named *colors = NULL;
    uint32_t *sort_order = NULL;
    char **genre_names = NULL;
    void *memory = NULL;
    size_t memory_size = 0;
    int track_count;
    int node_count;
    int entry_count;
    int artist_count;
    int genre_count;
    int key_count;
    int color_count;
    bool index_replaced = false;
    bool genre_replaced = false;
    const char *stage = "source check";
    int result = -1;

    rb->memset(&pdb, 0, sizeof(pdb));
    rbprep_burn_failure_detail[0] = '\0';
    pdb.fd = -1;
    if (!check_source && rbprep_refresh_read_source_state(&saved) &&
        rb->file_exists(RBPREP_INDEX))
        return 0;
    if (!rbprep_refresh_source_signature(&current))
        return rb->file_exists(RBPREP_INDEX) ? 0 : -1;
    if (rbprep_refresh_read_source_state(&saved) &&
        saved.size == current.size &&
        saved.fingerprint == current.fingerprint &&
        rb->file_exists(RBPREP_INDEX))
        return 0;

    /* The audio buffer is the only workspace large enough for a full 1 TB
       library index on both HDD and flash-backed Classics.  USB data mode has
       already stopped playback, and first-run refresh explicitly does so. */
    stop_editor_audio();
    rb->audio_stop();
    rb->yield();
    stage = "workspace";
    memory = rb->plugin_get_audio_buffer(&memory_size);
    if (!memory || memory_size < 128 * 1024)
        goto done;
    arena.cursor = memory;
    arena.end = (unsigned char *)memory + memory_size;

    rb->splash(1, "Updating Rekordpod library");
    activity_ticker_ping(40);
    stage = "database open";
    if (!rbprep_refresh_pdb_open(&pdb, RBPREP_PDB) ||
        !pdb_validate_structure(&pdb))
        goto done;
    stage = "table scan";
    track_count = rbprep_refresh_count_rows(&pdb, 0);
    genre_count = rbprep_refresh_count_rows(&pdb, 1);
    artist_count = rbprep_refresh_count_rows(&pdb, 2);
    key_count = rbprep_refresh_count_rows(&pdb, 5);
    color_count = rbprep_refresh_count_rows(&pdb, 6);
    node_count = rbprep_refresh_count_rows(&pdb, 7);
    entry_count = rbprep_refresh_count_rows(&pdb, 8);
    if (track_count < 0 || genre_count < 0 || artist_count < 0 ||
        key_count < 0 || color_count < 0 || node_count < 0 ||
        entry_count < 0)
        goto done;

    tracks = rbprep_refresh_alloc_array(&arena, track_count,
                                        sizeof(*tracks));
    track_map = rbprep_refresh_alloc_array(&arena, track_count,
                                           sizeof(*track_map));
    nodes = rbprep_refresh_alloc_array(&arena, node_count, sizeof(*nodes));
    entries = rbprep_refresh_alloc_array(&arena, entry_count,
                                         sizeof(*entries));
    artists = rbprep_refresh_alloc_array(&arena, artist_count,
                                         sizeof(*artists));
    genres = rbprep_refresh_alloc_array(&arena, genre_count,
                                        sizeof(*genres));
    keys = rbprep_refresh_alloc_array(&arena, key_count, sizeof(*keys));
    colors = rbprep_refresh_alloc_array(&arena, color_count,
                                        sizeof(*colors));
    sort_order = rbprep_refresh_alloc_array(&arena, track_count,
                                            sizeof(*sort_order));
    genre_names = rbprep_refresh_alloc_array(&arena, track_count,
                                             sizeof(*genre_names));
    if ((track_count && (!tracks || !track_map || !sort_order ||
                         !genre_names)) ||
        (node_count && !nodes) || (entry_count && !entries) ||
        (artist_count && !artists) || (genre_count && !genres) ||
        (key_count && !keys) || (color_count && !colors))
        goto done;

    stage = "metadata scan";
    if (!rbprep_refresh_collect_named(&pdb, 1, genres, genre_count,
                                      &arena) ||
        !rbprep_refresh_collect_named(&pdb, 2, artists, artist_count,
                                      &arena) ||
        !rbprep_refresh_collect_named(&pdb, 5, keys, key_count, &arena) ||
        !rbprep_refresh_collect_named(&pdb, 6, colors, color_count, &arena) ||
        !rbprep_refresh_collect_tracks(&pdb, tracks, track_count,
                                       artists, artist_count,
                                       genres, genre_count, keys, key_count,
                                       colors, color_count, &arena) ||
        !rbprep_refresh_collect_nodes(&pdb, nodes, node_count, &arena) ||
        !rbprep_refresh_collect_entries(&pdb, entries, entry_count))
        goto done;
    rb->close(pdb.fd);
    pdb.fd = -1;
    activity_ticker_ping(500);

    stage = "index write";
    if (!rbprep_refresh_write_index(tracks, track_count, track_map,
                                    nodes, node_count, entries, entry_count,
                                    sort_order) ||
        !rbprep_refresh_write_genres(tracks, track_count, genre_names))
        goto done;
    stage = "index install";
    if (!rbprep_refresh_replace(RBPREP_INDEX, RBPREP_INDEX_NEW,
                                RBPREP_INDEX_PREV))
        goto done;
    index_replaced = true;
    stage = "genre install";
    if (!rbprep_refresh_replace(RBPREP_GENRES, RBPREP_GENRES_NEW,
                                RBPREP_GENRES_PREV))
        goto done;
    genre_replaced = true;
    stage = "source state";
    if (!rbprep_refresh_write_source_state(&current))
        goto done;
    rb->remove(RBPREP_INDEX_PREV);
    rb->remove(RBPREP_GENRES_PREV);
    activity_ticker_ping(1000);
    result = 1;

done:
    if (pdb.fd >= 0)
        rb->close(pdb.fd);
    if (result < 0) {
        if (!rbprep_burn_failure_detail[0])
            rb->strlcpy(rbprep_burn_failure_detail, stage,
                        sizeof(rbprep_burn_failure_detail));
        if (genre_replaced)
            burn_restore_previous(RBPREP_GENRES, RBPREP_GENRES_PREV);
        if (index_replaced)
            burn_restore_previous(RBPREP_INDEX, RBPREP_INDEX_PREV);
        rb->remove(RBPREP_INDEX_NEW);
        rb->remove(RBPREP_GENRES_NEW);
    }
    if (memory)
        rb->plugin_release_audio_buffer();
    return result;
}

struct rbprep_refresh_anlz_tag {
    uint32_t offset;
    uint32_t header_size;
    uint32_t size;
    bool found;
};

struct rbprep_refresh_output {
    int fd;
    int used;
};

static unsigned char rbprep_refresh_output_buffer[4096];

static bool rbprep_refresh_output_flush(struct rbprep_refresh_output *output)
{
    if (!output->used)
        return true;
    if (!rbprep_refresh_write_all(output->fd, rbprep_refresh_output_buffer,
                                  output->used))
        return false;
    output->used = 0;
    return true;
}

static bool rbprep_refresh_output_add(struct rbprep_refresh_output *output,
                                      const void *data, int size)
{
    const unsigned char *source = data;

    while (size > 0) {
        int amount = MIN(size, (int)sizeof(rbprep_refresh_output_buffer) -
                                   output->used);
        rb->memcpy(rbprep_refresh_output_buffer + output->used,
                   source, amount);
        output->used += amount;
        source += amount;
        size -= amount;
        if (output->used == (int)sizeof(rbprep_refresh_output_buffer) &&
            !rbprep_refresh_output_flush(output))
            return false;
    }
    return true;
}

static bool rbprep_refresh_find_anlz_tag(const char *path, const char *kind,
                                         struct rbprep_refresh_anlz_tag *tag)
{
    unsigned char raw[24];
    uint32_t cursor;
    uint32_t header_size;
    uint32_t declared_size;
    off_t actual_size;
    int fd;

    rb->memset(tag, 0, sizeof(*tag));
    fd = rb->open(path, O_RDONLY);
    if (fd < 0)
        return false;
    actual_size = rb->filesize(fd);
    if (!burn_read_at(fd, 0, raw, 12) || rb->memcmp(raw, "PMAI", 4))
        goto invalid;
    header_size = burn_be32(raw + 4);
    declared_size = burn_be32(raw + 8);
    if (header_size < 12 || declared_size != (uint32_t)actual_size ||
        header_size > declared_size)
        goto invalid;
    cursor = header_size;
    while (cursor < declared_size) {
        uint32_t tag_header;
        uint32_t tag_size;

        if (declared_size - cursor < 12 ||
            !burn_read_at(fd, cursor, raw, 12))
            goto invalid;
        tag_header = burn_be32(raw + 4);
        tag_size = burn_be32(raw + 8);
        if (tag_header < 12 || tag_size < tag_header ||
            tag_size > declared_size - cursor)
            goto invalid;
        if (!rb->memcmp(raw, kind, 4)) {
            tag->offset = cursor;
            tag->header_size = tag_header;
            tag->size = tag_size;
            tag->found = true;
            rb->close(fd);
            return true;
        }
        cursor += tag_size;
    }
    if (cursor != declared_size)
        goto invalid;
    rb->close(fd);
    return true;
invalid:
    rb->close(fd);
    return false;
}

static int rbprep_refresh_count_cues(
    const char *path, const struct rbprep_refresh_anlz_tag *tag,
    bool extended)
{
    unsigned char raw[48];
    uint32_t cursor;
    uint32_t end;
    int declared;
    int valid = 0;
    int fd;
    int i;

    if (!tag->found)
        return 0;
    fd = rb->open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    if (!burn_read_at(fd, tag->offset, raw, extended ? 20 : 24) ||
        burn_be32(raw + 12) != 1) {
        rb->close(fd);
        return 0;
    }
    declared = burn_be16(raw + (extended ? 16 : 18));
    cursor = tag->offset + (extended ? 20 : 24);
    end = tag->offset + tag->size;
    for (i = 0; i < declared; i++) {
        uint32_t size;
        uint32_t slot;
        int minimum = extended ? 48 : 56;

        if (cursor > end || end - cursor < (uint32_t)minimum ||
            !burn_read_at(fd, cursor, raw, minimum)) {
            rb->close(fd);
            return -1;
        }
        size = burn_be32(raw + 8);
        slot = burn_be32(raw + 12);
        if (size < (uint32_t)minimum || size > end - cursor ||
            (extended && 48 + burn_be32(raw + 40) > size)) {
            rb->close(fd);
            return -1;
        }
        if (slot >= 1 && slot <= 16)
            valid++;
        cursor += size;
    }
    rb->close(fd);
    return valid;
}

static unsigned char rbprep_refresh_cue_color(unsigned char red,
                                              unsigned char green,
                                              unsigned char blue)
{
    static const unsigned char palette[8][3] = {
        {255, 0, 0}, {255, 94, 0}, {255, 232, 0}, {26, 255, 0},
        {0, 224, 255}, {0, 0, 255}, {77, 0, 255}, {255, 0, 161}
    };
    uint32_t best_distance = 0xffffffffu;
    int best = 3;
    int i;

    if (red == 0 && green == 0 && blue == 0)
        return 3;
    for (i = 0; i < 8; i++) {
        int dr = (int)red - palette[i][0];
        int dg = (int)green - palette[i][1];
        int db = (int)blue - palette[i][2];
        uint32_t distance = dr * dr + dg * dg + db * db;
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

static bool rbprep_refresh_write_cues(
    struct rbprep_refresh_output *output, const char *path,
    const struct rbprep_refresh_anlz_tag *tag, bool extended)
{
    unsigned char raw[56];
    unsigned char encoded[8];
    uint32_t cursor;
    uint32_t end;
    int declared;
    int fd;
    int i;

    if (!tag->found)
        return true;
    fd = rb->open(path, O_RDONLY);
    if (fd < 0 ||
        !burn_read_at(fd, tag->offset, raw, extended ? 20 : 24)) {
        if (fd >= 0)
            rb->close(fd);
        return false;
    }
    if (burn_be32(raw + 12) != 1) {
        rb->close(fd);
        return true;
    }
    declared = burn_be16(raw + (extended ? 16 : 18));
    cursor = tag->offset + (extended ? 20 : 24);
    end = tag->offset + tag->size;
    for (i = 0; i < declared; i++) {
        uint32_t size;
        uint32_t slot;
        uint32_t time;
        unsigned char color = 3;
        int minimum = extended ? 48 : 56;

        if (cursor > end || end - cursor < (uint32_t)minimum ||
            !burn_read_at(fd, cursor, raw, minimum)) {
            rb->close(fd);
            return false;
        }
        size = burn_be32(raw + 8);
        slot = burn_be32(raw + 12);
        if (size < (uint32_t)minimum || size > end - cursor) {
            rb->close(fd);
            return false;
        }
        time = burn_be32(raw + (extended ? 20 : 32));
        if (extended) {
            uint32_t comment = burn_be32(raw + 40);
            unsigned char rgb[4];
            if (48 + comment > size ||
                !burn_read_at(fd, cursor + 44 + comment, rgb, sizeof(rgb))) {
                rb->close(fd);
                return false;
            }
            color = rbprep_refresh_cue_color(rgb[1], rgb[2], rgb[3]);
        }
        if (slot >= 1 && slot <= 16) {
            write_u32(encoded, time);
            encoded[4] = color;
            encoded[5] = slot;
            encoded[6] = encoded[7] = 0;
            if (!rbprep_refresh_output_add(output, encoded, sizeof(encoded))) {
                rb->close(fd);
                return false;
            }
        }
        cursor += size;
    }
    rb->close(fd);
    return true;
}

static bool rbprep_refresh_write_waveform(
    struct rbprep_refresh_output *output, const char *path,
    const struct rbprep_refresh_anlz_tag *tag, uint32_t source_points,
    uint32_t output_points)
{
    uint32_t source_index = 0;
    uint32_t target_index = 0;
    uint32_t target_end = source_points / output_points;
    unsigned char peak[4] = {0, 0, 0, 0};
    int fd = rb->open(path, O_RDONLY);

    if (fd < 0 || rb->lseek(fd, tag->offset + tag->header_size,
                            SEEK_SET) < 0)
        goto fail;
    if (!target_end)
        target_end = 1;
    while (source_index < source_points) {
        uint32_t remaining = source_points - source_index;
        int samples = MIN((uint32_t)(sizeof(rbprep_burn_page) / 2),
                          remaining);
        int got = rb->read(fd, rbprep_burn_page, samples * 2);
        int i;

        if (got != samples * 2)
            goto fail;
        for (i = 0; i < samples; i++, source_index++) {
            uint16_t value = burn_be16(rbprep_burn_page + i * 2);
            unsigned char sample[4];
            int red;

            sample[0] = (((value & 0x7c) >> 2) * 255 + 15) / 31;
            red = ((value & 0xe000) >> 12) * 36;
            sample[1] = MIN(255, red);
            sample[2] = ((value & 0x1c00) >> 10) * 36;
            sample[3] = ((value & 0x0380) >> 7) * 36;
            if (sample[0] >= peak[0])
                rb->memcpy(peak, sample, sizeof(peak));
            if (source_index + 1 >= target_end) {
                if (!rbprep_refresh_output_add(output, peak, sizeof(peak)))
                    goto fail;
                target_index++;
                rb->memset(peak, 0, sizeof(peak));
                if (target_index < output_points) {
                    target_end = (uint32_t)(((uint64_t)(target_index + 1) *
                                             source_points) / output_points);
                    if (target_end <= source_index + 1)
                        target_end = source_index + 2;
                }
            }
        }
        if (!(source_index & 0x3fff)) {
            activity_ticker_ping(650 + (int)((uint64_t)source_index * 240 /
                                             MAX(1u, source_points)));
            rb->yield();
        }
    }
    rb->close(fd);
    return target_index == output_points;
fail:
    if (fd >= 0)
        rb->close(fd);
    return false;
}

static bool rbprep_refresh_write_beats(
    struct rbprep_refresh_output *output, const char *path,
    const struct rbprep_refresh_anlz_tag *tag, uint32_t count)
{
    unsigned char source[8];
    unsigned char encoded[8];
    int fd;
    uint32_t i;

    if (!count)
        return true;
    fd = rb->open(path, O_RDONLY);
    if (fd < 0 || rb->lseek(fd, tag->offset + tag->header_size,
                            SEEK_SET) < 0)
        goto fail;
    for (i = 0; i < count; i++) {
        uint16_t beat;
        uint16_t bpm;
        if (rb->read(fd, source, sizeof(source)) != sizeof(source))
            goto fail;
        beat = burn_be16(source);
        bpm = burn_be16(source + 2);
        write_u32(encoded, burn_be32(source + 4));
        encoded[4] = MAX(1, MIN(4, beat));
        encoded[5] = 0;
        write_u16(encoded + 6, MAX(1, bpm));
        if (!rbprep_refresh_output_add(output, encoded, sizeof(encoded)))
            goto fail;
    }
    rb->close(fd);
    return true;
fail:
    if (fd >= 0)
        rb->close(fd);
    return false;
}

static bool rbprep_refresh_validate_track_cache_once(
    const char *path, uint64_t expected_size, uint32_t waveform_points,
    uint16_t cue_count, uint16_t beat_count)
{
    unsigned char header[40];
    int fd = rb->open(path, O_RDONLY);
    bool valid = fd >= 0 && read_exact(fd, header, sizeof(header)) &&
                 !rb->memcmp(header, "RBW3", 4) &&
                 read_u16(header + 4) == sizeof(header) &&
                 read_u32(header + 8) == waveform_points &&
                 read_u16(header + 12) == cue_count &&
                 read_u16(header + 14) == beat_count &&
                 expected_size <= 0x7fffffffu &&
                 rb->filesize(fd) == (off_t)expected_size;

    if (fd >= 0)
        rb->close(fd);
    return valid;
}

static bool rbprep_refresh_validate_track_cache(
    const char *path, uint64_t expected_size, uint32_t waveform_points,
    uint16_t cue_count, uint16_t beat_count)
{
    int attempt;

    for (attempt = 0; attempt < 4; attempt++) {
        if (rbprep_refresh_validate_track_cache_once(
                path, expected_size, waveform_points, cue_count, beat_count))
            return true;
        rb->sleep(MAX(1, HZ / 20));
    }
    return false;
}

static bool rbprep_refresh_build_track_cache(uint32_t track_id)
{
    struct rbprep_refresh_anlz_tag waveform;
    struct rbprep_refresh_anlz_tag beats;
    struct rbprep_refresh_anlz_tag cues;
    struct rbprep_refresh_output output;
    struct rbprep_pdb pdb;
    struct rbprep_pdb_track pdb_track;
    struct rbprep_track_record local_track;
    unsigned char header[40];
    unsigned char raw[24];
    char dat[MAX_PATH] = "";
    char ext[MAX_PATH] = "";
    char destination[MAX_PATH] = "";
    char temporary[MAX_PATH] = "";
    char previous[MAX_PATH] = "";
    uint32_t source_points;
    uint32_t stored_points;
    uint32_t beat_count = 0;
    uint32_t duration;
    uint32_t phase = 0;
    int cue_count;
    int fd = -1;
    int track_index;
    uint64_t expected_size = 0;
    bool extended_cues = false;
    bool replaced = false;
    bool ok = false;

    rb->memset(&pdb, 0, sizeof(pdb));
    pdb.fd = -1;
    output.fd = -1;
    output.used = 0;
    if (!rbprep_refresh_pdb_open(&pdb, RBPREP_PDB) ||
        !pdb_find_track(&pdb, track_id, &pdb_track))
        goto done;
    rb->strlcpy(dat, pdb_track.analysis_path, sizeof(dat));
    rb->close(pdb.fd);
    pdb.fd = -1;
    if (!dat[0] || rb->strlen(dat) + 1 > sizeof(ext))
        goto done;
    rb->strlcpy(ext, dat, sizeof(ext));
    {
        char *suffix = rb->strrchr(ext, '.');
        if (!suffix)
            goto done;
        rb->strlcpy(suffix, ".EXT", sizeof(ext) - (suffix - ext));
    }
    if (!rbprep_refresh_find_anlz_tag(ext, "PWV5", &waveform) ||
        !waveform.found || waveform.header_size < 24 ||
        !burn_read_at((fd = rb->open(ext, O_RDONLY)), waveform.offset,
                      raw, sizeof(raw)))
        goto done;
    rb->close(fd);
    fd = -1;
    if (burn_be32(raw + 12) != 2)
        goto done;
    source_points = burn_be32(raw + 16);
    if (!source_points ||
        (uint64_t)waveform.header_size + (uint64_t)source_points * 2 >
            waveform.size)
        goto done;
    stored_points = MIN((uint32_t)RBPREP_POINTS, source_points);
    duration = (uint32_t)MIN(0xffffffffu,
        ((uint64_t)source_points * 1000 + 149) / 150);

    rb->memset(&beats, 0, sizeof(beats));
    if (!rbprep_refresh_find_anlz_tag(dat, "PQTZ", &beats))
        goto done;
    if (beats.found) {
        fd = rb->open(dat, O_RDONLY);
        if (fd < 0 || !burn_read_at(fd, beats.offset, raw, 24))
            goto done;
        rb->close(fd);
        fd = -1;
        beat_count = MIN(65535u, burn_be32(raw + 20));
        if (beats.header_size < 24 ||
            (uint64_t)beats.header_size + (uint64_t)beat_count * 8 >
                beats.size)
            goto done;
        if (beat_count) {
            fd = rb->open(dat, O_RDONLY);
            if (fd < 0 || !burn_read_at(fd, beats.offset +
                                        beats.header_size + 4, raw, 4))
                goto done;
            rb->close(fd);
            fd = -1;
            phase = burn_be32(raw);
        }
    }

    rb->memset(&cues, 0, sizeof(cues));
    if (!rbprep_refresh_find_anlz_tag(ext, "PCO2", &cues))
        goto done;
    if (cues.found) {
        extended_cues = true;
        cue_count = rbprep_refresh_count_cues(ext, &cues, true);
    } else {
        if (!rbprep_refresh_find_anlz_tag(dat, "PCOB", &cues))
            goto done;
        cue_count = rbprep_refresh_count_cues(dat, &cues, false);
    }
    if (cue_count < 0 || cue_count > 65535)
        goto done;

    track_index = find_track_index_by_id(track_id);
    rb->memset(&local_track, 0, sizeof(local_track));
    local_track.color = 8;
    if (track_index >= 0)
        read_track_record(track_index, &local_track);
    rb->mkdir(RBPREP_TRACK_DIR);
    rb->snprintf(destination, sizeof(destination), "%s/%06lu.rbw",
                 RBPREP_TRACK_DIR, (unsigned long)track_id);
    rb->snprintf(temporary, sizeof(temporary), "%s/%06lu.rbw.tmp",
                 RBPREP_TRACK_DIR, (unsigned long)track_id);
    rb->snprintf(previous, sizeof(previous), "%s/%06lu.rbw.rekordpod-prev",
                 RBPREP_TRACK_DIR, (unsigned long)track_id);
    if (!rb->file_exists(destination) && rb->file_exists(previous))
        rb->rename(previous, destination);
    rb->remove(temporary);
    output.fd = rb->open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    output.used = 0;
    if (output.fd < 0)
        goto done;
    rb->memset(header, 0, sizeof(header));
    rb->memcpy(header, "RBW3", 4);
    write_u16(header + 4, 40);
    write_u32(header + 8, stored_points);
    write_u16(header + 12, cue_count);
    write_u16(header + 14, beat_count);
    write_u32(header + 16, duration);
    write_u32(header + 20, duration);
    write_u16(header + 24, MIN(65535u, pdb_track.source_bpm_x100));
    write_u16(header + 26, 150);
    write_u32(header + 28, phase);
    header[32] = MIN(5, local_track.rating);
    header[33] = MIN(8, local_track.color);
    write_u32(header + 36, source_points);
    if (!rbprep_refresh_write_all(output.fd, header, sizeof(header)) ||
        !rbprep_refresh_write_waveform(&output, ext, &waveform,
                                       source_points, stored_points) ||
        !rbprep_refresh_write_cues(&output, extended_cues ? ext : dat,
                                   &cues, extended_cues) ||
        !rbprep_refresh_write_beats(&output, dat, &beats, beat_count) ||
        !rbprep_refresh_output_flush(&output))
        goto done;
    expected_size = 40 + (uint64_t)stored_points * 4 +
                    (uint64_t)cue_count * 8 + (uint64_t)beat_count * 8;
    if (rb->filesize(output.fd) != (off_t)expected_size)
        goto done;
    if (rb->close(output.fd) < 0) {
        output.fd = -1;
        goto done;
    }
    output.fd = -1;
    if (rb->file_exists(destination)) {
        if (!burn_swap_keep_previous(destination, temporary, previous))
            goto done;
        replaced = true;
    } else if (rb->rename(temporary, destination) < 0) {
        goto done;
    }
    if (!rbprep_refresh_validate_track_cache(
            destination, expected_size, stored_points, cue_count,
            beat_count)) {
        if (replaced)
            burn_restore_previous(destination, previous);
        else
            rb->remove(destination);
        goto done;
    }
    rb->remove(previous);
    activity_ticker_ping(1000);
    ok = true;
done:
    if (fd >= 0)
        rb->close(fd);
    if (pdb.fd >= 0)
        rb->close(pdb.fd);
    if (output.fd >= 0)
        rb->close(output.fd);
    if (!ok && temporary[0])
        rb->remove(temporary);
    return ok;
}
