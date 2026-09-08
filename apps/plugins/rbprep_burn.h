/* RBPrep's on-device rekordbox transaction writer.
 *
 * This is included by rbprep.c after the edit-journal helpers so it can share
 * the plugin's compact RBE1 format without exporting a second plugin ABI.
 * Every mutable rekordbox file gets a persistent baseline backup and a
 * same-directory temporary replacement before it is renamed into place.
 */

#define RBPREP_PDB_NEW  RBPREP_PDB ".rbprep-new"
#define RBPREP_PDB_PREV RBPREP_PDB ".rbprep-prev"
#define RBPREP_PDB_BAK  RBPREP_PDB ".rbprep-bak"
#define RBPREP_PDB_PAGE_MAX 4096
#define RBPREP_PDB_TOUCH_MAX 32

struct rbprep_pdb_touch {
    uint32_t table;
    uint32_t page;
    int original_slots;
    int appends;
    uint32_t generation;
};

struct rbprep_pdb {
    int fd;
    uint32_t page_size;
    uint32_t table_count;
    bool structural;
    int touch_count;
    struct rbprep_pdb_touch touches[RBPREP_PDB_TOUCH_MAX];
};

struct rbprep_pdb_track {
    uint32_t row;
    uint32_t source_bpm_x100;
    char analysis_path[MAX_PATH];
};

static unsigned char rbprep_burn_page[RBPREP_PDB_PAGE_MAX];

