/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Rekordpod's on-device rekordbox transaction writer.
 *
 * This is included by rbprep.c after the edit-journal helpers so it can share
 * the plugin's compact RBE1 format without exporting a second plugin ABI.
 * General mutations use a persistent baseline backup and same-directory
 * replacement. A single add to an existing playlist uses a verified 4 KB
 * page journal plus an index replacement, avoiding a full export.pdb copy.
 */

#define RBPREP_PDB_NEW  RBPREP_PDB ".rekordpod-new"
#define RBPREP_PDB_PREV RBPREP_PDB ".rekordpod-prev"
#define RBPREP_PDB_BAK  RBPREP_PDB ".rekordpod-bak"
#define RBPREP_PDB_FAILED "/.rockbox/rekordpod/export-failed.pdb"
#define RBPREP_INDEX_NEW  RBPREP_INDEX ".rekordpod-new"
#define RBPREP_INDEX_PREV RBPREP_INDEX ".rekordpod-prev"
#define RBPREP_INDEX_BAK  RBPREP_INDEX ".rekordpod-bak"
#define RBPREP_PDB_PAGE_JOURNAL \
    "/.rockbox/rekordpod/pdb-page-transaction.rpf"
#define RBPREP_PDB_PAGE_JOURNAL_TMP \
    "/.rockbox/rekordpod/pdb-page-transaction.tmp"
#define RBPREP_PDB_PAGE_MAX 4096
#define RBPREP_PDB_TOUCH_MAX 32
#define RBPREP_PDB_PAGE_JOURNAL_HEADER 20
#define RBPREP_PDB_PAGE_JOURNAL_RECORD 12
#define RBPREP_PDB_PAGE_JOURNAL_MAX 3

struct rbprep_pdb_touch {
    uint32_t table;
    uint32_t page;
    int original_slots;
    int appends;
    uint32_t generation;
    bool initialized;
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

struct rbprep_pdb_playlist_node {
    uint32_t page;
    uint32_t row;
    uint32_t parent;
    uint32_t sort_order;
    uint32_t id;
    int slot;
    bool is_folder;
    char name[64];
};

static unsigned char rbprep_burn_page[RBPREP_PDB_PAGE_MAX];
static char rbprep_burn_failure_detail[80];

static bool pdb_structure_error(const char *reason, uint32_t table,
                                uint32_t page, uint32_t value)
{
    if (!rbprep_burn_failure_detail[0])
        rb->snprintf(rbprep_burn_failure_detail,
                     sizeof(rbprep_burn_failure_detail),
                     "PDB T%lu P%lu %s %lu",
                     (unsigned long)table, (unsigned long)page, reason,
                     (unsigned long)value);
    return false;
}

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

static bool burn_copy_file_once(const char *source, const char *destination)
{
    int input;
    int output;
    int count;
    int close_result;
    off_t expected;
    off_t copied = 0;
    off_t actual = -1;

    input = rb->open(source, O_RDONLY);
    if (input < 0)
        return false;
    expected = rb->filesize(input);
    if (expected < 0) {
        rb->close(input);
        return false;
    }
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
        copied += count;
    }
    rb->close(input);
    close_result = rb->close(output);
    if (count < 0 || close_result < 0 || copied != expected) {
        rb->remove(destination);
        return false;
    }

    /* close() performs Rockbox's FAT cache flush and final cluster-chain
       update. Verify that result through a fresh descriptor: an iFlash flush
       failure used to be ignored here, leaving the final 4096-byte PDB page
       absent even though every preceding write had reported success. */
    output = rb->open(destination, O_RDONLY);
    if (output >= 0) {
        actual = rb->filesize(output);
        rb->close(output);
    }
    if (actual != expected) {
        rb->remove(destination);
        return false;
    }
    return true;
}

static bool burn_copy_file(const char *source, const char *destination)
{
    int attempt;

    for (attempt = 0; attempt < 2; attempt++) {
        rb->remove(destination);
        if (burn_copy_file_once(source, destination))
            return true;
        /* Give the storage thread a chance to finish an allocation/cache
           cycle before retrying the complete, still-uncommitted copy. */
        rb->yield();
    }
    return false;
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

static bool pdb_open_retry(struct rbprep_pdb *pdb, const char *path)
{
    int attempt;

    /* A just-closed multi-megabyte FAT file can briefly have its directory
       entry visible before all cached first-cluster data is readable through
       a new descriptor on slow iFlash media. Retry only the non-destructive
       open/read/validate step; the working copy is never edited meanwhile. */
    for (attempt = 0; attempt < 3; attempt++) {
        if (pdb_open(pdb, path))
            return true;
        rb->sleep(1);
    }
    return false;
}

static bool pdb_create_working_copy_once(struct rbprep_pdb *pdb,
                                         const char *source,
                                         const char *destination)
{
    unsigned char header[28];
    off_t expected;
    off_t copied = 0;
    int input;
    int output;
    int count;

    input = rb->open(source, O_RDONLY);
    if (input < 0) {
        rb->strlcpy(rbprep_burn_failure_detail,
                    "PDB source open",
                    sizeof(rbprep_burn_failure_detail));
        return false;
    }
    expected = rb->filesize(input);
    if (expected < 0) {
        rb->close(input);
        rb->strlcpy(rbprep_burn_failure_detail,
                    "PDB source size",
                    sizeof(rbprep_burn_failure_detail));
        return false;
    }
    output = rb->open(destination, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (output < 0) {
        rb->close(input);
        rb->strlcpy(rbprep_burn_failure_detail,
                    "PDB work create",
                    sizeof(rbprep_burn_failure_detail));
        return false;
    }
    while ((count = rb->read(input, rbprep_burn_page,
                             sizeof(rbprep_burn_page))) > 0) {
        if (rb->write(output, rbprep_burn_page, count) != count) {
            rb->strlcpy(rbprep_burn_failure_detail,
                        "PDB work write",
                        sizeof(rbprep_burn_failure_detail));
            goto fail;
        }
        copied += count;
    }
    rb->close(input);
    input = -1;
    if (count < 0 || copied != expected ||
        rb->filesize(output) != expected ||
        !burn_read_at(output, 0, header, sizeof(header))) {
        rb->strlcpy(rbprep_burn_failure_detail,
                    "PDB work verify",
                    sizeof(rbprep_burn_failure_detail));
        goto fail;
    }

    rb->memset(pdb, 0, sizeof(*pdb));
    pdb->fd = output;
    pdb->page_size = read_u32(header + 4);
    pdb->table_count = read_u32(header + 8);
    if (read_u32(header) != 0 || pdb->page_size != 4096 ||
        pdb->page_size > RBPREP_PDB_PAGE_MAX ||
        pdb->table_count == 0 || pdb->table_count > 64) {
        rb->strlcpy(rbprep_burn_failure_detail,
                    "PDB work header",
                    sizeof(rbprep_burn_failure_detail));
        goto fail;
    }
    return true;

fail:
    if (input >= 0)
        rb->close(input);
    rb->close(output);
    rb->remove(destination);
    pdb->fd = -1;
    return false;
}

static bool pdb_create_working_copy(struct rbprep_pdb *pdb,
                                    const char *source,
                                    const char *destination)
{
    int attempt;

    /* Keep the newly-created file descriptor for the whole transaction.
       Closing a large FAT file and immediately reopening it O_RDWR was the
       regression: slow iFlash media can expose the directory entry before a
       fresh descriptor can read its first cluster. Final close/reopen and
       full structural validation still happen before the atomic rename. */
    for (attempt = 0; attempt < 2; attempt++) {
        rb->remove(destination);
        rbprep_burn_failure_detail[0] = '\0';
        if (pdb_create_working_copy_once(pdb, source, destination))
            return true;
        rb->sleep(1);
    }
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

/* A single add to an existing playlist can modify only the PDB header page,
   table 8's last page, and (when that page is full) its reserved successor.
   Snapshot those pages before the in-place write. This retains crash recovery
   without copying a multi-megabyte export.pdb for every favorite click. */
static bool pdb_page_journal_header(int fd, uint32_t *original_size,
                                    int *record_count)
{
    unsigned char header[RBPREP_PDB_PAGE_JOURNAL_HEADER];
    uint64_t expected;
    off_t actual;

    if (!burn_read_at(fd, 0, header, sizeof(header)) ||
        rb->memcmp(header, "RPF1", 4) ||
        read_u32(header + 4) != RBPREP_PDB_PAGE_MAX ||
        read_u32(header + 16) != macro_checksum(header, 16))
        return false;
    *original_size = read_u32(header + 8);
    *record_count = read_u32(header + 12);
    if (*record_count < 1 ||
        *record_count > RBPREP_PDB_PAGE_JOURNAL_MAX)
        return false;
    expected = RBPREP_PDB_PAGE_JOURNAL_HEADER +
        (uint64_t)*record_count *
        (RBPREP_PDB_PAGE_JOURNAL_RECORD + RBPREP_PDB_PAGE_MAX);
    actual = rb->filesize(fd);
    return expected <= 0xffffffffu && actual == (off_t)expected;
}

static bool pdb_page_journal_validate(int fd, uint32_t *original_size,
                                      int *record_count)
{
    unsigned char record[RBPREP_PDB_PAGE_JOURNAL_RECORD];
    uint32_t pages[RBPREP_PDB_PAGE_JOURNAL_MAX];
    int i;

    if (!pdb_page_journal_header(fd, original_size, record_count) ||
        rb->lseek(fd, RBPREP_PDB_PAGE_JOURNAL_HEADER, SEEK_SET) < 0)
        return false;
    for (i = 0; i < *record_count; i++) {
        uint32_t page;
        uint32_t saved;
        uint32_t expected_saved;
        uint64_t offset;
        int prior;

        if (!read_exact(fd, record, sizeof(record)) ||
            !read_exact(fd, rbprep_burn_page, RBPREP_PDB_PAGE_MAX))
            return false;
        page = read_u32(record);
        saved = read_u32(record + 4);
        offset = (uint64_t)page * RBPREP_PDB_PAGE_MAX;
        if (offset > 0xffffffffu)
            return false;
        expected_saved = offset < *original_size
            ? MIN((uint32_t)RBPREP_PDB_PAGE_MAX,
                  *original_size - (uint32_t)offset) : 0;
        if (saved != expected_saved ||
            read_u32(record + 8) !=
                macro_checksum(rbprep_burn_page, RBPREP_PDB_PAGE_MAX))
            return false;
        for (prior = 0; prior < i; prior++)
            if (pages[prior] == page)
                return false;
        pages[i] = page;
    }
    return true;
}

static bool pdb_restore_page_journal(void)
{
    unsigned char record[RBPREP_PDB_PAGE_JOURNAL_RECORD];
    uint32_t original_size;
    int record_count;
    int journal = -1;
    int pdb_fd = -1;
    int i;
    bool index_was_open = library_fd >= 0;
    bool ok = false;

    rb->remove(RBPREP_PDB_PAGE_JOURNAL_TMP);
    if (!rb->file_exists(RBPREP_PDB_PAGE_JOURNAL))
        return true;
    journal = rb->open(RBPREP_PDB_PAGE_JOURNAL, O_RDONLY);
    if (journal < 0 ||
        !pdb_page_journal_validate(journal, &original_size, &record_count) ||
        rb->lseek(journal, RBPREP_PDB_PAGE_JOURNAL_HEADER, SEEK_SET) < 0)
        goto done;
    pdb_fd = rb->open(RBPREP_PDB, O_RDWR);
    if (pdb_fd < 0)
        goto done;
    for (i = 0; i < record_count; i++) {
        uint32_t page;
        uint32_t saved;

        if (!read_exact(journal, record, sizeof(record)) ||
            !read_exact(journal, rbprep_burn_page, RBPREP_PDB_PAGE_MAX))
            goto done;
        page = read_u32(record);
        saved = read_u32(record + 4);
        if (saved && !burn_write_at(pdb_fd,
                                    page * RBPREP_PDB_PAGE_MAX,
                                    rbprep_burn_page, saved))
            goto done;
    }
    if (rb->close(journal) < 0) {
        journal = -1;
        goto done;
    }
    journal = -1;
    if (rb->ftruncate(pdb_fd, original_size) < 0 || rb->close(pdb_fd) < 0) {
        pdb_fd = -1;
        goto done;
    }
    pdb_fd = -1;
    if (library_fd >= 0) {
        rb->close(library_fd);
        library_fd = -1;
    }
    if (rb->file_exists(RBPREP_INDEX_PREV))
        burn_restore_previous(RBPREP_INDEX, RBPREP_INDEX_PREV);
    rb->remove(RBPREP_INDEX_NEW);
    if (index_was_open && !open_library_index())
        goto done;
    if (rb->remove(RBPREP_PDB_PAGE_JOURNAL) < 0 &&
        rb->file_exists(RBPREP_PDB_PAGE_JOURNAL))
        goto done;
    ok = true;

done:
    if (pdb_fd >= 0)
        rb->close(pdb_fd);
    if (journal >= 0)
        rb->close(journal);
    if (!ok && !rbprep_burn_failure_detail[0])
        rb->strlcpy(rbprep_burn_failure_detail,
                    "PDB page journal recovery",
                    sizeof(rbprep_burn_failure_detail));
    return ok;
}

static bool pdb_capture_page_journal(struct rbprep_pdb *pdb)
{
    unsigned char header[RBPREP_PDB_PAGE_JOURNAL_HEADER];
    unsigned char record[RBPREP_PDB_PAGE_JOURNAL_RECORD];
    unsigned char table[16];
    unsigned char raw[4];
    uint32_t pages[RBPREP_PDB_PAGE_JOURNAL_MAX];
    uint32_t entry;
    uint32_t original_size;
    uint32_t next_unused;
    uint32_t candidate;
    uint32_t last;
    int record_count = 0;
    int journal = -1;
    int verify = -1;
    int i;
    bool ok = false;
    off_t file_size = rb->filesize(pdb->fd);

    if (file_size <= 0 || (uint64_t)file_size > 0xffffffffu ||
        !pdb_table_entry(pdb, 8, &entry) ||
        !burn_read_at(pdb->fd, entry, table, sizeof(table)) ||
        !burn_read_at(pdb->fd, 12, raw, sizeof(raw)))
        goto done;
    original_size = file_size;
    next_unused = read_u32(raw);
    candidate = read_u32(table + 4);
    last = read_u32(table + 12);
    if (!last || (uint64_t)last * RBPREP_PDB_PAGE_MAX >= original_size)
        goto done;
    pages[record_count++] = 0;
    if (last != 0)
        pages[record_count++] = last;
    if (candidate && candidate != last && candidate < next_unused)
        pages[record_count++] = candidate;
    if (record_count > RBPREP_PDB_PAGE_JOURNAL_MAX)
        goto done;

    rb->remove(RBPREP_PDB_PAGE_JOURNAL_TMP);
    journal = rb->open(RBPREP_PDB_PAGE_JOURNAL_TMP,
                       O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (journal < 0)
        goto done;
    rb->memset(header, 0, sizeof(header));
    rb->memcpy(header, "RPF1", 4);
    write_u32(header + 4, RBPREP_PDB_PAGE_MAX);
    write_u32(header + 8, original_size);
    write_u32(header + 12, record_count);
    write_u32(header + 16, macro_checksum(header, 16));
    if (!write_exact(journal, header, sizeof(header)))
        goto done;
    for (i = 0; i < record_count; i++) {
        uint64_t offset = (uint64_t)pages[i] * RBPREP_PDB_PAGE_MAX;
        uint32_t saved = offset < original_size
            ? MIN((uint32_t)RBPREP_PDB_PAGE_MAX,
                  original_size - (uint32_t)offset) : 0;

        rb->memset(rbprep_burn_page, 0, RBPREP_PDB_PAGE_MAX);
        if (saved && !burn_read_at(pdb->fd, (uint32_t)offset,
                                   rbprep_burn_page, saved))
            goto done;
        write_u32(record, pages[i]);
        write_u32(record + 4, saved);
        write_u32(record + 8,
                  macro_checksum(rbprep_burn_page, RBPREP_PDB_PAGE_MAX));
        if (!write_exact(journal, record, sizeof(record)) ||
            !write_exact(journal, rbprep_burn_page, RBPREP_PDB_PAGE_MAX))
            goto done;
    }
    if (rb->close(journal) < 0) {
        journal = -1;
        goto done;
    }
    journal = -1;
    verify = rb->open(RBPREP_PDB_PAGE_JOURNAL_TMP, O_RDONLY);
    if (verify < 0 ||
        !pdb_page_journal_validate(verify, &original_size, &record_count))
        goto done;
    rb->close(verify);
    verify = -1;
    rb->remove(RBPREP_PDB_PAGE_JOURNAL);
    if (rb->rename(RBPREP_PDB_PAGE_JOURNAL_TMP,
                   RBPREP_PDB_PAGE_JOURNAL) < 0)
        goto done;
    ok = true;

done:
    if (verify >= 0)
        rb->close(verify);
    if (journal >= 0)
        rb->close(journal);
    if (!ok) {
        rb->remove(RBPREP_PDB_PAGE_JOURNAL_TMP);
        if (!rbprep_burn_failure_detail[0])
            rb->strlcpy(rbprep_burn_failure_detail,
                        "PDB page journal create",
                        sizeof(rbprep_burn_failure_detail));
    }
    return ok;
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

static bool pdb_required_page_count(struct rbprep_pdb *pdb,
                                    uint32_t *required_pages)
{
    uint32_t table_index;
    uint32_t highest = 0;

    for (table_index = 0; table_index < pdb->table_count; table_index++) {
        unsigned char table[16];
        uint32_t first;
        uint32_t last;

        if (!burn_read_at(pdb->fd, 0x1c + table_index * 16,
                          table, sizeof(table)))
            return false;
        first = read_u32(table + 8);
        last = read_u32(table + 12);
        if (first > last)
            return false;
        highest = MAX(highest, MAX(first, last));
    }
    *required_pages = highest + 1;
    return true;
}

/* Return the number of restored pages, zero for an already complete file,
   or -1 when a compatible tail cannot be recovered. The live prefix is kept
   because it contains the most recent successful metadata writes. Missing
   pages come from the newest compatible transaction, falling back to the
   immutable baseline backup. The caller replays the complete journals so
   changes that once lived in a missing page are reconstructed as well. */
static int pdb_restore_missing_tail(struct rbprep_pdb *pdb)
{
    static const char * const candidates[] = {
        RBPREP_PDB_PREV, RBPREP_PDB_BAK
    };
    unsigned char header[28];
    unsigned char live_table[16];
    unsigned char candidate_table[16];
    off_t file_size = rb->filesize(pdb->fd);
    off_t candidate_size;
    uint32_t file_pages;
    uint32_t required_pages;
    uint32_t page;
    int candidate_index;

    if (file_size < (off_t)pdb->page_size ||
        file_size % pdb->page_size != 0 ||
        !pdb_required_page_count(pdb, &required_pages))
        return -1;
    file_pages = file_size / pdb->page_size;
    if (file_pages >= required_pages)
        return 0;

    for (candidate_index = 0;
         candidate_index < (int)ARRAYLEN(candidates); candidate_index++) {
        const char *candidate = candidates[candidate_index];
        int fd = rb->open(candidate, O_RDONLY);
        uint32_t table_index;
        bool compatible = fd >= 0;

        if (!compatible)
            continue;
        candidate_size = rb->filesize(fd);
        compatible = candidate_size >=
                     (off_t)required_pages * (off_t)pdb->page_size &&
                     burn_read_at(fd, 0, header, sizeof(header)) &&
                     read_u32(header) == 0 &&
                     read_u32(header + 4) == pdb->page_size &&
                     read_u32(header + 8) == pdb->table_count;
        for (table_index = 0;
             compatible && table_index < pdb->table_count; table_index++) {
            uint32_t offset = 0x1c + table_index * 16;
            compatible = burn_read_at(pdb->fd, offset, live_table,
                                      sizeof(live_table)) &&
                         burn_read_at(fd, offset, candidate_table,
                                      sizeof(candidate_table)) &&
                         read_u32(live_table) == read_u32(candidate_table) &&
                         read_u32(live_table + 8) ==
                             read_u32(candidate_table + 8) &&
                         read_u32(live_table + 12) ==
                             read_u32(candidate_table + 12);
        }
        /* Preflight readability before extending the working file. Unlinked
           allocation pages are legitimately all-zero in DeviceSQL, so only
           the full chain validator below may require page identity/type. */
        for (page = file_pages;
             compatible && page < required_pages; page++) {
            compatible = burn_read_at(fd, page * pdb->page_size,
                                      rbprep_burn_page, pdb->page_size);
        }
        if (!compatible) {
            rb->close(fd);
            continue;
        }
        for (page = file_pages; page < required_pages; page++) {
            if (!burn_read_at(fd, page * pdb->page_size,
                              rbprep_burn_page, pdb->page_size) ||
                !burn_write_at(pdb->fd, page * pdb->page_size,
                               rbprep_burn_page, pdb->page_size)) {
                rb->close(fd);
                return -1;
            }
        }
        rb->close(fd);
        return required_pages - file_pages;
    }
    return -1;
}

static bool pdb_validate_structure(struct rbprep_pdb *pdb)
{
    off_t file_size = rb->filesize(pdb->fd);
    uint32_t file_pages;
    uint32_t table_index;

    if (file_size < (off_t)pdb->page_size ||
        file_size % pdb->page_size != 0)
        return pdb_structure_error("size", 0, 0,
                                   file_size < 0 ? 0 : file_size);
    file_pages = file_size / pdb->page_size;
    for (table_index = 0; table_index < pdb->table_count; table_index++) {
        unsigned char table[16];
        uint32_t type;
        uint32_t page;
        uint32_t last;
        uint32_t guard = 0;

        if (!burn_read_at(pdb->fd, 0x1c + table_index * 16,
                          table, sizeof(table)))
            return pdb_structure_error("directory", table_index, 0, 0);
        type = read_u32(table);
        page = read_u32(table + 8);
        last = read_u32(table + 12);
        if (page >= file_pages || last >= file_pages)
            return pdb_structure_error("bounds", type, page, last);
        while (guard++ <= file_pages) {
            int slots;
            int used;
            int slot;
            uint32_t next;

            if (page >= file_pages)
                return pdb_structure_error("page bounds", type, page,
                                           file_pages);
            if (!burn_read_at(pdb->fd, page * pdb->page_size,
                              rbprep_burn_page, pdb->page_size))
                return pdb_structure_error("read", type, page, 0);
            if (read_u32(rbprep_burn_page + 4) != page)
                return pdb_structure_error("self", type, page,
                                           read_u32(rbprep_burn_page + 4));
            if (read_u32(rbprep_burn_page + 8) != type)
                return pdb_structure_error("type", type, page,
                                           read_u32(rbprep_burn_page + 8));
            next = read_u32(rbprep_burn_page + 0x0c);
            if (!(rbprep_burn_page[0x1b] & 0x40)) {
                int free_space;
                int present;
                uint16_t previous_offset = 0;
                slots = pdb_slot_count(rbprep_burn_page);
                used = read_u16(rbprep_burn_page + 0x1e);
                free_space = read_u16(rbprep_burn_page + 0x1c);
                present = read_u16(rbprep_burn_page + 0x19) >> 5;
                if (slots < 0 || slots > 511 || used < 0 ||
                    0x28 + used + pdb_directory_bytes(slots) >
                    (int)pdb->page_size)
                    return pdb_structure_error("space", type, page,
                                               ((uint32_t)slots << 16) |
                                               (uint16_t)used);
                if (free_space != (int)pdb->page_size - 0x28 - used -
                                  pdb_directory_bytes(slots))
                    return pdb_structure_error("free", type, page,
                                               free_space);
                if (present != pdb_present_count(rbprep_burn_page,
                                                 pdb->page_size, slots))
                    return pdb_structure_error("present", type, page,
                                               present);
                for (slot = 0; slot < slots; slot++) {
                    uint16_t offset = pdb_row_heap_offset(
                        rbprep_burn_page, pdb->page_size, slot);
                    if (offset >= used ||
                        (slot > 0 && offset <= previous_offset))
                        return pdb_structure_error("heap", type, page,
                                                   slot);
                    previous_offset = offset;
                }
            }
            if (page == last)
                break;
            if (next >= file_pages || next == page)
                return pdb_structure_error("next", type, page, next);
            page = next;
        }
        if (page != last || guard > file_pages + 1)
            return pdb_structure_error("chain", type, page, last);
    }
    return true;
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
    touch->initialized = false;
    pdb->structural = true;
    return touch;
}

static void pdb_prepare_touch(struct rbprep_pdb *pdb,
                              struct rbprep_pdb_touch *touch,
                              unsigned char *page)
{
    int i;

    if (touch->initialized)
        return;
    for (i = 0; i < (touch->original_slots + 15) / 16; i++) {
        int group_base = pdb->page_size - i * 0x24;
        write_u16(page + group_base - 2, 0);
    }
    touch->initialized = true;
}

static bool pdb_allocate_page(struct rbprep_pdb *pdb, uint32_t table,
                              uint32_t entry, uint32_t old_last,
                              uint32_t *new_page)
{
    unsigned char raw[4];
    off_t file_size;
    uint32_t candidate;
    uint32_t next_unused;
    uint32_t old_flags;

    if (!burn_read_at(pdb->fd, entry + 4, raw, 4))
        return false;
    candidate = read_u32(raw);
    if (!burn_read_at(pdb->fd, 12, raw, 4))
        return false;
    next_unused = read_u32(raw);
    file_size = rb->filesize(pdb->fd);
    if (candidate == 0 || candidate == old_last ||
        candidate >= next_unused || file_size < 0 ||
        !burn_read_at(pdb->fd, old_last * pdb->page_size + 0x0c,
                      raw, sizeof(raw)) || read_u32(raw) != candidate)
        return false;
    if ((off_t)candidate * (off_t)pdb->page_size < file_size) {
        int i;
        if (!burn_read_at(pdb->fd, candidate * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return false;
        for (i = 0; i < (int)pdb->page_size; i++)
            if (rbprep_burn_page[i] != 0)
                return false;
    }
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
    pdb_prepare_touch(pdb, touch, rbprep_burn_page);
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

static int pdb_find_key(struct rbprep_pdb *pdb, const char *name,
                        uint32_t *max_id)
{
    uint32_t entry;
    unsigned char table[16];
    uint32_t page;
    uint32_t last;
    int guard = 0;

    *max_id = 0;
    if (!pdb_table_entry(pdb, 5, &entry) ||
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
                unsigned char raw[8];
                char existing[24];
                uint32_t id;

                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                row = page * pdb->page_size + 0x28 +
                      pdb_row_heap_offset(rbprep_burn_page,
                                          pdb->page_size, slot);
                if (!burn_read_at(pdb->fd, row, raw, sizeof(raw)))
                    return -1;
                id = read_u32(raw);
                *max_id = MAX(*max_id, id);
                if (read_u32(raw + 4) == id &&
                    pdb_decode_string(pdb, row + 8, existing,
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

static int pdb_get_or_create_key(struct rbprep_pdb *pdb, const char *name)
{
    unsigned char row[48];
    uint32_t max_id;
    int id = pdb_find_key(pdb, name, &max_id);
    int string_size;
    int row_size;
    int alloc;

    if (id != 0)
        return id;
    write_u32(row, max_id + 1);
    write_u32(row + 4, max_id + 1);
    string_size = pdb_encode_name(name, row + 8, sizeof(row) - 8);
    if (string_size < 0)
        return -1;
    row_size = 8 + string_size;
    alloc = (row_size + 3) & ~3;
    if (!pdb_append_row(pdb, 5, row, row_size, alloc))
        return -1;
    return max_id + 1;
}

static bool pdb_patch_snapshot(struct rbprep_pdb *pdb,
                               const unsigned char *snapshot,
                               struct rbprep_pdb_track *track)
{
    uint32_t track_id = read_u32(snapshot + 8);
    uint32_t genre_id;
    uint32_t key_id = 0;
    unsigned char raw[4];
    unsigned char current_color;
    unsigned char desired_color;
    char genre[32];
    char key[24];
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
    if (read_u16(snapshot + 6) >= 2) {
        rb->memcpy(key, snapshot + 192, sizeof(key));
        key[sizeof(key) - 1] = '\0';
        key_id = 0;
        if (key[0]) {
            found = pdb_get_or_create_key(pdb, key);
            if (found < 0)
                return false;
            key_id = found;
        }
        write_u32(raw, key_id);
        if (!burn_write_at(pdb->fd, track->row + 0x20, raw, 4))
            return false;
    }
    write_u32(raw, read_u32(snapshot + 24));
    if (!burn_write_at(pdb->fd, track->row + 0x38, raw, 4))
        return false;
    write_u32(raw, genre_id);
    if (!burn_write_at(pdb->fd, track->row + 0x3c, raw, 4))
        return false;
    write_u16(raw, read_u16(snapshot + 20));
    if (!burn_write_at(pdb->fd, track->row + 0x50, raw, 2))
        return false;
    if (!burn_read_at(pdb->fd, track->row + 0x58, raw, 2))
        return false;
    current_color = raw[0];
    desired_color = snapshot[17] < 8 ? snapshot[17] + 1 : 0;
    if (current_color != desired_color)
        raw[0] = desired_color;
    raw[1] = MIN(5, snapshot[16]);
    if (!burn_write_at(pdb->fd, track->row + 0x58, raw, 2))
        return false;

    /* Never report a successful metadata burn merely because every write()
       returned its byte count. Re-read all edited fields from the working
       PDB before it can enter the commit transaction. */
    if (!burn_read_at(pdb->fd, track->row + 0x38, raw, 4) ||
        read_u32(raw) != read_u32(snapshot + 24) ||
        !burn_read_at(pdb->fd, track->row + 0x3c, raw, 4) ||
        read_u32(raw) != genre_id ||
        !burn_read_at(pdb->fd, track->row + 0x50, raw, 2) ||
        read_u16(raw) != read_u16(snapshot + 20) ||
        !burn_read_at(pdb->fd, track->row + 0x58, raw, 2) ||
        raw[0] != desired_color || raw[1] != MIN(5, snapshot[16]))
        return false;
    if (read_u16(snapshot + 6) >= 2 &&
        (!burn_read_at(pdb->fd, track->row + 0x20, raw, 4) ||
         read_u32(raw) != key_id))
        return false;
    return true;
}

static bool pdb_verify_snapshot(struct rbprep_pdb *pdb,
                                const unsigned char *snapshot)
{
    struct rbprep_pdb_track track;
    unsigned char raw[4];
    char genre[32];
    char key[24];
    uint32_t ignored_max;
    int genre_id = 0;
    int key_id = 0;
    unsigned char desired_color = snapshot[17] < 8
                                ? snapshot[17] + 1 : 0;

    if (!pdb_find_track(pdb, read_u32(snapshot + 8), &track))
        return false;
    rb->memcpy(genre, snapshot + 120, sizeof(genre));
    genre[sizeof(genre) - 1] = '\0';
    if (genre[0]) {
        genre_id = pdb_find_genre(pdb, genre, &ignored_max);
        if (genre_id <= 0)
            return false;
    }
    if (read_u16(snapshot + 6) >= 2) {
        rb->memcpy(key, snapshot + 192, sizeof(key));
        key[sizeof(key) - 1] = '\0';
        if (key[0]) {
            key_id = pdb_find_key(pdb, key, &ignored_max);
            if (key_id <= 0)
                return false;
        }
        if (!burn_read_at(pdb->fd, track.row + 0x20, raw, 4) ||
            read_u32(raw) != (uint32_t)key_id)
            return false;
    }
    return burn_read_at(pdb->fd, track.row + 0x38, raw, 4) &&
           read_u32(raw) == read_u32(snapshot + 24) &&
           burn_read_at(pdb->fd, track.row + 0x3c, raw, 4) &&
           read_u32(raw) == (uint32_t)genre_id &&
           burn_read_at(pdb->fd, track.row + 0x50, raw, 2) &&
           read_u16(raw) == read_u16(snapshot + 20) &&
           burn_read_at(pdb->fd, track.row + 0x58, raw, 2) &&
           raw[0] == desired_color && raw[1] == MIN(5, snapshot[16]);
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

static bool pdb_find_playlist_node(struct rbprep_pdb *pdb, uint32_t id,
                                   struct rbprep_pdb_playlist_node *result)
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
                if (read_u32(fixed + 12) != id)
                    continue;
                if (result) {
                    result->page = page;
                    result->slot = slot;
                    result->row = row;
                    result->parent = read_u32(fixed);
                    result->sort_order = read_u32(fixed + 8);
                    result->id = id;
                    result->is_folder = read_u32(fixed + 16) != 0;
                    result->name[0] = '\0';
                    pdb_decode_string(pdb, row + 20, result->name,
                                      sizeof(result->name));
                }
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

static bool pdb_mark_slot(struct rbprep_pdb *pdb, uint32_t table,
                          uint32_t page, int slot, bool deleted)
{
    struct rbprep_pdb_touch *touch;
    int slots;
    int group;
    int bit;
    int base;
    int present;

    if (!burn_read_at(pdb->fd, page * pdb->page_size,
                      rbprep_burn_page, pdb->page_size))
        return false;
    slots = pdb_slot_count(rbprep_burn_page);
    if (slot < 0 || slot >= slots ||
        !pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
        return false;
    touch = pdb_touch(pdb, table, page, slots);
    if (!touch)
        return false;
    pdb_prepare_touch(pdb, touch, rbprep_burn_page);
    group = slot / 16;
    bit = slot & 15;
    base = pdb->page_size - group * 0x24;
    if (deleted) {
        write_u16(rbprep_burn_page + base - 4,
                  read_u16(rbprep_burn_page + base - 4) & ~(1u << bit));
        rbprep_burn_page[0x1b] |= 0x10;
    } else {
        write_u16(rbprep_burn_page + base - 2,
                  read_u16(rbprep_burn_page + base - 2) | (1u << bit));
    }
    present = pdb_present_count(rbprep_burn_page, pdb->page_size, slots);
    write_u16(rbprep_burn_page + 0x19,
              0x20 * present | (slots > 255 ? 1 : 0));
    if (deleted && touch->appends == 0) {
        /* Rekordbox uses the 0x1fff sentinel for delete-only page saves. */
        write_u16(rbprep_burn_page + 0x20, 0x1fff);
        write_u16(rbprep_burn_page + 0x22, 0x1fff);
    } else {
        write_u16(rbprep_burn_page + 0x20, touch->appends);
        write_u16(rbprep_burn_page + 0x22, touch->original_slots);
    }
    write_u32(rbprep_burn_page + 0x10, touch->generation);
    return burn_write_at(pdb->fd, page * pdb->page_size,
                         rbprep_burn_page, pdb->page_size);
}

static int pdb_next_playlist_sort(struct rbprep_pdb *pdb, uint32_t parent)
{
    uint32_t entry;
    unsigned char table[16];
    uint32_t page;
    uint32_t last;
    int maximum = -1;
    int guard = 0;

    if (!pdb_table_entry(pdb, 7, &entry) ||
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
                unsigned char fixed[12];
                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                row = page * pdb->page_size + 0x28 +
                      pdb_row_heap_offset(rbprep_burn_page,
                                          pdb->page_size, slot);
                if (!burn_read_at(pdb->fd, row, fixed, sizeof(fixed)))
                    return -1;
                if (read_u32(fixed) == parent)
                    maximum = MAX(maximum, (int)read_u32(fixed + 8));
            }
        }
        if (page == last)
            break;
        if (!pdb_next_page(pdb, page, &page))
            return -1;
    }
    return maximum + 1;
}

static bool pdb_valid_playlist_parent(struct rbprep_pdb *pdb,
                                      uint32_t parent)
{
    struct rbprep_pdb_playlist_node node;
    return parent == 0 ||
           (pdb_find_playlist_node(pdb, parent, &node) && node.is_folder);
}

static bool pdb_create_playlist(struct rbprep_pdb *pdb, uint32_t id,
                                uint32_t parent, const char *name)
{
    struct rbprep_pdb_playlist_node existing;
    unsigned char row[96];
    int sort_order;
    int name_size;
    int row_size;

    if (pdb_find_playlist_node(pdb, id, &existing))
        return !existing.is_folder && existing.parent == parent &&
               !rb->strcmp(existing.name, name);
    if (!pdb_valid_playlist_parent(pdb, parent) ||
        (sort_order = pdb_next_playlist_sort(pdb, parent)) < 0)
        return false;
    write_u32(row, parent);
    write_u32(row + 4, 0);
    write_u32(row + 8, sort_order);
    write_u32(row + 12, id);
    write_u32(row + 16, 0);
    name_size = pdb_encode_name(name, row + 20, sizeof(row) - 20);
    if (name_size < 0)
        return false;
    row_size = 20 + name_size;
    return pdb_append_row(pdb, 7, row, row_size, (row_size + 3) & ~3) &&
           pdb_find_playlist_node(pdb, id, &existing) &&
           !existing.is_folder && existing.parent == parent &&
           !rb->strcmp(existing.name, name);
}

static bool pdb_rename_playlist(struct rbprep_pdb *pdb, uint32_t id,
                                const char *name)
{
    struct rbprep_pdb_playlist_node node;
    struct rbprep_pdb_playlist_node verify;
    unsigned char row[96];
    int name_size;
    int row_size;

    if (!pdb_find_playlist_node(pdb, id, &node) || node.is_folder ||
        !burn_read_at(pdb->fd, node.row, row, 20))
        return false;
    if (!rb->strcmp(node.name, name))
        return true;
    name_size = pdb_encode_name(name, row + 20, sizeof(row) - 20);
    if (name_size < 0 ||
        !pdb_mark_slot(pdb, 7, node.page, node.slot, true))
        return false;
    row_size = 20 + name_size;
    return pdb_append_row(pdb, 7, row, row_size, (row_size + 3) & ~3) &&
           pdb_find_playlist_node(pdb, id, &verify) &&
           !rb->strcmp(verify.name, name);
}

static bool pdb_move_playlist(struct rbprep_pdb *pdb, uint32_t id,
                              uint32_t parent)
{
    struct rbprep_pdb_playlist_node node;
    struct rbprep_pdb_playlist_node verify;
    unsigned char raw[4];
    int sort_order;

    if (!pdb_find_playlist_node(pdb, id, &node) || node.is_folder ||
        !pdb_valid_playlist_parent(pdb, parent))
        return false;
    if (node.parent == parent)
        return true;
    if ((sort_order = pdb_next_playlist_sort(pdb, parent)) < 0)
        return false;
    write_u32(raw, parent);
    if (!burn_write_at(pdb->fd, node.row, raw, 4))
        return false;
    write_u32(raw, sort_order);
    if (!burn_write_at(pdb->fd, node.row + 8, raw, 4) ||
        !pdb_mark_slot(pdb, 7, node.page, node.slot, false))
        return false;
    return pdb_find_playlist_node(pdb, id, &verify) &&
           verify.parent == parent;
}

static bool pdb_delete_playlist_entries(struct rbprep_pdb *pdb,
                                        uint32_t playlist)
{
    uint32_t entry;
    unsigned char table_data[16];
    uint32_t page;
    uint32_t last;
    int guard = 0;

    if (!pdb_table_entry(pdb, 8, &entry) ||
        !burn_read_at(pdb->fd, entry, table_data, sizeof(table_data)))
        return false;
    page = read_u32(table_data + 8);
    last = read_u32(table_data + 12);
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
                unsigned char values[12];
                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                row = page * pdb->page_size + 0x28 +
                      pdb_row_heap_offset(rbprep_burn_page,
                                          pdb->page_size, slot);
                if (!burn_read_at(pdb->fd, row, values, sizeof(values)))
                    return false;
                if (read_u32(values + 8) == playlist &&
                    !pdb_mark_slot(pdb, 8, page, slot, true))
                    return false;
            }
        }
        if (page == last)
            break;
        if (!pdb_next_page(pdb, page, &page))
            return false;
    }
    return true;
}

static bool pdb_delete_playlist(struct rbprep_pdb *pdb, uint32_t id)
{
    struct rbprep_pdb_playlist_node node;
    bool folder;

    if (!pdb_find_playlist_node(pdb, id, &node))
        return true;
    if (node.is_folder ||
        !pdb_mark_slot(pdb, 7, node.page, node.slot, true) ||
        !pdb_delete_playlist_entries(pdb, id))
        return false;
    return !pdb_playlist_exists(pdb, id, &folder);
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

enum rbprep_playlist_add_result {
    PLAYLIST_ADD_OK,
    PLAYLIST_ADD_TRACK_MISSING,
    PLAYLIST_ADD_PLAYLIST_MISSING,
    PLAYLIST_ADD_IS_FOLDER,
    PLAYLIST_ADD_SCAN_FAILED,
    PLAYLIST_ADD_APPEND_FAILED,
    PLAYLIST_ADD_VERIFY_FAILED
};

static enum rbprep_playlist_add_result
pdb_add_playlist_entry(struct rbprep_pdb *pdb, uint32_t track,
                       uint32_t playlist)
{
    unsigned char row[12];
    bool folder;
    bool present;
    int maximum;
    struct rbprep_pdb_track ignored;

    if (!pdb_find_track(pdb, track, &ignored))
        return PLAYLIST_ADD_TRACK_MISSING;
    if (!pdb_playlist_exists(pdb, playlist, &folder))
        return PLAYLIST_ADD_PLAYLIST_MISSING;
    if (folder)
        return PLAYLIST_ADD_IS_FOLDER;
    maximum = pdb_playlist_entry_index(pdb, track, playlist, &present);
    if (maximum < 0)
        return PLAYLIST_ADD_SCAN_FAILED;
    if (present)
        return PLAYLIST_ADD_OK;
    write_u32(row, maximum + 1);
    write_u32(row + 4, track);
    write_u32(row + 8, playlist);
    if (!pdb_append_row(pdb, 8, row, sizeof(row), sizeof(row)))
        return PLAYLIST_ADD_APPEND_FAILED;
    maximum = pdb_playlist_entry_index(pdb, track, playlist, &present);
    return maximum >= 0 && present ? PLAYLIST_ADD_OK
                                   : PLAYLIST_ADD_VERIFY_FAILED;
}

/* Return 1 when this is the newest snapshot, 0 when a later one exists, and
   -1 on an I/O/format failure.  Callers must never mistake an unreadable
   journal tail for an obsolete record. */
static int burn_record_latest_state(int fd, uint32_t track_id, off_t after)
{
    unsigned char record[RBPREP_EDIT_RECORD_SIZE];
    ssize_t got;
    int latest = 1;

    if (after < 0 || rb->lseek(fd, after, SEEK_SET) < 0)
        return -1;
    while ((got = rb->read(fd, record, sizeof(record))) == sizeof(record)) {
        if (!valid_edit_record(record)) {
            latest = -1;
            break;
        }
        if (read_u32(record + 8) == track_id) {
            latest = 0;
            break;
        }
    }
    if (latest == 1 && got != 0)
        latest = -1;
    if (rb->lseek(fd, after, SEEK_SET) < 0)
        return -1;
    return latest;
}

static bool burn_verify_metadata_journal(struct rbprep_pdb *pdb,
                                         uint32_t edit_offset,
                                         int target_track)
{
    unsigned char record[RBPREP_EDIT_RECORD_SIZE];
    off_t size;
    ssize_t got = 0;
    int fd = rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);
    bool ok = true;

    if (fd < 0)
        return false;
    size = rb->filesize(fd);
    if (size < 0 || edit_offset > (uint32_t)size ||
        edit_offset % RBPREP_EDIT_RECORD_SIZE ||
        size % RBPREP_EDIT_RECORD_SIZE ||
        rb->lseek(fd, edit_offset, SEEK_SET) < 0) {
        ok = false;
        goto done;
    }
    while ((got = rb->read(fd, record, sizeof(record))) == sizeof(record)) {
        off_t after = rb->lseek(fd, 0, SEEK_CUR);
        uint32_t track_id;
        int latest;

        if (!valid_edit_record(record)) {
            ok = false;
            break;
        }
        track_id = read_u32(record + 8);
        if (target_track >= 0 && track_id != (uint32_t)target_track)
            continue;
        latest = burn_record_latest_state(fd, track_id, after);
        if (latest < 0) {
            ok = false;
            break;
        }
        if (!latest)
            continue;
        if (!pdb_verify_snapshot(pdb, record)) {
            ok = false;
            break;
        }
    }
    if (got != 0)
        ok = false;
done:
    if (rb->close(fd) < 0)
        ok = false;
    return ok;
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

static bool burn_copy_bytes(int input, int output, off_t count)
{
    while (count > 0) {
        int amount = count > (off_t)sizeof(rbprep_burn_page)
                   ? (int)sizeof(rbprep_burn_page) : (int)count;

        if (rb->read(input, rbprep_burn_page, amount) != amount ||
            rb->write(output, rbprep_burn_page, amount) != amount)
            return false;
        count -= amount;
    }
    return true;
}

static bool burn_rewrite_track_cues(uint32_t track_id,
                                    const unsigned char *snapshot)
{
    char path[MAX_PATH];
    char temporary[MAX_PATH];
    char previous[MAX_PATH];
    unsigned char header[40];
    unsigned char cue[8];
    off_t source_size;
    off_t waveform_size = 0;
    off_t old_beat_offset;
    off_t required_size;
    off_t expected_size = 0;
    int old_cue_count;
    int beat_count = 0;
    int new_cue_count = 0;
    int input = -1;
    int output = -1;
    int slot;
    bool ok = false;

    rb->snprintf(path, sizeof(path), "%s/%06lu.rbw", RBPREP_TRACK_DIR,
                 (unsigned long)track_id);
    rb->snprintf(temporary, sizeof(temporary), "%s.rekordpod-new", path);
    rb->snprintf(previous, sizeof(previous), "%s.rekordpod-prev", path);
    if (!rb->file_exists(path) && rb->file_exists(previous))
        rb->rename(previous, path);
    if (!rb->file_exists(path))
        return true;

    input = rb->open(path, O_RDONLY);
    if (input < 0 || rb->read(input, header, sizeof(header)) !=
                     (ssize_t)sizeof(header) ||
        rb->memcmp(header, "RBW3", 4) || read_u16(header + 4) != 40)
        goto done;
    source_size = rb->filesize(input);
    old_cue_count = read_u16(header + 12);
    beat_count = read_u16(header + 14);
    waveform_size = (off_t)read_u32(header + 8) * 4;
    old_beat_offset = (off_t)sizeof(header) + waveform_size +
                      (off_t)old_cue_count * 8;
    required_size = old_beat_offset + (off_t)beat_count * 8;
    new_cue_count = burn_hotcue_count(snapshot);
    expected_size = source_size +
                    (off_t)(new_cue_count - old_cue_count) * 8;
    if (source_size < required_size || waveform_size < 0 ||
        old_beat_offset < (off_t)sizeof(header) ||
        expected_size < (off_t)sizeof(header))
        goto done;

    write_u16(header + 12, new_cue_count);
    header[32] = MIN(5, snapshot[16]);
    header[33] = normalize_track_color(snapshot[17]);
    rb->remove(temporary);
    output = rb->open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output < 0 ||
        rb->write(output, header, sizeof(header)) !=
        (ssize_t)sizeof(header) ||
        rb->lseek(input, sizeof(header), SEEK_SET) < 0 ||
        !burn_copy_bytes(input, output, waveform_size))
        goto done;

    for (slot = 0; slot < 16; slot++) {
        int32_t time = read_u32(snapshot + 40 + slot * 4);

        if (time < 0)
            continue;
        write_u32(cue, time);
        cue[4] = snapshot[104 + slot] & 7;
        cue[5] = slot + 1;
        write_u16(cue + 6, 0);
        if (rb->write(output, cue, sizeof(cue)) != (ssize_t)sizeof(cue))
            goto done;
    }
    if (rb->lseek(input, old_beat_offset, SEEK_SET) < 0 ||
        !burn_copy_bytes(input, output, source_size - old_beat_offset))
        goto done;
    ok = true;

done:
    if (input >= 0 && rb->close(input) < 0)
        ok = false;
    if (output >= 0 && rb->close(output) < 0)
        ok = false;
    if (ok) {
        int verify = rb->open(temporary, O_RDONLY);
        int expected_slot = 0;

        ok = verify >= 0 && rb->filesize(verify) == expected_size &&
             rb->read(verify, header, sizeof(header)) ==
             (ssize_t)sizeof(header) &&
             !rb->memcmp(header, "RBW3", 4) &&
             read_u16(header + 4) == 40 &&
             read_u16(header + 12) == new_cue_count &&
             read_u16(header + 14) == beat_count &&
             header[32] == MIN(5, snapshot[16]) &&
             header[33] == normalize_track_color(snapshot[17]) &&
             rb->lseek(verify, sizeof(header) + waveform_size,
                       SEEK_SET) >= 0;
        for (slot = 0; ok && slot < new_cue_count; slot++) {
            int32_t expected_time;

            while (expected_slot < 16 &&
                   (int32_t)read_u32(snapshot + 40 + expected_slot * 4) < 0)
                expected_slot++;
            if (expected_slot >= 16 ||
                rb->read(verify, cue, sizeof(cue)) !=
                (ssize_t)sizeof(cue)) {
                ok = false;
                break;
            }
            expected_time = read_u32(snapshot + 40 + expected_slot * 4);
            ok = (int32_t)read_u32(cue) == expected_time &&
                 cue[4] == (snapshot[104 + expected_slot] & 7) &&
                 cue[5] == expected_slot + 1 && read_u16(cue + 6) == 0;
            expected_slot++;
        }
        if (verify >= 0 && rb->close(verify) < 0)
            ok = false;
    }
    if (!ok) {
        rb->remove(temporary);
        return false;
    }
    return burn_swap_keep_previous(path, temporary, previous);
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
        {255, 0, 0}, {255, 94, 0}, {255, 232, 0}, {26, 255, 0},
        {0, 224, 255}, {0, 0, 255}, {77, 0, 255}, {255, 0, 161}
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
    bool saw_pcob = false;
    bool saw_pco2 = false;
    bool extended;

    rb->snprintf(backup, sizeof(backup), "%s.rekordpod-bak", path);
    rb->snprintf(temporary, sizeof(temporary), "%s.rekordpod-new", path);
    rb->snprintf(previous, sizeof(previous), "%s.rekordpod-prev", path);
    extended = rb->strrchr(path, '.') &&
               !rb->strcasecmp(rb->strrchr(path, '.'), ".EXT");
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
            saw_pcob = true;
            replacement = burn_build_pcob(output + output_at,
                                           output_capacity - output_at,
                                           snapshot);
            if (replacement < 0)
                return false;
        } else if (!rb->memcmp(source + source_at, "PCO2", 4) &&
                   tag_length >= 20 &&
                   burn_be32(source + source_at + 12) == 1) {
            saw_pco2 = true;
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
    /* Some Rekordbox exports omit an empty hotcue bank entirely. Replacing
       only tags that already exist made first-cue burns look successful but
       left nothing for Rekordbox/CDJs to discover. Materialize the missing
       type-1 bank; EXT receives its colour companion as well. */
    if (!saw_pcob) {
        int addition = burn_build_pcob(output + output_at,
                                       output_capacity - output_at,
                                       snapshot);

        if (addition < 0)
            return false;
        output_at += addition;
    }
    if (extended && !saw_pco2) {
        int addition = burn_build_pco2(output + output_at,
                                       output_capacity - output_at,
                                       snapshot);

        if (addition < 0)
            return false;
        output_at += addition;
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
    if (rb->close(fd) < 0) {
        rb->remove(temporary);
        return false;
    }
    fd = rb->open(temporary, O_RDONLY);
    if (fd < 0 || rb->filesize(fd) != output_at) {
        if (fd >= 0)
            rb->close(fd);
        rb->remove(temporary);
        return false;
    }
    source_at = 0;
    while (source_at < output_at) {
        int amount = MIN((int)sizeof(rbprep_burn_page),
                         output_at - source_at);

        if (rb->read(fd, rbprep_burn_page, amount) != amount ||
            rb->memcmp(rbprep_burn_page, output + source_at, amount)) {
            rb->close(fd);
            rb->remove(temporary);
            return false;
        }
        source_at += amount;
    }
    if (rb->close(fd) < 0) {
        rb->remove(temporary);
        return false;
    }
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

    source_bpm = burn_source_bpm(track_id, track->source_bpm_x100);
    if (track->analysis_path[0]) {
        rb->strlcpy(path, track->analysis_path, sizeof(path));
        if (!burn_rewrite_analysis(path, snapshot, source_bpm,
                                   memory, memory_size))
            return false;
        burn_analysis_variant(path, ".EXT");
        if (!burn_rewrite_analysis(path, snapshot, source_bpm,
                                   memory, memory_size))
            return false;
    }
    return burn_rewrite_track_cues(track_id, snapshot);
}

static void burn_finish_analysis_files(struct rbprep_pdb *pdb,
                                       uint32_t edit_offset, bool restore,
                                       int target_track)
{
    unsigned char record[RBPREP_EDIT_RECORD_SIZE];
    int fd = rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);

    if (fd < 0)
        return;
    if (edit_offset > (uint32_t)rb->filesize(fd))
        edit_offset = 0;
    rb->lseek(fd, edit_offset, SEEK_SET);
    while (rb->read(fd, record, sizeof(record)) == sizeof(record)) {
        off_t after = rb->lseek(fd, 0, SEEK_CUR);
        struct rbprep_pdb_track track;
        uint32_t track_id;
        char path[MAX_PATH];
        char previous[MAX_PATH];
        if (!valid_edit_record(record))
            continue;
        track_id = read_u32(record + 8);
        if ((target_track >= 0 && track_id != (uint32_t)target_track) ||
            burn_record_latest_state(fd, track_id, after) == 0)
            continue;

        rb->snprintf(path, sizeof(path), "%s/%06lu.rbw",
                     RBPREP_TRACK_DIR, (unsigned long)track_id);
        rb->snprintf(previous, sizeof(previous), "%s.rekordpod-prev", path);
        if (restore)
            burn_restore_previous(path, previous);
        else
            rb->remove(previous);

        if (!pdb_find_track(pdb, track_id, &track) ||
            !track.analysis_path[0])
            continue;
        rb->strlcpy(path, track.analysis_path, sizeof(path));
        rb->snprintf(previous, sizeof(previous), "%s.rekordpod-prev", path);
        if (restore)
            burn_restore_previous(path, previous);
        else
            rb->remove(previous);
        burn_analysis_variant(path, ".EXT");
        rb->snprintf(previous, sizeof(previous), "%s.rekordpod-prev", path);
        if (restore)
            burn_restore_previous(path, previous);
        else
            rb->remove(previous);
    }
    rb->close(fd);
}

#define RBPREP_PLAYLIST_ORDER_FOUND 0x80000000u
#define RBPREP_PLAYLIST_ORDER_RANK  0x7fffffffu

static int burn_compare_playlist_order_ids(const void *left_value,
                                           const void *right_value)
{
    const struct rbprep_playlist_order_item *left = left_value;
    const struct rbprep_playlist_order_item *right = right_value;

    if (left->track_id == right->track_id)
        return 0;
    return left->track_id < right->track_id ? -1 : 1;
}

static int burn_compare_playlist_order_rank(const void *left_value,
                                            const void *right_value)
{
    const struct rbprep_playlist_order_item *left = left_value;
    const struct rbprep_playlist_order_item *right = right_value;

    if (left->sort.value == right->sort.value)
        return 0;
    return left->sort.value < right->sort.value ? -1 : 1;
}

static bool rbi_write_member(int fd, uint32_t member);

static int burn_playlist_order_rank(uint32_t track_id, int count)
{
    int low = 0;
    int high = count;

    while (low < high) {
        int middle = low + (high - low) / 2;
        uint32_t candidate = playlist_order_items[middle].track_id;

        if (candidate < track_id)
            low = middle + 1;
        else
            high = middle;
    }
    return low < count && playlist_order_items[low].track_id == track_id
         ? low : -1;
}

static bool pdb_validate_playlist_order(struct rbprep_pdb *pdb,
                                        uint32_t playlist, int count,
                                        bool verify_sort)
{
    uint32_t entry;
    unsigned char table[16];
    uint32_t page;
    uint32_t last;
    int matched = 0;
    int guard = 0;
    int i;

    for (i = 0; i < count; i++)
        playlist_order_items[i].sort.value =
            (int)((uint32_t)playlist_order_items[i].sort.value &
                  RBPREP_PLAYLIST_ORDER_RANK);
    if (!pdb_table_entry(pdb, 8, &entry) ||
        !burn_read_at(pdb->fd, entry, table, sizeof(table)))
        return false;
    page = read_u32(table + 8);
    last = read_u32(table + 12);
    while (guard++ < 100000) {
        int slots;
        int slot;
        uint32_t next;

        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return false;
        slots = pdb_slot_count(rbprep_burn_page);
        next = read_u32(rbprep_burn_page + 0x0c);
        if (!(rbprep_burn_page[0x1b] & 0x40)) {
            for (slot = 0; slot < slots; slot++) {
                unsigned char *values;
                int rank;

                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                values = rbprep_burn_page + 0x28 +
                         pdb_row_heap_offset(rbprep_burn_page,
                                             pdb->page_size, slot);
                if (read_u32(values + 8) != playlist)
                    continue;
                rank = burn_playlist_order_rank(read_u32(values + 4), count);
                if (rank < 0 ||
                    ((uint32_t)playlist_order_items[rank].sort.value &
                     RBPREP_PLAYLIST_ORDER_FOUND) ||
                    (verify_sort && read_u32(values) !=
                     ((uint32_t)playlist_order_items[rank].sort.value &
                      RBPREP_PLAYLIST_ORDER_RANK) + 1))
                    return false;
                playlist_order_items[rank].sort.value = (int)(
                    (uint32_t)playlist_order_items[rank].sort.value |
                    RBPREP_PLAYLIST_ORDER_FOUND);
                matched++;
            }
        }
        if (page == last)
            break;
        page = next;
    }
    if (matched != count)
        return false;
    for (i = 0; i < count; i++)
        if (!((uint32_t)playlist_order_items[i].sort.value &
              RBPREP_PLAYLIST_ORDER_FOUND))
            return false;
    return true;
}

static bool pdb_apply_playlist_order(struct rbprep_pdb *pdb,
                                     uint32_t playlist, int count)
{
    uint32_t entry;
    unsigned char table[16];
    uint32_t page;
    uint32_t last;
    int guard = 0;

    if (!pdb_table_entry(pdb, 8, &entry) ||
        !burn_read_at(pdb->fd, entry, table, sizeof(table)))
        return false;
    page = read_u32(table + 8);
    last = read_u32(table + 12);
    while (guard++ < 100000) {
        struct rbprep_pdb_touch *touch;
        uint32_t next;
        int slots;
        int slot;
        bool changed = false;

        if (!burn_read_at(pdb->fd, page * pdb->page_size,
                          rbprep_burn_page, pdb->page_size))
            return false;
        slots = pdb_slot_count(rbprep_burn_page);
        next = read_u32(rbprep_burn_page + 0x0c);
        if (!(rbprep_burn_page[0x1b] & 0x40)) {
            for (slot = 0; slot < slots; slot++) {
                unsigned char *values;

                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                values = rbprep_burn_page + 0x28 +
                         pdb_row_heap_offset(rbprep_burn_page,
                                             pdb->page_size, slot);
                if (read_u32(values + 8) != playlist)
                    continue;
                changed = true;
                break;
            }
        }
        if (changed) {
            touch = pdb_touch(pdb, 8, page, slots);
            if (!touch ||
                !burn_read_at(pdb->fd, page * pdb->page_size,
                              rbprep_burn_page, pdb->page_size))
                return false;
            pdb_prepare_touch(pdb, touch, rbprep_burn_page);
            for (slot = 0; slot < slots; slot++) {
                unsigned char *values;
                int rank;
                int group;
                int bit;
                int base;

                if (!pdb_row_present(rbprep_burn_page, pdb->page_size, slot))
                    continue;
                values = rbprep_burn_page + 0x28 +
                         pdb_row_heap_offset(rbprep_burn_page,
                                             pdb->page_size, slot);
                if (read_u32(values + 8) != playlist)
                    continue;
                rank = burn_playlist_order_rank(read_u32(values + 4), count);
                if (rank < 0)
                    return false;
                write_u32(values,
                          ((uint32_t)playlist_order_items[rank].sort.value &
                           RBPREP_PLAYLIST_ORDER_RANK) + 1);
                group = slot / 16;
                bit = slot & 15;
                base = pdb->page_size - group * 0x24;
                write_u16(rbprep_burn_page + base - 2,
                          read_u16(rbprep_burn_page + base - 2) |
                          (1u << bit));
            }
            write_u32(rbprep_burn_page + 0x10, touch->generation);
            if (!burn_write_at(pdb->fd, page * pdb->page_size,
                               rbprep_burn_page, pdb->page_size))
                return false;
        }
        if (page == last)
            break;
        page = next;
    }
    return true;
}

static bool pdb_reorder_playlist(struct rbprep_pdb *pdb,
                                 const struct rbprep_pending_playlist *order)
{
    int count = load_playlist_order_sidecar(order->playlist_id,
                                            order->track_id,
                                            order->parent_id);
    int i;

    if (count <= 0)
        return false;
    rb->qsort(playlist_order_items, count,
              sizeof(playlist_order_items[0]),
              burn_compare_playlist_order_ids);
    for (i = 1; i < count; i++)
        if (playlist_order_items[i - 1].track_id ==
            playlist_order_items[i].track_id)
            return false;
    return pdb_validate_playlist_order(pdb, order->playlist_id,
                                       count, false) &&
           pdb_apply_playlist_order(pdb, order->playlist_id, count) &&
           pdb_validate_playlist_order(pdb, order->playlist_id,
                                       count, true);
}

static bool rbi_write_playlist_order(int output,
                                     const struct rbprep_node_record *node,
                                     const struct rbprep_pending_playlist *order)
{
    int count = load_playlist_order_sidecar(order->playlist_id,
                                            order->track_id,
                                            order->parent_id);
    uint32_t base;
    int i;

    if (count != (int)node->member_count)
        return false;
    rb->qsort(playlist_order_items, count,
              sizeof(playlist_order_items[0]),
              burn_compare_playlist_order_ids);
    for (i = 0; i < count; i++)
        playlist_order_items[i].track_index = -1;
    for (base = 0; base < library_track_count;) {
        uint32_t amount = MIN((uint32_t)(RBPREP_LIBRARY_SCAN_BYTES /
                                        library_track_record_size),
                              library_track_count - base);
        uint32_t item;

        if (!read_index_at(library_track_offset +
                           base * library_track_record_size,
                           library_scan_buffer,
                           amount * library_track_record_size))
            return false;
        for (item = 0; item < amount; item++) {
            struct rbprep_track_record track;
            int rank;

            decode_track_record(library_scan_buffer +
                                item * library_track_record_size, &track);
            rank = burn_playlist_order_rank(track.id, count);
            if (rank >= 0)
                playlist_order_items[rank].track_index = base + item;
        }
        base += amount;
    }
    for (i = 0; i < count; i++)
        if (playlist_order_items[i].track_index < 0)
            return false;
    rb->qsort(playlist_order_items, count,
              sizeof(playlist_order_items[0]),
              burn_compare_playlist_order_rank);
    for (i = 0; i < count; i++)
        if (!rbi_write_member(output, playlist_order_items[i].track_index))
            return false;
    return true;
}

static bool burn_playlist_operation(
    struct rbprep_pdb *pdb, const struct rbprep_pending_playlist *operation,
    int *completed, int total)
{
    uint32_t playlist = operation->playlist_id;
    bool success = false;

    rb->splash_progress(MIN(*completed, total), total,
                        "%c %.28s", operation->operation, operation->name);
    if (operation->operation == PLAYLIST_OP_ADD) {
        enum rbprep_playlist_add_result result;
        if (!pdb_resolve_playlist(pdb, operation->playlist_id,
                                  operation->name, &playlist)) {
            rb->snprintf(rbprep_burn_failure_detail,
                         sizeof(rbprep_burn_failure_detail),
                         "playlist %lu unresolved",
                         (unsigned long)operation->playlist_id);
            return false;
        }
        result = pdb_add_playlist_entry(pdb, operation->track_id, playlist);
        success = result == PLAYLIST_ADD_OK;
        if (!success) {
            const char *reason = result == PLAYLIST_ADD_TRACK_MISSING
                               ? "track missing" :
                result == PLAYLIST_ADD_PLAYLIST_MISSING
                               ? "playlist missing" :
                result == PLAYLIST_ADD_IS_FOLDER
                               ? "destination is folder" :
                result == PLAYLIST_ADD_SCAN_FAILED
                               ? "entry scan failed" :
                result == PLAYLIST_ADD_APPEND_FAILED
                               ? "page append failed" : "read-back failed";
            rb->snprintf(rbprep_burn_failure_detail,
                         sizeof(rbprep_burn_failure_detail),
                         "track %lu / list %lu: %s",
                         (unsigned long)operation->track_id,
                         (unsigned long)playlist, reason);
        }
    } else if (operation->operation == PLAYLIST_OP_CREATE) {
        /* kind 2 is an internal smart playlist. DeviceSQL receives a
           materialized ordinary playlist for current CDJ compatibility;
           its RBQ query and reserved native-rule fields stay device-local
           until the native Rekordbox rule layout is verified. */
        success = pdb_create_playlist(pdb, playlist, operation->parent_id,
                                      operation->name);
    } else if (operation->operation == PLAYLIST_OP_RENAME) {
        success = pdb_rename_playlist(pdb, playlist, operation->name);
    } else if (operation->operation == PLAYLIST_OP_MOVE) {
        success = pdb_move_playlist(pdb, playlist, operation->parent_id);
    } else if (operation->operation == PLAYLIST_OP_DELETE) {
        success = pdb_delete_playlist(pdb, playlist);
    } else if (operation->operation == PLAYLIST_OP_ORDER) {
        success = pdb_reorder_playlist(pdb, operation);
    }
    if (!success && !rbprep_burn_failure_detail[0])
        rb->snprintf(rbprep_burn_failure_detail,
                     sizeof(rbprep_burn_failure_detail),
                     "%c playlist %lu failed", operation->operation,
                     (unsigned long)playlist);
    if (success)
        (*completed)++;
    return success;
}

static bool burn_playlist_journal(struct rbprep_pdb *pdb,
                                  uint32_t playlist_offset,
                                  int *completed, int total)
{
    int i;
    (void)playlist_offset;

    /* refresh_pending_summary() has already reduced the append-only journal
       into its net transaction: duplicate adds collapse, later rename/move
       wins, and create+delete cancels before DeviceSQL is touched. */
    for (i = 0; i < pending_playlist_count; i++)
        if (!burn_playlist_operation(pdb, &pending_playlists[i],
                                     completed, total))
            return false;
    return true;
}

static const struct rbprep_pending_playlist *
rbi_playlist_operation(unsigned char operation, uint32_t playlist)
{
    int i;
    for (i = 0; i < pending_playlist_count; i++)
        if (pending_playlists[i].operation == operation &&
            pending_playlists[i].playlist_id == playlist)
            return &pending_playlists[i];
    return NULL;
}

static int rbi_node_for_source(uint32_t source_id)
{
    struct rbprep_node_record node;
    uint32_t index;

    if (!source_id)
        return -1;
    for (index = 0; index < library_node_count; index++)
        if (read_node_record(index, &node) && node.source_id == source_id &&
            node.kind != 0xff)
            return index;
    return -1;
}

static int rbi_output_node_for_source(uint32_t source_id)
{
    int index = rbi_node_for_source(source_id);
    int output_index = library_node_count;
    int i;

    if (index >= 0)
        return index;
    for (i = 0; i < pending_playlist_count; i++) {
        if (pending_playlists[i].operation != PLAYLIST_OP_CREATE)
            continue;
        if (pending_playlists[i].playlist_id == source_id)
            return output_index;
        output_index++;
    }
    return -1;
}

static bool rbi_node_has_member(const struct rbprep_node_record *node,
                                uint32_t track_index)
{
    unsigned char raw[4];
    uint32_t i;

    for (i = 0; i < node->member_count; i++)
        if (read_index_at(library_member_offset +
                          (node->first_member + i) * 4, raw, sizeof(raw)) &&
            read_u32(raw) == track_index)
            return true;
    return false;
}

static bool rbi_node_name_matches(const struct rbprep_node_record *node,
                                  const char *name)
{
    char current[64];

    return read_index_string(node->name_offset, current, sizeof(current)) &&
           !rb->strcmp(current, name);
}

static int rbi_pending_track_index[RBPREP_PENDING_MAX];

static int rbi_new_member_count(const struct rbprep_node_record *node)
{
    int count = 0;
    int i;

    for (i = 0; i < pending_playlist_count; i++) {
        if (pending_playlists[i].operation != PLAYLIST_OP_ADD ||
            pending_playlists[i].playlist_id != node->source_id)
            continue;
        if (rbi_pending_track_index[i] >= 0 &&
            !rbi_node_has_member(node, rbi_pending_track_index[i]))
            count++;
    }
    return count;
}

static bool rbi_write_member(int fd, uint32_t member)
{
    unsigned char raw[4];

    write_u32(raw, member);
    return rb->write(fd, raw, sizeof(raw)) == sizeof(raw);
}

static bool rbi_write_new_members(int fd,
                                  const struct rbprep_node_record *node)
{
    int i;

    for (i = 0; i < pending_playlist_count; i++) {
        if (pending_playlists[i].operation != PLAYLIST_OP_ADD ||
            pending_playlists[i].playlist_id != node->source_id ||
            rbi_pending_track_index[i] < 0 ||
            rbi_node_has_member(node, rbi_pending_track_index[i]))
            continue;
        if (!rbi_write_member(fd, rbi_pending_track_index[i]))
            return false;
    }
    return true;
}

static bool burn_copy_range(int input, int output, uint32_t offset,
                            uint32_t size)
{
    uint32_t remaining = size;

    if (rb->lseek(input, offset, SEEK_SET) < 0)
        return false;
    while (remaining > 0) {
        int amount = MIN((uint32_t)sizeof(rbprep_burn_page), remaining);
        int got = rb->read(input, rbprep_burn_page, amount);
        if (got != amount || rb->write(output, rbprep_burn_page, amount) != got)
            return false;
        remaining -= amount;
    }
    return true;
}

static bool rbi_write_node(int fd, const struct rbprep_node_record *node)
{
    unsigned char raw[RBPREP_NODE_RECORD];

    rb->memset(raw, 0, sizeof(raw));
    write_u32(raw, node->parent);
    write_u32(raw + 4, node->name_offset);
    write_u32(raw + 8, node->first_member);
    write_u32(raw + 12, node->member_count);
    raw[16] = node->kind;
    write_u32(raw + 20, node->source_id);
    return rb->write(fd, raw, sizeof(raw)) == sizeof(raw);
}

static bool burn_rewrite_local_index(void)
{
    unsigned char header[RBPREP_INDEX_HEADER];
    uint32_t create_count = 0;
    uint64_t member_total = 0;
    uint64_t string_growth = 0;
    uint64_t layout;
    uint32_t new_node_count;
    uint32_t new_member_count;
    uint32_t new_node_offset;
    uint32_t new_member_offset;
    uint32_t new_sort_offsets[TRACK_SORT_COUNT];
    uint32_t new_string_offset;
    uint32_t expected_size;
    uint32_t member_cursor;
    uint32_t string_cursor;
    uint32_t index;
    int output = -1;
    int i;
    bool success = false;

    if (library_fd < 0 || library_index_version != 3 ||
        !read_index_at(0, header, sizeof(header))) {
        rb->strlcpy(rbprep_burn_failure_detail, "RBI3 index required",
                    sizeof(rbprep_burn_failure_detail));
        return false;
    }
    for (i = 0; i < pending_playlist_count; i++) {
        rbi_pending_track_index[i] = -1;
        if (pending_playlists[i].operation != PLAYLIST_OP_ADD)
            continue;
        rbi_pending_track_index[i] =
            find_track_index_by_id(pending_playlists[i].track_id);
        if (rbi_pending_track_index[i] < 0) {
            rb->strlcpy(rbprep_burn_failure_detail, "RBI track missing",
                        sizeof(rbprep_burn_failure_detail));
            goto done;
        }
        if (rbi_output_node_for_source(
                pending_playlists[i].playlist_id) < 0) {
            rb->strlcpy(rbprep_burn_failure_detail,
                        "RBI playlist missing",
                        sizeof(rbprep_burn_failure_detail));
            goto done;
        }
    }
    for (i = 0; i < pending_playlist_count; i++) {
        const struct rbprep_pending_playlist *operation =
            &pending_playlists[i];
        if (operation->operation == PLAYLIST_OP_CREATE) {
            int existing = rbi_node_for_source(operation->playlist_id);
            if (existing < 0) {
                create_count++;
                string_growth += rb->strlen(operation->name) + 1;
            } else {
                struct rbprep_node_record node;
                if (!read_node_record(existing, &node))
                    goto done;
                if (!rbi_node_name_matches(&node, operation->name))
                    string_growth += rb->strlen(operation->name) + 1;
            }
        } else if (operation->operation == PLAYLIST_OP_RENAME) {
            int existing = rbi_node_for_source(operation->playlist_id);
            struct rbprep_node_record node;
            if (existing < 0 || !read_node_record(existing, &node))
                goto done;
            if (!rbi_node_name_matches(&node, operation->name))
                string_growth += rb->strlen(operation->name) + 1;
        }
    }
    for (index = 0; index < library_node_count; index++) {
        struct rbprep_node_record node;
        int additions;
        if (!read_node_record(index, &node))
            goto done;
        if ((uint64_t)node.first_member + node.member_count >
            library_member_count) {
            rb->strlcpy(rbprep_burn_failure_detail,
                        "RBI member range invalid",
                        sizeof(rbprep_burn_failure_detail));
            goto done;
        }
        if (rbi_playlist_operation(PLAYLIST_OP_DELETE, node.source_id))
            continue;
        additions = rbi_new_member_count(&node);
        member_total += node.member_count + additions;
    }
    for (i = 0; i < pending_playlist_count; i++) {
        struct rbprep_node_record node;
        if (pending_playlists[i].operation != PLAYLIST_OP_CREATE)
            continue;
        if (rbi_node_for_source(pending_playlists[i].playlist_id) >= 0)
            continue;
        rb->memset(&node, 0, sizeof(node));
        node.source_id = pending_playlists[i].playlist_id;
        member_total += rbi_new_member_count(&node);
    }
    if ((uint64_t)library_node_count + create_count > 0xffffffffu ||
        member_total > 0xffffffffu ||
        (uint64_t)library_string_size + string_growth > 0xffffffffu) {
        rb->strlcpy(rbprep_burn_failure_detail, "RBI size overflow",
                    sizeof(rbprep_burn_failure_detail));
        goto done;
    }
    new_node_count = library_node_count + create_count;
    new_member_count = (uint32_t)member_total;
    layout = (uint64_t)RBPREP_INDEX_HEADER +
             (uint64_t)library_track_count * RBPREP_TRACK_RECORD;
    if (layout > 0xffffffffu)
        goto size_overflow;
    new_node_offset = (uint32_t)layout;
    layout += (uint64_t)new_node_count * RBPREP_NODE_RECORD;
    if (layout > 0xffffffffu)
        goto size_overflow;
    new_member_offset = (uint32_t)layout;
    new_sort_offsets[TRACK_SORT_TITLE] = 0;
    layout += (uint64_t)new_member_count * 4;
    for (i = TRACK_SORT_BPM; i < TRACK_SORT_COUNT; i++) {
        if (layout > 0xffffffffu)
            goto size_overflow;
        new_sort_offsets[i] = (uint32_t)layout;
        layout += (uint64_t)library_track_count * 4;
    }
    if (layout > 0xffffffffu)
        goto size_overflow;
    new_string_offset = (uint32_t)layout;
    layout += (uint64_t)library_string_size + string_growth;
    if (layout > 0xffffffffu)
        goto size_overflow;
    expected_size = (uint32_t)layout;

    rb->remove(RBPREP_INDEX_NEW);
    output = rb->open(RBPREP_INDEX_NEW,
                      O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output < 0)
        goto done;
    rb->memset(header, 0, sizeof(header));
    rb->memcpy(header, "RBI1", 4);
    write_u16(header + 4, 3);
    write_u16(header + 6, RBPREP_INDEX_HEADER);
    write_u32(header + 8, library_track_count);
    write_u32(header + 12, new_node_count);
    write_u32(header + 16, new_member_count);
    write_u32(header + 20, RBPREP_INDEX_HEADER);
    write_u32(header + 24, new_node_offset);
    write_u32(header + 28, new_member_offset);
    write_u32(header + 32, new_string_offset);
    write_u32(header + 36, library_string_size + string_growth);
    for (i = TRACK_SORT_BPM; i < TRACK_SORT_COUNT; i++)
        write_u32(header + 36 + i * 4, new_sort_offsets[i]);
    if (rb->write(output, header, sizeof(header)) != sizeof(header) ||
        !burn_copy_range(library_fd, output, library_track_offset,
                         (uint32_t)((uint64_t)library_track_count *
                                    RBPREP_TRACK_RECORD)))
        goto done;

    member_cursor = 0;
    string_cursor = library_string_size;
    for (index = 0; index < library_node_count; index++) {
        struct rbprep_node_record node;
        const struct rbprep_pending_playlist *operation;
        int additions;
        if (!read_node_record(index, &node))
            goto done;
        operation = rbi_playlist_operation(PLAYLIST_OP_DELETE,
                                           node.source_id);
        if (operation) {
            node.parent = 0xfffffffeu;
            node.kind = 0xff;
            node.first_member = node.member_count = 0;
        } else {
            operation = node.kind != 0xff
                      ? rbi_playlist_operation(PLAYLIST_OP_CREATE,
                                               node.source_id)
                      : NULL;
            if (operation) {
                int parent = rbi_output_node_for_source(
                    operation->parent_id);
                if (operation->parent_id && parent < 0)
                    goto done;
                node.parent = parent < 0 ? RBPREP_ROOT_NODE
                                         : (uint32_t)parent;
                node.kind = MAX(0, MIN(2, operation->kind));
                if (!rbi_node_name_matches(&node, operation->name)) {
                    node.name_offset = string_cursor;
                    string_cursor += rb->strlen(operation->name) + 1;
                }
            } else {
                operation = rbi_playlist_operation(PLAYLIST_OP_MOVE,
                                                   node.source_id);
                if (operation) {
                    int parent = rbi_output_node_for_source(
                        operation->parent_id);
                    if (operation->parent_id && parent < 0)
                        goto done;
                    node.parent = parent < 0 ? RBPREP_ROOT_NODE
                                             : (uint32_t)parent;
                }
                operation = rbi_playlist_operation(PLAYLIST_OP_RENAME,
                                                   node.source_id);
                if (operation &&
                    !rbi_node_name_matches(&node, operation->name)) {
                    node.name_offset = string_cursor;
                    string_cursor += rb->strlen(operation->name) + 1;
                }
            }
            additions = rbi_new_member_count(&node);
            node.first_member = member_cursor;
            node.member_count += additions;
            member_cursor += node.member_count;
        }
        if (!rbi_write_node(output, &node))
            goto done;
    }
    for (i = 0; i < pending_playlist_count; i++) {
        const struct rbprep_pending_playlist *operation =
            &pending_playlists[i];
        struct rbprep_node_record node;
        int parent;
        if (operation->operation != PLAYLIST_OP_CREATE)
            continue;
        if (rbi_node_for_source(operation->playlist_id) >= 0)
            continue;
        parent = rbi_output_node_for_source(operation->parent_id);
        if (operation->parent_id && parent < 0)
            goto done;
        rb->memset(&node, 0, sizeof(node));
        node.parent = parent < 0 ? RBPREP_ROOT_NODE : (uint32_t)parent;
        node.name_offset = string_cursor;
        node.first_member = member_cursor;
        node.source_id = operation->playlist_id;
        node.member_count = rbi_new_member_count(&node);
        member_cursor += node.member_count;
        node.kind = MAX(0, MIN(2, operation->kind));
        string_cursor += rb->strlen(operation->name) + 1;
        if (!rbi_write_node(output, &node))
            goto done;
    }
    if (member_cursor != new_member_count ||
        string_cursor != library_string_size + string_growth)
        goto done;
    for (index = 0; index < library_node_count; index++) {
        struct rbprep_node_record node;
        const struct rbprep_pending_playlist *order;
        if (!read_node_record(index, &node))
            goto done;
        if (rbi_playlist_operation(PLAYLIST_OP_DELETE, node.source_id))
            continue;
        order = rbi_playlist_operation(PLAYLIST_OP_ORDER, node.source_id);
        if (node.member_count > 0) {
            if (order) {
                if (!rbi_write_playlist_order(output, &node, order))
                    goto done;
            } else if (!burn_copy_range(library_fd, output,
                       library_member_offset + node.first_member * 4,
                       node.member_count * 4)) {
                goto done;
            }
        }
        if (!rbi_write_new_members(output, &node))
            goto done;
    }
    for (i = 0; i < pending_playlist_count; i++) {
        struct rbprep_node_record node;
        if (pending_playlists[i].operation != PLAYLIST_OP_CREATE)
            continue;
        if (rbi_node_for_source(pending_playlists[i].playlist_id) >= 0)
            continue;
        rb->memset(&node, 0, sizeof(node));
        node.source_id = pending_playlists[i].playlist_id;
        if (!rbi_write_new_members(output, &node))
            goto done;
    }
    for (i = TRACK_SORT_BPM; i < TRACK_SORT_COUNT; i++)
        if (!burn_copy_range(library_fd, output, library_sort_offsets[i],
                             library_track_count * 4))
            goto done;
    if (!burn_copy_range(library_fd, output, library_string_offset,
                         library_string_size))
        goto done;
    for (index = 0; index < library_node_count; index++) {
        struct rbprep_node_record node;
        const struct rbprep_pending_playlist *operation;
        if (!read_node_record(index, &node))
            goto done;
        operation = node.kind != 0xff
                  ? rbi_playlist_operation(PLAYLIST_OP_CREATE,
                                           node.source_id)
                  : NULL;
        if (!operation)
            operation = rbi_playlist_operation(PLAYLIST_OP_RENAME,
                                               node.source_id);
        if (operation && !rbi_node_name_matches(&node, operation->name) &&
            rb->write(output, operation->name,
                      rb->strlen(operation->name) + 1) !=
                      (ssize_t)rb->strlen(operation->name) + 1)
            goto done;
    }
    for (i = 0; i < pending_playlist_count; i++) {
        const struct rbprep_pending_playlist *operation =
            &pending_playlists[i];
        if (operation->operation == PLAYLIST_OP_CREATE &&
            rbi_node_for_source(operation->playlist_id) < 0 &&
            rb->write(output, operation->name,
                      rb->strlen(operation->name) + 1) !=
                      (ssize_t)rb->strlen(operation->name) + 1)
            goto done;
    }
    success = rb->filesize(output) == (off_t)expected_size;
    goto done;

size_overflow:
    rb->strlcpy(rbprep_burn_failure_detail, "RBI layout overflow",
                sizeof(rbprep_burn_failure_detail));
done:
    if (output >= 0)
        rb->close(output);
    if (!success) {
        rb->remove(RBPREP_INDEX_NEW);
        if (!rbprep_burn_failure_detail[0])
            rb->strlcpy(rbprep_burn_failure_detail, "RBI parity rebuild",
                        sizeof(rbprep_burn_failure_detail));
    }
    return success;
}

static bool rbprep_burn_single_playlist_add_fast(uint32_t edit_offset)
{
    const struct rbprep_pending_playlist *operation = &pending_playlists[0];
    struct rbprep_pdb pdb;
    uint32_t playlist = operation->playlist_id;
    uint32_t playlist_size = 0;
    bool present = false;
    bool journal_captured = false;
    bool checkpoint_ready = false;
    bool success = false;
    int completed = 0;
    int journal_fd;
    int maximum;
    const char *failure = "fast playlist add";

    rb->memset(&pdb, 0, sizeof(pdb));
    pdb.fd = -1;
    if (operation->operation != PLAYLIST_OP_ADD ||
        !rb->file_exists(RBPREP_PDB) || library_fd < 0) {
        rb->strlcpy(rbprep_burn_failure_detail,
                    "fast add prerequisites",
                    sizeof(rbprep_burn_failure_detail));
        goto done;
    }
    if (!pdb_open(&pdb, RBPREP_PDB)) {
        failure = "PDB open";
        goto done;
    }
    if (!pdb_capture_page_journal(&pdb)) {
        failure = "PDB page journal";
        goto done;
    }
    journal_captured = true;
    if (!burn_playlist_operation(&pdb, operation, &completed, 1)) {
        failure = "playlist row";
        goto done;
    }
    if (pdb.structural) {
        unsigned char raw[4];

        if (!burn_read_at(pdb.fd, 0x14, raw, sizeof(raw))) {
            failure = "PDB sequence read";
            goto done;
        }
        write_u32(raw, read_u32(raw) + 1);
        if (!burn_write_at(pdb.fd, 0x14, raw, sizeof(raw))) {
            failure = "PDB sequence write";
            goto done;
        }
    }
    if (rb->close(pdb.fd) < 0) {
        pdb.fd = -1;
        failure = "PDB flush";
        goto done;
    }
    pdb.fd = -1;

    /* Reopen from storage and verify the exact relationship just written.
       Full-table structural scans remain on the general transaction path;
       this fast path validates its track, playlist, row, and page journal. */
    if (!pdb_open_retry(&pdb, RBPREP_PDB) ||
        !pdb_resolve_playlist(&pdb, operation->playlist_id,
                              operation->name, &playlist)) {
        failure = "PDB add verification";
        goto done;
    }
    maximum = pdb_playlist_entry_index(&pdb, operation->track_id,
                                       playlist, &present);
    if (maximum < 0 || !present) {
        failure = "PDB add verification";
        goto done;
    }
    if (rb->close(pdb.fd) < 0) {
        pdb.fd = -1;
        failure = "PDB verify flush";
        goto done;
    }
    pdb.fd = -1;

    /* RBI3 is much smaller than export.pdb. Rebuild it for immediate local
       playlist parity, but retain the old file by rename until the PDB page
       journal has been retired. */
    if (!burn_rewrite_local_index()) {
        failure = "Rekordpod playlist parity";
        goto done;
    }
    if (library_fd >= 0) {
        rb->close(library_fd);
        library_fd = -1;
    }
    if (!burn_swap_keep_previous(RBPREP_INDEX, RBPREP_INDEX_NEW,
                                 RBPREP_INDEX_PREV)) {
        failure = "Rekordpod index commit";
        goto done;
    }
    if (!open_library_index()) {
        failure = "Rekordpod index validation";
        goto done;
    }

    /* The PDB and local index are now a verified pair. Remove the rollback
       marker before advancing the append-only journal checkpoint: a crash in
       between merely replays an idempotent ADD on the next attempt. */
    if (rb->remove(RBPREP_PDB_PAGE_JOURNAL) < 0 &&
        rb->file_exists(RBPREP_PDB_PAGE_JOURNAL)) {
        failure = "PDB page journal retire";
        goto done;
    }
    journal_captured = false;
    rb->remove(RBPREP_INDEX_PREV);
    journal_fd = rb->open(RBPREP_PLAYLIST_JOURNAL, O_RDONLY);
    if (journal_fd >= 0) {
        off_t size = rb->filesize(journal_fd);

        if (size >= 0 && (uint64_t)size <= 0xffffffffu) {
            playlist_size = size;
            checkpoint_ready = true;
        }
        rb->close(journal_fd);
    }
    if (!checkpoint_ready ||
        !write_burn_offsets(edit_offset, playlist_size))
        rb->splash(HZ, "Saved; pending marker will retry");
    rb->splash_progress(1, 1, "Playlist saved");
    success = true;

done:
    if (pdb.fd >= 0)
        rb->close(pdb.fd);
    rb->remove(RBPREP_INDEX_NEW);
    if (!success && journal_captured) {
        if (!pdb_restore_page_journal())
            failure = "PDB page rollback";
        if (library_fd < 0)
            open_library_index();
    }
    if (!success) {
        if (rbprep_burn_failure_detail[0])
            rb->splashf(HZ * 4, "BURN FAILED: %s",
                        rbprep_burn_failure_detail);
        else
            rb->splashf(HZ * 3, "BURN FAILED: %s", failure);
    }
    restore_black_canvas();
    return success;
}

static bool rbprep_burn_transaction(int target_track, bool playlist_only)
{
    struct rbprep_pdb pdb;
    unsigned char record[RBPREP_EDIT_RECORD_SIZE];
    unsigned char *memory = NULL;
    size_t memory_size = 0;
    uint32_t edit_offset;
    uint32_t playlist_offset;
    int fd;
    ssize_t record_read = 0;
    int completed = 0;
    int total;
    bool success = false;
    bool preserve_failed_pdb = false;
    bool workspace_acquired = false;
    bool needs_analysis_workspace;
    bool recovered_tail = false;
    bool apply_playlists = target_track < 0 || playlist_only;
    int pending_index;
    const char *failure = "initialization";

    rbprep_burn_failure_detail[0] = '\0';
    if (!pdb_restore_page_journal()) {
        failure = "PDB page journal recovery";
        goto done;
    }
    refresh_pending_summary();
    if (pending_summary_overflow) {
        rb->strlcpy(rbprep_burn_failure_detail,
                    "pending change limit exceeded",
                    sizeof(rbprep_burn_failure_detail));
        goto done;
    }
    /* A playlist-only commit must not be held hostage by an unrelated edit
       journal tail.  Its own journal is fully revalidated here; track/full
       commits remain strict about both journals. */
    if (pending_journal_invalid &&
        (!playlist_only || !playlist_journal_is_complete())) {
        rb->strlcpy(rbprep_burn_failure_detail,
                    "pending journal is incomplete",
                    sizeof(rbprep_burn_failure_detail));
        goto done;
    }
    if (playlist_only && pending_playlist_count == 0) {
        success = true;
        goto done;
    }
    total = playlist_only ? MAX(1, pending_playlist_count)
          : target_track >= 0 ? 1
          : MAX(1, pending_snapshot_count + pending_playlist_count);
    needs_analysis_workspace = !playlist_only && target_track < 0 &&
                               pending_snapshot_count > 0;
    if (target_track >= 0) {
        for (pending_index = 0;
             pending_index < pending_snapshot_count; pending_index++) {
            if (pending_entries[pending_index].track_id ==
                (uint32_t)target_track) {
                needs_analysis_workspace = true;
                break;
            }
        }
        if (!needs_analysis_workspace) {
            rb->strlcpy(rbprep_burn_failure_detail,
                        "song has no pending edit",
                        sizeof(rbprep_burn_failure_detail));
            goto done;
        }
    }
    read_burn_offsets(&edit_offset, &playlist_offset);
    if (playlist_only && pending_playlist_count == 1 &&
        pending_playlists[0].operation == PLAYLIST_OP_ADD)
        return rbprep_burn_single_playlist_add_fast(edit_offset);
    if (needs_analysis_workspace) {
        stop_editor_audio();
        /* Analysis rewrites need two simultaneous copies of the largest
           ANLZ file. Playlist-only commits do not borrow the audio buffer;
           their caller pauses transport briefly to prioritize storage I/O. */
        rb->audio_stop();
        rb->yield();
        audio_was_running = false;
        playlist_playback = false;
        memory = rb->plugin_get_audio_buffer(&memory_size);
        workspace_acquired = memory != NULL;
        if (!memory || memory_size < 65536) {
            failure = "audio workspace";
            goto done;
        }
    }
    if (!rb->file_exists(RBPREP_PDB) && rb->file_exists(RBPREP_PDB_PREV))
        rb->rename(RBPREP_PDB_PREV, RBPREP_PDB);
    if (apply_playlists && pending_playlist_count > 0 &&
        !rb->file_exists(RBPREP_INDEX) &&
        rb->file_exists(RBPREP_INDEX_PREV))
        rb->rename(RBPREP_INDEX_PREV, RBPREP_INDEX);
    if (!rb->file_exists(RBPREP_PDB)) {
        failure = "export.pdb missing";
        goto done;
    }
    if (apply_playlists && pending_playlist_count > 0 && library_fd < 0 &&
        !open_library_index()) {
        failure = "Rekordpod index missing";
        goto done;
    }
    if (!burn_backup_once(RBPREP_PDB, RBPREP_PDB_BAK)) {
        failure = "PDB backup";
        goto done;
    }
    rb->remove(RBPREP_PDB_NEW);
    if (!pdb_create_working_copy(&pdb, RBPREP_PDB, RBPREP_PDB_NEW)) {
        failure = "PDB working copy";
        goto done;
    }
    {
        int recovered_pages = pdb_restore_missing_tail(&pdb);

        if (recovered_pages < 0) {
            failure = "PDB tail recovery";
            rb->close(pdb.fd);
            pdb.fd = -1;
            goto done;
        }
        if (recovered_pages > 0) {
            int close_result = rb->close(pdb.fd);

            rb->splashf(HZ * 2, "RECOVERING PDB: %d PAGES",
                        recovered_pages);
            pdb.fd = -1;
            if (close_result < 0 ||
                !pdb_open_retry(&pdb, RBPREP_PDB_NEW)) {
                failure = "PDB tail recovery";
                goto done;
            }
            recovered_tail = true;

            /* This is the reliable behavior of the original burner: rebuild
               from the append-only history instead of trusting a checkpoint.
               The recovered prefix keeps newer pages; replaying every latest
               track snapshot and the reduced playlist transaction restores
               any successful edits that used to reside in the missing tail. */
            target_track = -1;
            playlist_only = false;
            apply_playlists = true;
            /* Replay valid legacy history, but keep the already-validated
               pending suffix strict. Early experimental journal formats can
               contain obsolete entries (notably playlist id zero) that the
               original burner ignored after checkpointing. */
            refresh_pending_summary_from_offsets(0, 0, edit_offset,
                                                 playlist_offset);
            if (pending_summary_overflow || pending_journal_invalid) {
                rb->strlcpy(rbprep_burn_failure_detail,
                            "recovery journal incomplete",
                            sizeof(rbprep_burn_failure_detail));
                rb->close(pdb.fd);
                pdb.fd = -1;
                goto done;
            }
            edit_offset = 0;
            playlist_offset = 0;
            total = MAX(1, pending_snapshot_count +
                           pending_playlist_count);
            if (pending_snapshot_count > 0 && !workspace_acquired) {
                stop_editor_audio();
                rb->audio_stop();
                rb->yield();
                audio_was_running = false;
                playlist_playback = false;
                memory = rb->plugin_get_audio_buffer(&memory_size);
                workspace_acquired = memory != NULL;
                if (!memory || memory_size < 65536) {
                    failure = "recovery audio workspace";
                    rb->close(pdb.fd);
                    pdb.fd = -1;
                    goto done;
                }
            }
            if (pending_playlist_count > 0 && library_fd < 0 &&
                !open_library_index()) {
                failure = "recovery index missing";
                rb->close(pdb.fd);
                pdb.fd = -1;
                goto done;
            }
        }
    }
    if (!pdb_validate_structure(&pdb)) {
        failure = "source PDB structure";
        rb->close(pdb.fd);
        goto done;
    }
    if (recovered_tail)
        pdb.structural = true;

    fd = playlist_only ? -1 : rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);
    if (fd >= 0) {
        if (edit_offset > (uint32_t)rb->filesize(fd) ||
            edit_offset % RBPREP_EDIT_RECORD_SIZE)
            edit_offset = 0;
        if (rb->lseek(fd, edit_offset, SEEK_SET) < 0) {
            failure = "edit journal seek";
            rb->close(fd);
            rb->close(pdb.fd);
            goto done;
        }
        while ((record_read = rb->read(fd, record, sizeof(record))) ==
               sizeof(record)) {
            off_t after = rb->lseek(fd, 0, SEEK_CUR);
            struct rbprep_pdb_track track;
            uint32_t track_id;
            int latest;
            if (!valid_edit_record(record))
                continue;
            track_id = read_u32(record + 8);
            if (target_track >= 0 && track_id != (uint32_t)target_track)
                continue;
            latest = burn_record_latest_state(fd, track_id, after);
            if (latest < 0) {
                failure = "edit journal scan";
                rb->close(fd);
                if (!playlist_only)
                    burn_finish_analysis_files(&pdb, edit_offset, true,
                                               target_track);
                rb->close(pdb.fd);
                goto done;
            }
            if (!latest)
                continue;
            rb->splash_progress(MIN(completed, total), total,
                                "Burning track %lu",
                                (unsigned long)track_id);
            if (!pdb_patch_snapshot(&pdb, record, &track)) {
                failure = "track lookup/PDB";
                rb->close(fd);
                if (!playlist_only)
                    burn_finish_analysis_files(&pdb, edit_offset, true,
                                               target_track);
                rb->close(pdb.fd);
                goto done;
            }
            if (!burn_analysis_for_track(&track, track_id, record,
                                         memory, memory_size)) {
                failure = "ANLZ rewrite";
                rb->close(fd);
                if (!playlist_only)
                    burn_finish_analysis_files(&pdb, edit_offset, true,
                                               target_track);
                rb->close(pdb.fd);
                goto done;
            }
            completed++;
        }
        if (record_read != 0) {
            failure = "edit journal read";
            rb->close(fd);
            if (!playlist_only)
                burn_finish_analysis_files(&pdb, edit_offset, true,
                                           target_track);
            rb->close(pdb.fd);
            goto done;
        }
        rb->close(fd);
    }
    if (apply_playlists &&
        !burn_playlist_journal(&pdb, playlist_offset, &completed, total)) {
        failure = "playlist table";
        if (!playlist_only)
            burn_finish_analysis_files(&pdb, edit_offset, true, target_track);
        rb->close(pdb.fd);
        goto done;
    }
    if (pdb.structural) {
        unsigned char raw[4];
        if (!burn_read_at(pdb.fd, 0x14, raw, 4)) {
            failure = "PDB sequence read";
            if (!playlist_only)
                burn_finish_analysis_files(&pdb, edit_offset, true,
                                           target_track);
            rb->close(pdb.fd);
            goto done;
        }
        write_u32(raw, read_u32(raw) + 1);
        if (!burn_write_at(pdb.fd, 0x14, raw, 4)) {
            failure = "PDB sequence write";
            if (!playlist_only)
                burn_finish_analysis_files(&pdb, edit_offset, true,
                                           target_track);
            rb->close(pdb.fd);
            goto done;
        }
    }
    /* Close first so Rockbox's FAT/file cache commits every surgical page
       write before the whole file is scanned. A read-after-write scan on
       this same descriptor can observe a mixture of old and new page data
       on the iPod even though reopening produces the correct view. */
    if (rb->close(pdb.fd) < 0) {
        struct rbprep_pdb original;

        pdb.fd = -1;
        failure = "PDB flush";
        if (pdb_open(&original, RBPREP_PDB)) {
            if (!playlist_only)
                burn_finish_analysis_files(&original, edit_offset, true,
                                           target_track);
            rb->close(original.fd);
        }
        goto done;
    }
    pdb.fd = -1;
    if (!pdb_open_retry(&pdb, RBPREP_PDB_NEW)) {
        failure = "PDB validation";
        struct rbprep_pdb original;
        if (pdb_open(&original, RBPREP_PDB)) {
            if (!playlist_only)
                burn_finish_analysis_files(&original, edit_offset, true,
                                           target_track);
            rb->close(original.fd);
        }
        goto done;
    }
    if (!pdb_validate_structure(&pdb)) {
        failure = "PDB validation";
        preserve_failed_pdb = true;
        if (!playlist_only)
            burn_finish_analysis_files(&pdb, edit_offset, true, target_track);
        rb->close(pdb.fd);
        goto done;
    }
    if (!playlist_only && pending_snapshot_count > 0 &&
        !burn_verify_metadata_journal(&pdb, edit_offset, target_track)) {
        failure = "PDB metadata verification";
        preserve_failed_pdb = true;
        burn_finish_analysis_files(&pdb, edit_offset, true, target_track);
        rb->close(pdb.fd);
        goto done;
    }
    if (apply_playlists && pending_playlist_count > 0 &&
        (!burn_backup_once(RBPREP_INDEX, RBPREP_INDEX_BAK) ||
         !burn_rewrite_local_index())) {
        failure = "Rekordpod playlist parity";
        if (!playlist_only)
            burn_finish_analysis_files(&pdb, edit_offset, true, target_track);
        rb->close(pdb.fd);
        goto done;
    }
    rb->close(pdb.fd);
    if (!burn_swap_keep_previous(RBPREP_PDB, RBPREP_PDB_NEW,
                                 RBPREP_PDB_PREV)) {
        failure = "PDB commit";
        rb->remove(RBPREP_INDEX_NEW);
        struct rbprep_pdb original;
        if (pdb_open(&original, RBPREP_PDB)) {
            if (!playlist_only)
                burn_finish_analysis_files(&original, edit_offset, true,
                                           target_track);
            rb->close(original.fd);
        }
        goto done;
    }
    if (apply_playlists && pending_playlist_count > 0) {
        if (library_fd >= 0) {
            rb->close(library_fd);
            library_fd = -1;
        }
        if (!burn_swap_keep_previous(RBPREP_INDEX, RBPREP_INDEX_NEW,
                                     RBPREP_INDEX_PREV)) {
            failure = "Rekordpod index commit";
            if (pdb_open(&pdb, RBPREP_PDB)) {
                if (!playlist_only)
                    burn_finish_analysis_files(&pdb, edit_offset, true,
                                               target_track);
                rb->close(pdb.fd);
            }
            burn_restore_previous(RBPREP_PDB, RBPREP_PDB_PREV);
            open_library_index();
            goto done;
        }
        if (!open_library_index()) {
            failure = "Rekordpod index validation";
            if (pdb_open(&pdb, RBPREP_PDB)) {
                if (!playlist_only)
                    burn_finish_analysis_files(&pdb, edit_offset, true,
                                               target_track);
                rb->close(pdb.fd);
            }
            burn_restore_previous(RBPREP_PDB, RBPREP_PDB_PREV);
            burn_restore_previous(RBPREP_INDEX, RBPREP_INDEX_PREV);
            open_library_index();
            goto done;
        }
    }
    if (!pdb_open_retry(&pdb, RBPREP_PDB)) {
        failure = "PDB reopen";
        struct rbprep_pdb original;
        if (pdb_open(&original, RBPREP_PDB_PREV)) {
            if (!playlist_only)
                burn_finish_analysis_files(&original, edit_offset, true,
                                           target_track);
            rb->close(original.fd);
        }
        burn_restore_previous(RBPREP_PDB, RBPREP_PDB_PREV);
        if (apply_playlists && pending_playlist_count > 0) {
            if (library_fd >= 0) {
                rb->close(library_fd);
                library_fd = -1;
            }
            burn_restore_previous(RBPREP_INDEX, RBPREP_INDEX_PREV);
        }
        if (library_fd < 0)
            open_library_index();
        goto done;
    }
    if (!playlist_only)
        burn_finish_analysis_files(&pdb, edit_offset, false, target_track);
    rb->close(pdb.fd);
    rb->remove(RBPREP_PDB_PREV);
    if (apply_playlists && pending_playlist_count > 0)
        rb->remove(RBPREP_INDEX_PREV);
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
        if (playlist_only) {
            if (!write_burn_offsets(edit_offset, playlist_size))
                rb->splash(HZ, "Burned; pending marker failed");
        } else if (target_track >= 0) {
            if (!commit_pending_edit((uint32_t)target_track))
                rb->splash(HZ, "Burned; song checkpoint failed");
        } else if (!write_burn_offsets(edit_size, playlist_size))
            rb->splash(HZ, "Burned; pending marker failed");
    }
    rb->splash_progress(total, total, "Rekordbox burn complete");
    success = true;
done:
    if (preserve_failed_pdb && rb->file_exists(RBPREP_PDB_NEW))
        burn_copy_file(RBPREP_PDB_NEW, RBPREP_PDB_FAILED);
    else if (success)
        rb->remove(RBPREP_PDB_FAILED);
    rb->remove(RBPREP_PDB_NEW);
    rb->remove(RBPREP_INDEX_NEW);
    if (library_fd < 0)
        open_library_index();
    if (workspace_acquired)
        rb->plugin_release_audio_buffer();
    if (!success) {
        if (rbprep_burn_failure_detail[0])
            rb->splashf(HZ * 4, "BURN FAILED: %s",
                        rbprep_burn_failure_detail);
        else
            rb->splashf(HZ * 3, "BURN FAILED: %s", failure);
    }
    restore_black_canvas();
    return success;
}

static bool rbprep_burn_all(void)
{
    return rbprep_burn_transaction(-1, false);
}

static bool rbprep_burn_song(uint32_t track_id)
{
    return rbprep_burn_transaction((int)track_id, false);
}

static bool rbprep_burn_playlists(void)
{
    return rbprep_burn_transaction(-1, true);
}