static uint16_t burn_be16(const unsigned char *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t burn_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void burn_put_be16(unsigned char *p, uint16_t value)
{
    p[0] = value >> 8;
    p[1] = value;
}

static void burn_put_be32(unsigned char *p, uint32_t value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

static bool burn_read_at(int fd, uint32_t offset, void *data, size_t size)
{
    return rb->lseek(fd, offset, SEEK_SET) >= 0 &&
           rb->read(fd, data, size) == (ssize_t)size;
}

static bool burn_write_at(int fd, uint32_t offset, const void *data,
                          size_t size)
{
    return rb->lseek(fd, offset, SEEK_SET) >= 0 &&
           rb->write(fd, data, size) == (ssize_t)size;
}

static bool burn_copy_file(const char *source, const char *destination)
{
    int input;
    int output;
    int count;

    input = rb->open(source, O_RDONLY);
    if (input < 0)
        return false;
    output = rb->open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output < 0) {
        rb->close(input);
        return false;
    }
    while ((count = rb->read(input, rbprep_burn_page,
                             sizeof(rbprep_burn_page))) > 0) {
        if (rb->write(output, rbprep_burn_page, count) != count) {
            rb->close(input);
            rb->close(output);
            rb->remove(destination);
            return false;
        }
    }
    rb->close(input);
    rb->close(output);
    if (count < 0) {
        rb->remove(destination);
        return false;
    }
    return true;
}

static bool burn_backup_once(const char *path, const char *backup)
{
    return rb->file_exists(backup) || burn_copy_file(path, backup);
}

static bool burn_swap_keep_previous(const char *path, const char *temporary,
                                    const char *previous)
{
    rb->remove(previous);
    if (rb->rename(path, previous) < 0)
        return false;
    if (rb->rename(temporary, path) < 0) {
        rb->rename(previous, path);
        return false;
    }
    return true;
}

static void burn_restore_previous(const char *path, const char *previous)
{
    if (!rb->file_exists(previous))
        return;
    rb->remove(path);
    rb->rename(previous, path);
}

static bool pdb_open(struct rbprep_pdb *pdb, const char *path)
{
    unsigned char header[28];

    rb->memset(pdb, 0, sizeof(*pdb));
    pdb->fd = rb->open(path, O_RDWR);
    if (pdb->fd < 0 ||
        !burn_read_at(pdb->fd, 0, header, sizeof(header)))
        goto fail;
    pdb->page_size = read_u32(header + 4);
    pdb->table_count = read_u32(header + 8);
    if (read_u32(header) != 0 || pdb->page_size != 4096 ||
        pdb->page_size > RBPREP_PDB_PAGE_MAX ||
        pdb->table_count == 0 || pdb->table_count > 64)
        goto fail;
    return true;
fail:
    if (pdb->fd >= 0)
        rb->close(pdb->fd);
    pdb->fd = -1;
    return false;
}

static bool pdb_table_entry(struct rbprep_pdb *pdb, uint32_t type,
                            uint32_t *entry)
{
    unsigned char raw[4];
    uint32_t i;

    for (i = 0; i < pdb->table_count; i++) {
        uint32_t offset = 0x1c + i * 16;
        if (!burn_read_at(pdb->fd, offset, raw, sizeof(raw)))
            return false;
        if (read_u32(raw) == type) {
            *entry = offset;
            return true;
        }
    }
    return false;
}

static int pdb_slot_count(const unsigned char *page)
{
    return page[0x18] + 0x100 * (page[0x19] & 1);
}

static int pdb_directory_bytes(int slots)
{
    return slots ? 2 * slots + 4 * ((slots + 15) / 16) : 0;
}

static bool pdb_row_present(const unsigned char *page, int page_size,
                            int slot)
{
    int group = slot / 16;
    int bit = slot & 15;
    int base = page_size - group * 0x24;
    return !!(read_u16(page + base - 4) & (1u << bit));
}

static uint16_t pdb_row_heap_offset(const unsigned char *page, int page_size,
                                    int slot)
{
    int group = slot / 16;
    int bit = slot & 15;
    int base = page_size - group * 0x24;
    return read_u16(page + base - 6 - bit * 2);
}

static int pdb_present_count(const unsigned char *page, int page_size,
                             int slots)
{
    int group;
    int count = 0;

    for (group = 0; group < (slots + 15) / 16; group++) {
        int base = page_size - group * 0x24;
        int bits = MIN(16, slots - group * 16);
        uint16_t flags = read_u16(page + base - 4);
        int bit;
        for (bit = 0; bit < bits; bit++)
            if (flags & (1u << bit))
                count++;
    }
    return count;
}

static bool pdb_next_page(struct rbprep_pdb *pdb, uint32_t page,
                          uint32_t *next)
{
    unsigned char raw[4];
    if (!burn_read_at(pdb->fd, page * pdb->page_size + 0x0c,
                      raw, sizeof(raw)))
        return false;
    *next = read_u32(raw);
    return true;
}

static bool pdb_decode_string(struct rbprep_pdb *pdb, uint32_t offset,
                              char *buffer, size_t size)
{
    unsigned char header[4];
    size_t used = 0;
    int bytes;

    if (size == 0 || !burn_read_at(pdb->fd, offset, header, 1))
        return false;
    if (header[0] & 1) {
        bytes = (header[0] >> 1) - 1;
        bytes = MIN(bytes, (int)size - 1);
        if (bytes > 0 && !burn_read_at(pdb->fd, offset + 1,
                                      buffer, bytes))
            return false;
        buffer[bytes] = '\0';
        return true;
    }
    if (!burn_read_at(pdb->fd, offset, header, sizeof(header)))
        return false;
    bytes = read_u16(header + 1) - 4;
    if (bytes < 0)
        return false;
    if (header[0] == 0x40) {
        bytes = MIN(bytes, (int)size - 1);
        if (bytes > 0 && !burn_read_at(pdb->fd, offset + 4,
                                      buffer, bytes))
            return false;
        buffer[bytes] = '\0';
        return true;
    }
    if (header[0] == 0x90) {
        int i;
        for (i = 0; i + 1 < bytes && used + 1 < size; i += 2) {
            unsigned char pair[2];
            uint16_t code;
            if (!burn_read_at(pdb->fd, offset + 4 + i, pair, 2))
                return false;
            code = read_u16(pair);
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

static bool pdb_find_track(struct rbprep_pdb *pdb, uint32_t track_id,
                           struct rbprep_pdb_track *track)
{
    uint32_t entry;
    unsigned char table[16];
    uint32_t page;
    uint32_t last;
    int guard = 0;

    if (!pdb_table_entry(pdb, 0, &entry) ||
        !burn_read_at(pdb->fd, entry, table, sizeof(table)))
        return false;
    page = read_u32(table + 8);
    last = read_u32(table + 12);
    while (guard++ < 100000) {
        int slot;
        int slots;
        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return false;
        slots = pdb_slot_count(rbprep_burn_page);
        if (!(rbprep_burn_page[0x1b] & 0x40)) {
            for (slot = 0; slot < slots; slot++) {
                uint32_t row;
                unsigned char fixed[0x88];
                uint16_t string_offset;
                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                row = page * pdb->page_size + 0x28 +
                      pdb_row_heap_offset(rbprep_burn_page,
                                          pdb->page_size, slot);
                if (!burn_read_at(pdb->fd, row, fixed, sizeof(fixed)))
                    return false;
                if (read_u32(fixed + 0x48) != track_id)
                    continue;
                track->row = row;
                track->source_bpm_x100 = read_u32(fixed + 0x38);
                string_offset = read_u16(fixed + 0x5e + 14 * 2);
                track->analysis_path[0] = '\0';
                pdb_decode_string(pdb, row + string_offset,
                                  track->analysis_path,
                                  sizeof(track->analysis_path));
                return true;
            }
        }
        if (page == last)
            break;
        if (!pdb_next_page(pdb, page, &page))
            return false;
    }
    return false;
}

static struct rbprep_pdb_touch *pdb_touch(struct rbprep_pdb *pdb,
                                          uint32_t table, uint32_t page,
                                          int original_slots)
{
    struct rbprep_pdb_touch *touch;
    uint32_t entry;
    unsigned char table_data[16];
    uint32_t cursor;
    uint32_t last;
    uint32_t generation = 0;
    int i;
    int guard = 0;
    bool generation_known = false;

    for (i = 0; i < pdb->touch_count; i++) {
        if (pdb->touches[i].table != table)
            continue;
        if (pdb->touches[i].page == page)
            return &pdb->touches[i];
        generation = pdb->touches[i].generation;
        generation_known = true;
    }
    if (pdb->touch_count >= RBPREP_PDB_TOUCH_MAX ||
        !pdb_table_entry(pdb, table, &entry) ||
        !burn_read_at(pdb->fd, entry, table_data, sizeof(table_data)))
        return NULL;
    if (!generation_known) {
        cursor = read_u32(table_data + 8);
        last = read_u32(table_data + 12);
        while (guard++ < 100000) {
            unsigned char raw[4];
            uint32_t next;
            if (!burn_read_at(pdb->fd, cursor * pdb->page_size + 0x10,
                              raw, 4))
                return NULL;
            generation = MAX(generation, read_u32(raw));
            if (cursor == last)
                break;
            if (!pdb_next_page(pdb, cursor, &next))
                return NULL;
            cursor = next;
        }
        generation++;
    }
    touch = &pdb->touches[pdb->touch_count++];
    touch->table = table;
    touch->page = page;
    touch->original_slots = original_slots;
    touch->appends = 0;
    touch->generation = generation;
    pdb->structural = true;
    return touch;
}

static bool pdb_allocate_page(struct rbprep_pdb *pdb, uint32_t table,
                              uint32_t entry, uint32_t old_last,
                              uint32_t *new_page)
{
    unsigned char raw[4];
    uint32_t candidate;
    uint32_t next_unused;
    uint32_t old_flags;

    if (!burn_read_at(pdb->fd, entry + 4, raw, 4))
        return false;
    candidate = read_u32(raw);
    if (!burn_read_at(pdb->fd, 12, raw, 4))
        return false;
    next_unused = read_u32(raw);
    rb->memset(rbprep_burn_page, 0, pdb->page_size);
    write_u32(rbprep_burn_page + 0x04, candidate);
    write_u32(rbprep_burn_page + 0x08, table);
    write_u32(rbprep_burn_page + 0x0c, next_unused);
    rbprep_burn_page[0x1b] = 0x24;
    write_u16(rbprep_burn_page + 0x1c, pdb->page_size - 0x28);
    if (!burn_write_at(pdb->fd, candidate * pdb->page_size,
                       rbprep_burn_page, pdb->page_size))
        return false;
    write_u32(raw, candidate);
    if (!burn_write_at(pdb->fd, entry + 12, raw, 4))
        return false;
    write_u32(raw, next_unused);
    if (!burn_write_at(pdb->fd, entry + 4, raw, 4))
        return false;
    write_u32(raw, next_unused + 1);
    if (!burn_write_at(pdb->fd, 12, raw, 4))
        return false;

    if (!burn_read_at(pdb->fd, old_last * pdb->page_size + 0x1b,
                      raw, 1))
        return false;
    old_flags = raw[0];
    if (old_flags & 0x40) {
        unsigned char heap[8];
        if (!burn_read_at(pdb->fd, old_last * pdb->page_size + 0x28,
                          heap, sizeof(heap)))
            return false;
        if (read_u32(heap + 4) == 0x03ffffffu) {
            write_u32(heap + 4, candidate);
            if (!burn_write_at(pdb->fd,
                               old_last * pdb->page_size + 0x28,
                               heap, sizeof(heap)))
                return false;
        }
    }
    *new_page = candidate;
    return true;
}

static bool pdb_append_row(struct rbprep_pdb *pdb, uint32_t table,
                           const unsigned char *row, int row_size, int alloc)
{
    uint32_t entry;
    unsigned char table_data[16];
    uint32_t page;
    int slots;
    int used;
    struct rbprep_pdb_touch *touch;
    int group;
    int bit;
    int base;
    int row_start;
    int new_slots;
    int present;
    int i;

    if (!pdb_table_entry(pdb, table, &entry) ||
        !burn_read_at(pdb->fd, entry, table_data, sizeof(table_data)))
        return false;
    page = read_u32(table_data + 12);
    if (!burn_read_at(pdb->fd, page * pdb->page_size,
                      rbprep_burn_page, pdb->page_size))
        return false;
    if (rbprep_burn_page[0x1b] & 0x40) {
        if (!pdb_allocate_page(pdb, table, entry, page, &page) ||
            !burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return false;
    }
    slots = pdb_slot_count(rbprep_burn_page);
    used = read_u16(rbprep_burn_page + 0x1e);
    if (0x28 + used + alloc + pdb_directory_bytes(slots + 1) >
        (int)pdb->page_size) {
        if (!pdb_allocate_page(pdb, table, entry, page, &page) ||
            !burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return false;
        slots = used = 0;
    }
    if (slots >= 511 || alloc < row_size)
        return false;
    touch = pdb_touch(pdb, table, page, slots);
    if (!touch)
        return false;
    if (touch->appends == 0) {
        for (i = 0; i < (slots + 15) / 16; i++) {
            int group_base = pdb->page_size - i * 0x24;
            write_u16(rbprep_burn_page + group_base - 2, 0);
        }
    }
    group = slots / 16;
    bit = slots & 15;
    base = pdb->page_size - group * 0x24;
    if (bit == 0) {
        write_u16(rbprep_burn_page + base - 4, 0);
        write_u16(rbprep_burn_page + base - 2, 0);
    }
    row_start = 0x28 + used;
    rb->memset(rbprep_burn_page + row_start, 0, alloc);
    rb->memcpy(rbprep_burn_page + row_start, row, row_size);
    write_u16(rbprep_burn_page + base - 6 - bit * 2, used);
    write_u16(rbprep_burn_page + base - 4,
              read_u16(rbprep_burn_page + base - 4) | (1u << bit));
    write_u16(rbprep_burn_page + base - 2,
              read_u16(rbprep_burn_page + base - 2) | (1u << bit));
    touch->appends++;
    new_slots = slots + 1;
    present = pdb_present_count(rbprep_burn_page, pdb->page_size, new_slots);
    rbprep_burn_page[0x18] = new_slots & 0xff;
    write_u16(rbprep_burn_page + 0x19,
              0x20 * present | (new_slots > 255 ? 1 : 0));
    used += alloc;
    write_u16(rbprep_burn_page + 0x1e, used);
    write_u16(rbprep_burn_page + 0x1c,
              pdb->page_size - 0x28 - used -
              pdb_directory_bytes(new_slots));
    write_u16(rbprep_burn_page + 0x20, touch->appends);
    write_u16(rbprep_burn_page + 0x22, touch->original_slots);
    write_u32(rbprep_burn_page + 0x10, touch->generation);
    return burn_write_at(pdb->fd, page * pdb->page_size,
                         rbprep_burn_page, pdb->page_size);
}

static int pdb_encode_name(const char *name, unsigned char *output,
                           int capacity)
{
    int length = rb->strlen(name);
    int i;
    bool ascii = true;

    for (i = 0; i < length; i++)
        if ((unsigned char)name[i] >= 0x80)
            ascii = false;
    if (ascii && length <= 126 && length + 1 <= capacity) {
        output[0] = (length + 1) * 2 + 1;
        rb->memcpy(output + 1, name, length);
        return length + 1;
    }
    if (ascii) {
        if (length + 4 > capacity)
            return -1;
        output[0] = 0x40;
        write_u16(output + 1, length + 4);
        output[3] = 0;
        rb->memcpy(output + 4, name, length);
        return length + 4;
    }
    {
        int source = 0;
        int target = 4;
        output[0] = 0x90;
        output[3] = 0;
        while (source < length && target + 2 <= capacity) {
            uint32_t code = (unsigned char)name[source++];
            if ((code & 0xe0) == 0xc0 && source < length) {
                code = ((code & 0x1f) << 6) |
                       ((unsigned char)name[source++] & 0x3f);
            } else if ((code & 0xf0) == 0xe0 && source + 1 < length) {
                unsigned char middle = name[source++];
                unsigned char low = name[source++];
                code = ((code & 0x0f) << 12) |
                       ((middle & 0x3f) << 6) | (low & 0x3f);
            } else if (code >= 0x80) {
                code = '?';
            }
            write_u16(output + target, code <= 0xffff ? code : '?');
            target += 2;
        }
        if (source != length)
            return -1;
        write_u16(output + 1, target);
        return target;
    }
}

static int pdb_find_genre(struct rbprep_pdb *pdb, const char *name,
                          uint32_t *max_id)
{
    uint32_t entry;
    unsigned char table[16];
    uint32_t page;
    uint32_t last;
    int guard = 0;

    *max_id = 0;
    if (!pdb_table_entry(pdb, 1, &entry) ||
        !burn_read_at(pdb->fd, entry, table, sizeof(table)))
        return -1;
    page = read_u32(table + 8);
    last = read_u32(table + 12);
    while (guard++ < 100000) {
        int slots;
        int slot;
        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return -1;
        slots = pdb_slot_count(rbprep_burn_page);
        if (!(rbprep_burn_page[0x1b] & 0x40)) {
            for (slot = 0; slot < slots; slot++) {
                uint32_t row;
                unsigned char raw[4];
                char existing[32];
                uint32_t id;
                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                row = page * pdb->page_size + 0x28 +
                      pdb_row_heap_offset(rbprep_burn_page,
                                          pdb->page_size, slot);
                if (!burn_read_at(pdb->fd, row, raw, 4))
                    return -1;
                id = read_u32(raw);
                *max_id = MAX(*max_id, id);
                if (pdb_decode_string(pdb, row + 4, existing,
                                      sizeof(existing)) &&
                    !rb->strcasecmp(existing, name))
                    return id;
            }
        }
        if (page == last)
            break;
        if (!pdb_next_page(pdb, page, &page))
            return -1;
    }
    return 0;
}

static int pdb_get_or_create_genre(struct rbprep_pdb *pdb, const char *name)
{
    unsigned char row[80];
    uint32_t max_id;
    int id = pdb_find_genre(pdb, name, &max_id);
    int string_size;
    int row_size;
    int alloc;

    if (id != 0)
        return id;
    write_u32(row, max_id + 1);
    string_size = pdb_encode_name(name, row + 4, sizeof(row) - 4);
    if (string_size < 0)
        return -1;
    row_size = 4 + string_size;
    alloc = (row_size + 3) & ~3;
    if (!pdb_append_row(pdb, 1, row, row_size, alloc))
        return -1;
    return max_id + 1;
}

static bool pdb_patch_snapshot(struct rbprep_pdb *pdb,
                               const unsigned char *snapshot,
                               struct rbprep_pdb_track *track)
{
    uint32_t track_id = read_u32(snapshot + 8);
    uint32_t genre_id;
    unsigned char raw[4];
    unsigned char current_color;
    char genre[32];
    int found;

    if (!pdb_find_track(pdb, track_id, track))
        return false;
    rb->memcpy(genre, snapshot + 120, sizeof(genre));
    genre[sizeof(genre) - 1] = '\0';
    genre_id = 0;
    if (genre[0]) {
        found = pdb_get_or_create_genre(pdb, genre);
        if (found < 0)
            return false;
        genre_id = found;
    }
    write_u32(raw, read_u32(snapshot + 24));
    if (!burn_write_at(pdb->fd, track->row + 0x38, raw, 4))
        return false;
    if (genre_id) {
        write_u32(raw, genre_id);
        if (!burn_write_at(pdb->fd, track->row + 0x3c, raw, 4))
            return false;
    }
    write_u16(raw, read_u16(snapshot + 20));
    if (!burn_write_at(pdb->fd, track->row + 0x50, raw, 2))
        return false;
    if (!burn_read_at(pdb->fd, track->row + 0x58, raw, 2))
        return false;
    current_color = raw[0];
    if ((current_color >= 1 && current_color <= 8
         ? current_color - 1 : 0) != (snapshot[17] & 7) ||
        (current_color == 0 && (snapshot[17] & 7) != 0))
        raw[0] = (snapshot[17] & 7) + 1;
    raw[1] = MIN(5, snapshot[16]);
    return burn_write_at(pdb->fd, track->row + 0x58, raw, 2);
}

static bool pdb_playlist_exists(struct rbprep_pdb *pdb, uint32_t playlist,
                                bool *is_folder)
{
    uint32_t entry;
    unsigned char table[16];
    uint32_t page;
    uint32_t last;
    int guard = 0;

    if (!pdb_table_entry(pdb, 7, &entry) ||
        !burn_read_at(pdb->fd, entry, table, sizeof(table)))
        return false;
    page = read_u32(table + 8);
    last = read_u32(table + 12);
    while (guard++ < 100000) {
        int slots;
        int slot;
        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return false;
        slots = pdb_slot_count(rbprep_burn_page);
        if (!(rbprep_burn_page[0x1b] & 0x40)) {
            for (slot = 0; slot < slots; slot++) {
                uint32_t row;
                unsigned char fixed[20];
                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                row = page * pdb->page_size + 0x28 +
                      pdb_row_heap_offset(rbprep_burn_page,
                                          pdb->page_size, slot);
                if (!burn_read_at(pdb->fd, row, fixed, sizeof(fixed)))
                    return false;
                if (read_u32(fixed + 12) == playlist) {
                    *is_folder = read_u32(fixed + 16) != 0;
                    return true;
                }
            }
        }
        if (page == last)
            break;
        if (!pdb_next_page(pdb, page, &page))
            return false;
    }
    return false;
}

static uint32_t pdb_playlist_by_name(struct rbprep_pdb *pdb,
                                     const char *name)
{
    uint32_t entry;
    unsigned char table[16];
    uint32_t page;
    uint32_t last;
    uint32_t match = 0;
    int guard = 0;

    if (!name || !name[0] || !pdb_table_entry(pdb, 7, &entry) ||
        !burn_read_at(pdb->fd, entry, table, sizeof(table)))
        return 0;
    page = read_u32(table + 8);
    last = read_u32(table + 12);
    while (guard++ < 100000) {
        int slots;
        int slot;
        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return 0;
        slots = pdb_slot_count(rbprep_burn_page);
        if (!(rbprep_burn_page[0x1b] & 0x40)) {
            for (slot = 0; slot < slots; slot++) {
                uint32_t row;
                unsigned char fixed[20];
                char existing[64];
                uint32_t id;
                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                row = page * pdb->page_size + 0x28 +
                      pdb_row_heap_offset(rbprep_burn_page,
                                          pdb->page_size, slot);
                if (!burn_read_at(pdb->fd, row, fixed, sizeof(fixed)))
                    return 0;
                if (read_u32(fixed + 16) != 0 ||
                    !pdb_decode_string(pdb, row + 20, existing,
                                       sizeof(existing)) ||
                    rb->strcasecmp(existing, name))
                    continue;
                id = read_u32(fixed + 12);
                if (match && match != id)
                    return 0;
                match = id;
            }
        }
        if (page == last)
            break;
        if (!pdb_next_page(pdb, page, &page))
            return 0;
    }
    return match;
}

static bool pdb_resolve_playlist(struct rbprep_pdb *pdb,
                                 uint32_t journal_id, const char *name,
                                 uint32_t *resolved)
{
    struct rbprep_node_record node;
    bool folder;
    uint32_t by_name;

    if (pdb_playlist_exists(pdb, journal_id, &folder) && !folder) {
        *resolved = journal_id;
        return true;
    }
    /* RBI1 journals stored the node ordinal. Translate those requests using
       the current device index before falling back to an unambiguous name. */
    if (journal_id < library_node_count &&
        read_node_record(journal_id, &node) && node.source_id &&
        pdb_playlist_exists(pdb, node.source_id, &folder) && !folder) {
        *resolved = node.source_id;
        return true;
    }
    by_name = pdb_playlist_by_name(pdb, name);
    if (by_name) {
        *resolved = by_name;
        return true;
    }
    return false;
}

static int pdb_playlist_entry_index(struct rbprep_pdb *pdb, uint32_t track,
                                    uint32_t playlist, bool *present)
{
    uint32_t entry;
    unsigned char table[16];
    uint32_t page;
    uint32_t last;
    int maximum = 0;
    int guard = 0;

    *present = false;
    if (!pdb_table_entry(pdb, 8, &entry) ||
        !burn_read_at(pdb->fd, entry, table, sizeof(table)))
        return -1;
    page = read_u32(table + 8);
    last = read_u32(table + 12);
    while (guard++ < 100000) {
        int slots;
        int slot;
        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return -1;
        slots = pdb_slot_count(rbprep_burn_page);
        if (!(rbprep_burn_page[0x1b] & 0x40)) {
            for (slot = 0; slot < slots; slot++) {
                uint32_t row;
                unsigned char values[12];
                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                row = page * pdb->page_size + 0x28 +
                      pdb_row_heap_offset(rbprep_burn_page,
                                          pdb->page_size, slot);
                if (!burn_read_at(pdb->fd, row, values, sizeof(values)))
                    return -1;
                if (read_u32(values + 8) != playlist)
                    continue;
                maximum = MAX(maximum, (int)read_u32(values));
                if (read_u32(values + 4) == track)
                    *present = true;
            }
        }
        if (page == last)
            break;
        if (!pdb_next_page(pdb, page, &page))
            return -1;
    }
    return maximum;
}

static bool pdb_add_playlist_entry(struct rbprep_pdb *pdb, uint32_t track,
                                   uint32_t playlist)
{
    unsigned char row[12];
    bool folder;
    bool present;
    int maximum;
    struct rbprep_pdb_track ignored;

    if (!pdb_find_track(pdb, track, &ignored) ||
        !pdb_playlist_exists(pdb, playlist, &folder) || folder)
        return false;
    maximum = pdb_playlist_entry_index(pdb, track, playlist, &present);
    if (maximum < 0)
        return false;
    if (present)
        return true;
    write_u32(row, maximum + 1);
    write_u32(row + 4, track);
    write_u32(row + 8, playlist);
    return pdb_append_row(pdb, 8, row, sizeof(row), sizeof(row));
}

static bool burn_record_is_latest(int fd, uint32_t track_id, off_t after)
{
    unsigned char record[RBPREP_EDIT_RECORD_SIZE];
    bool latest = true;

    if (rb->lseek(fd, after, SEEK_SET) < 0)
        return false;
    while (rb->read(fd, record, sizeof(record)) == sizeof(record)) {
        if (valid_edit_record(record) && read_u32(record + 8) == track_id) {
            latest = false;
            break;
        }
    }
    rb->lseek(fd, after, SEEK_SET);
    return latest;
}

static int burn_source_bpm(uint32_t track_id, int fallback)
{
    char path[MAX_PATH];
    unsigned char header[28];
    int fd;

    rb->snprintf(path, sizeof(path), "%s/%06lu.rbw", RBPREP_TRACK_DIR,
                 (unsigned long)track_id);
    fd = rb->open(path, O_RDONLY);
    if (fd < 0)
        return fallback;
    if (rb->read(fd, header, sizeof(header)) != sizeof(header) ||
        rb->memcmp(header, "RBW3", 4)) {
        rb->close(fd);
        return fallback;
    }
    rb->close(fd);
    return MAX(1, (int)read_u16(header + 24));
}

static int burn_hotcue_count(const unsigned char *snapshot)
{
    int i;
    int count = 0;
    for (i = 0; i < 16; i++)
        if ((int32_t)read_u32(snapshot + 40 + i * 4) >= 0)
            count++;
    return count;
}

static int burn_build_pcob(unsigned char *out, int capacity,
                           const unsigned char *snapshot)
{
    int count = burn_hotcue_count(snapshot);
    int length = 24 + count * 56;
    int slot;
    int ordinal = 0;

    if (length > capacity)
        return -1;
    rb->memset(out, 0, length);
    rb->memcpy(out, "PCOB", 4);
    burn_put_be32(out + 4, 24);
    burn_put_be32(out + 8, length);
    burn_put_be32(out + 12, 1);
    burn_put_be16(out + 18, count);
    burn_put_be32(out + 20, 0xffffffffu);
    for (slot = 0; slot < 16; slot++) {
        int32_t time = read_u32(snapshot + 40 + slot * 4);
        unsigned char *entry;
        if (time < 0)
            continue;
        entry = out + 24 + ordinal * 56;
        rb->memcpy(entry, "PCPT", 4);
        burn_put_be32(entry + 4, 28);
        burn_put_be32(entry + 8, 56);
        burn_put_be32(entry + 12, slot + 1);
        burn_put_be32(entry + 16, 4);
        burn_put_be32(entry + 20, 0x10000);
        burn_put_be16(entry + 24, ordinal == 0 ? 0xffff : ordinal - 1);
        burn_put_be16(entry + 26, ordinal + 1 == count ? 0xffff
                                                       : ordinal + 1);
        entry[28] = 1;
        burn_put_be16(entry + 30, 1000);
        burn_put_be32(entry + 32, time);
        burn_put_be32(entry + 36, 0xffffffffu);
        ordinal++;
    }
    return length;
}

static int burn_build_pco2(unsigned char *out, int capacity,
                           const unsigned char *snapshot)
{
    static const unsigned char rgb_palette[8][3] = {
        {255, 70, 70}, {255, 145, 40}, {250, 220, 45}, {55, 235, 95},
        {50, 225, 225}, {55, 135, 255}, {175, 90, 255}, {255, 80, 185}
    };
    int count = burn_hotcue_count(snapshot);
    int length = 20 + count * 48;
    int slot;
    int ordinal = 0;

    if (length > capacity)
        return -1;
    rb->memset(out, 0, length);
    rb->memcpy(out, "PCO2", 4);
    burn_put_be32(out + 4, 20);
    burn_put_be32(out + 8, length);
    burn_put_be32(out + 12, 1);
    burn_put_be16(out + 16, count);
    for (slot = 0; slot < 16; slot++) {
        int32_t time = read_u32(snapshot + 40 + slot * 4);
        int color = snapshot[104 + slot] & 7;
        unsigned char *entry;
        if (time < 0)
            continue;
        entry = out + 20 + ordinal * 48;
        rb->memcpy(entry, "PCP2", 4);
        burn_put_be32(entry + 4, 16);
        burn_put_be32(entry + 8, 48);
        burn_put_be32(entry + 12, slot + 1);
        entry[16] = 1;
        burn_put_be32(entry + 20, time);
        burn_put_be32(entry + 24, 0xffffffffu);
        entry[28] = color + 1;
        entry[44] = color + 1;
        entry[45] = rgb_palette[color][0];
        entry[46] = rgb_palette[color][1];
        entry[47] = rgb_palette[color][2];
        ordinal++;
    }
    return length;
}

static bool burn_rewrite_analysis(const char *path,
                                  const unsigned char *snapshot,
                                  int source_bpm, unsigned char *memory,
                                  size_t memory_size)
{
    char backup[MAX_PATH];
    char temporary[MAX_PATH];
    char previous[MAX_PATH];
    const char *source_path;
    unsigned char *source = memory;
    unsigned char *output;
    off_t file_size;
    size_t output_capacity;
    int fd;
    int header_length;
    int source_at;
    int output_at;

    rb->snprintf(backup, sizeof(backup), "%s.rbprep-bak", path);
    rb->snprintf(temporary, sizeof(temporary), "%s.rbprep-new", path);
    rb->snprintf(previous, sizeof(previous), "%s.rbprep-prev", path);
    if (!rb->file_exists(path) && rb->file_exists(previous))
        rb->rename(previous, path);
    if (!rb->file_exists(path))
        return true;
    if (!burn_backup_once(path, backup))
        return false;
    source_path = backup;
    fd = rb->open(source_path, O_RDONLY);
    if (fd < 0)
        return false;
    file_size = rb->filesize(fd);
    if (file_size < 28 || (size_t)file_size + 2048 >= memory_size) {
        rb->close(fd);
        return false;
    }
    if (rb->read(fd, source, file_size) != file_size) {
        rb->close(fd);
        return false;
    }
    rb->close(fd);
    output = source + ((file_size + 3) & ~3);
    output_capacity = memory_size - (output - source);
    if (rb->memcmp(source, "PMAI", 4) ||
        burn_be32(source + 8) != (uint32_t)file_size)
        return false;
    header_length = burn_be32(source + 4);
    if (header_length < 28 || header_length > file_size ||
        (size_t)header_length > output_capacity)
        return false;
    rb->memcpy(output, source, header_length);
    source_at = output_at = header_length;
    while (source_at < file_size) {
        int tag_header;
        int tag_length;
        int replacement;
        if (source_at + 12 > file_size)
            return false;
        tag_header = burn_be32(source + source_at + 4);
        tag_length = burn_be32(source + source_at + 8);
        if (tag_header < 12 || tag_length < tag_header ||
            source_at + tag_length > file_size)
            return false;
        replacement = tag_length;
        if (!rb->memcmp(source + source_at, "PQTZ", 4)) {
            uint32_t count;
            int i;
            if (tag_length < 24)
                return false;
            count = burn_be32(source + source_at + 20);
            if (tag_length != 24 + (int)count * 8 ||
                output_at + tag_length > (int)output_capacity)
                return false;
            rb->memcpy(output + output_at, source + source_at, tag_length);
            for (i = 0; i < (int)count; i++) {
                unsigned char *entry = output + output_at + 24 + i * 8;
                int beat = burn_be16(entry);
                int32_t time = burn_be32(entry + 4);
                int32_t phase = read_u32(snapshot + 28);
                int32_t offset = read_u32(snapshot + 32);
                int target_bpm = MAX(1, (int)read_u32(snapshot + 24));
                int64_t delta = (int64_t)time - phase;
                int64_t target = phase + offset +
                                 delta * MAX(1, source_bpm) / target_bpm;
                beat = ((beat - 1 + (int8_t)snapshot[19]) & 3) + 1;
                burn_put_be16(entry, beat);
                burn_put_be16(entry + 2, MIN(65535, target_bpm));
                burn_put_be32(entry + 4, MAX(0, target));
            }
        } else if (!rb->memcmp(source + source_at, "PCOB", 4) &&
                   tag_length >= 24 &&
                   burn_be32(source + source_at + 12) == 1) {
            replacement = burn_build_pcob(output + output_at,
                                           output_capacity - output_at,
                                           snapshot);
            if (replacement < 0)
                return false;
        } else if (!rb->memcmp(source + source_at, "PCO2", 4) &&
                   tag_length >= 20 &&
                   burn_be32(source + source_at + 12) == 1) {
            replacement = burn_build_pco2(output + output_at,
                                           output_capacity - output_at,
                                           snapshot);
            if (replacement < 0)
                return false;
        } else {
            if (output_at + tag_length > (int)output_capacity)
                return false;
            rb->memcpy(output + output_at, source + source_at, tag_length);
        }
        output_at += replacement;
        source_at += tag_length;
    }
    burn_put_be32(output + 8, output_at);
    rb->remove(temporary);
    fd = rb->open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    if (rb->write(fd, output, output_at) != output_at) {
        rb->close(fd);
        rb->remove(temporary);
        return false;
    }
    rb->close(fd);
    return burn_swap_keep_previous(path, temporary, previous);
}

static void burn_analysis_variant(char *path, const char *extension)
{
    char *dot = rb->strrchr(path, '.');
    if (dot)
        rb->strlcpy(dot, extension, MAX_PATH - (dot - path));
}

static bool burn_analysis_for_track(const struct rbprep_pdb_track *track,
                                    uint32_t track_id,
                                    const unsigned char *snapshot,
                                    unsigned char *memory,
                                    size_t memory_size)
{
    char path[MAX_PATH];
    int source_bpm;

    if (!track->analysis_path[0])
        return true;
    rb->strlcpy(path, track->analysis_path, sizeof(path));
    source_bpm = burn_source_bpm(track_id, track->source_bpm_x100);
    if (!burn_rewrite_analysis(path, snapshot, source_bpm,
                               memory, memory_size))
        return false;
    burn_analysis_variant(path, ".EXT");
    return burn_rewrite_analysis(path, snapshot, source_bpm,
                                 memory, memory_size);
}

static void burn_finish_analysis_files(struct rbprep_pdb *pdb,
                                       bool restore)
{
    unsigned char record[RBPREP_EDIT_RECORD_SIZE];
    int fd = rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);

    if (fd < 0)
        return;
    while (rb->read(fd, record, sizeof(record)) == sizeof(record)) {
        off_t after = rb->lseek(fd, 0, SEEK_CUR);
        struct rbprep_pdb_track track;
        char path[MAX_PATH];
        char previous[MAX_PATH];
        if (!valid_edit_record(record) ||
            !burn_record_is_latest(fd, read_u32(record + 8), after) ||
            !pdb_find_track(pdb, read_u32(record + 8), &track) ||
            !track.analysis_path[0])
            continue;
        rb->strlcpy(path, track.analysis_path, sizeof(path));
        rb->snprintf(previous, sizeof(previous), "%s.rbprep-prev", path);
        if (restore)
            burn_restore_previous(path, previous);
        else
            rb->remove(previous);
        burn_analysis_variant(path, ".EXT");
        rb->snprintf(previous, sizeof(previous), "%s.rbprep-prev", path);
        if (restore)
            burn_restore_previous(path, previous);
        else
            rb->remove(previous);
    }
    rb->close(fd);
}

static bool burn_playlist_journal(struct rbprep_pdb *pdb)
{
    char line[160];
    int length = 0;
    unsigned char value;
    int fd = rb->open(RBPREP_PLAYLIST_JOURNAL, O_RDONLY);

    if (fd < 0)
        return true;
    while (rb->read(fd, &value, 1) == 1) {
        if (value == '\n') {
            char *first;
            char *second;
            uint32_t track;
            uint32_t playlist;
            line[length] = '\0';
            first = rb->strchr(line, '\t');
            second = first ? rb->strchr(first + 1, '\t') : NULL;
            if (first && second) {
                *first = *second = '\0';
                track = rb->strtoul(line, NULL, 10);
                playlist = rb->strtoul(first + 1, NULL, 10);
                if (!pdb_resolve_playlist(pdb, playlist, second + 1,
                                          &playlist) ||
                    !pdb_add_playlist_entry(pdb, track, playlist)) {
                    rb->close(fd);
                    return false;
                }
            }
            length = 0;
        } else if (value != '\r' && length + 1 < (int)sizeof(line)) {
            line[length++] = value;
        }
    }
    rb->close(fd);
    return true;
}

static bool rbprep_burn_all(void)
{
    struct rbprep_pdb pdb;
    unsigned char record[RBPREP_EDIT_RECORD_SIZE];
    unsigned char *memory;
    size_t memory_size;
    int fd;
    int completed = 0;
    int total = MAX(1, pending_snapshot_count);
    bool success = false;
    const char *failure = "initialization";

    stop_editor_audio();
    /* The analysis transaction needs two simultaneous copies of the largest
       ANLZ file. Explicitly release playback's codec/buffering allocation
       before asking buflib for its maximum block. */
    rb->audio_stop();
    rb->yield();
    audio_was_running = false;
    playlist_playback = false;
    memory = rb->plugin_get_audio_buffer(&memory_size);
    if (!memory || memory_size < 65536) {
        failure = "audio workspace";
        goto done;
    }
    if (!rb->file_exists(RBPREP_PDB) && rb->file_exists(RBPREP_PDB_PREV))
        rb->rename(RBPREP_PDB_PREV, RBPREP_PDB);
    if (!rb->file_exists(RBPREP_PDB)) {
        failure = "export.pdb missing";
        goto done;
    }
    if (!burn_backup_once(RBPREP_PDB, RBPREP_PDB_BAK)) {
        failure = "PDB backup";
        goto done;
    }
    rb->remove(RBPREP_PDB_NEW);
    if (!burn_copy_file(RBPREP_PDB, RBPREP_PDB_NEW)) {
        failure = "PDB working copy";
        goto done;
    }
    if (!pdb_open(&pdb, RBPREP_PDB_NEW)) {
        failure = "PDB open";
        goto done;
    }

    fd = rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);
    if (fd >= 0) {
        while (rb->read(fd, record, sizeof(record)) == sizeof(record)) {
            off_t after = rb->lseek(fd, 0, SEEK_CUR);
            struct rbprep_pdb_track track;
            uint32_t track_id;
            if (!valid_edit_record(record))
                continue;
            track_id = read_u32(record + 8);
            if (!burn_record_is_latest(fd, track_id, after))
                continue;
            rb->splash_progress(MIN(completed, total), total,
                                "Burning track %lu",
                                (unsigned long)track_id);
            if (!pdb_patch_snapshot(&pdb, record, &track)) {
                failure = "track lookup/PDB";
                rb->close(fd);
                burn_finish_analysis_files(&pdb, true);
                rb->close(pdb.fd);
                goto done;
            }
            if (!burn_analysis_for_track(&track, track_id, record,
                                         memory, memory_size)) {
                failure = "ANLZ rewrite";
                rb->close(fd);
                burn_finish_analysis_files(&pdb, true);
                rb->close(pdb.fd);
                goto done;
            }
            completed++;
        }
        rb->close(fd);
    }
    if (!burn_playlist_journal(&pdb)) {
        failure = "playlist table";
        burn_finish_analysis_files(&pdb, true);
        rb->close(pdb.fd);
        goto done;
    }
    if (pdb.structural) {
        unsigned char raw[4];
        if (!burn_read_at(pdb.fd, 0x14, raw, 4)) {
            failure = "PDB sequence read";
            burn_finish_analysis_files(&pdb, true);
            rb->close(pdb.fd);
            goto done;
        }
        write_u32(raw, read_u32(raw) + 1);
        if (!burn_write_at(pdb.fd, 0x14, raw, 4)) {
            failure = "PDB sequence write";
            burn_finish_analysis_files(&pdb, true);
            rb->close(pdb.fd);
            goto done;
        }
    }
    rb->close(pdb.fd);
    if (!pdb_open(&pdb, RBPREP_PDB_NEW)) {
        failure = "PDB validation";
        struct rbprep_pdb original;
        if (pdb_open(&original, RBPREP_PDB)) {
            burn_finish_analysis_files(&original, true);
            rb->close(original.fd);
        }
        goto done;
    }
    rb->close(pdb.fd);
    if (!burn_swap_keep_previous(RBPREP_PDB, RBPREP_PDB_NEW,
                                 RBPREP_PDB_PREV)) {
        failure = "PDB commit";
        struct rbprep_pdb original;
        if (pdb_open(&original, RBPREP_PDB)) {
            burn_finish_analysis_files(&original, true);
            rb->close(original.fd);
        }
        goto done;
    }
    if (!pdb_open(&pdb, RBPREP_PDB)) {
        failure = "PDB reopen";
        struct rbprep_pdb original;
        if (pdb_open(&original, RBPREP_PDB_PREV)) {
            burn_finish_analysis_files(&original, true);
            rb->close(original.fd);
        }
        burn_restore_previous(RBPREP_PDB, RBPREP_PDB_PREV);
        goto done;
    }
    burn_finish_analysis_files(&pdb, false);
    rb->close(pdb.fd);
    rb->remove(RBPREP_PDB_PREV);
    {
        uint32_t edit_size = 0;
        uint32_t playlist_size = 0;
        fd = rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);
        if (fd >= 0) {
            edit_size = rb->filesize(fd);
            rb->close(fd);
        }
        fd = rb->open(RBPREP_PLAYLIST_JOURNAL, O_RDONLY);
        if (fd >= 0) {
            playlist_size = rb->filesize(fd);
            rb->close(fd);
        }
        if (!write_burn_offsets(edit_size, playlist_size))
            rb->splash(HZ, "Burned; pending marker failed");
    }
    rb->splash_progress(total, total, "Rekordbox burn complete");
    success = true;
done:
    rb->remove(RBPREP_PDB_NEW);
    rb->plugin_release_audio_buffer();
    if (!success)
        rb->splashf(HZ * 3, "BURN FAILED: %s", failure);
    restore_black_canvas();
    return success;
}
