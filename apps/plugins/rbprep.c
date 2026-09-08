#include "plugin.h"

#if CONFIG_KEYPAD != IPOD_4G_PAD
#error "RBPrep currently targets the iPod click wheel"
#endif

#define RBPREP_PDB "/PIONEER/rekordbox/export.pdb"
#define RBPREP_INDEX "/.rockbox/rbprep/library.rbi"
#define RBPREP_TRACK_DIR "/.rockbox/rbprep/tracks"
#define RBPREP_POINTS 131072
#define RBPREP_BEATS 16384
#define RBPREP_INDEX_HEADER 40
#define RBPREP_TRACK_RECORD 24
#define RBPREP_NODE_RECORD 20
#define RBPREP_ROOT_NODE 0xffffffffu
#define RBPREP_LIST_ROWS 9
#define RBPREP_TREE_NODES 2048
#define RBPREP_HUD_WIDTH 40
#define RBPREP_DECK_X (RBPREP_HUD_WIDTH + 2)
#define RBPREP_DECK_WIDTH (LCD_WIDTH - RBPREP_DECK_X)
#define RBPREP_WAVE_TOP 20
#define RBPREP_WAVE_BOTTOM 209
#define RBPREP_MAX_ZOOM 128
#define RBPREP_SEEK_DEBOUNCE MAX(1, HZ / 20)
#define RBPREP_SEEK_SETTLE MAX(1, HZ / 10)
#define RBPREP_PREVIEW_TICKS MAX(1, HZ / 5)

enum rbprep_mode {
    MODE_LIBRARY,
    MODE_PLAYLISTS,
    MODE_TRACKS,
    MODE_DECK,
    MODE_CUES,
    MODE_GRID,
    MODE_METADATA
};

static enum rbprep_mode mode;
static int selection;
static int playhead;
static unsigned char waveform[RBPREP_POINTS][4];
static int waveform_points;
static int waveform_duration_ms = 1;
static int beat_times[RBPREP_BEATS];
static unsigned char beat_numbers[RBPREP_BEATS];
static int beat_count;
static int zoom = 1;
static int grid_offset;
static int grid_phase_ms = 26;
static int grid_bpm_x100 = 15000;
static int grid_beat_shift;
static int cue_slot;
static int deck_cue = -1;
static int hotcues[16];
static unsigned char hotcue_colors[16];
static int rating;
static int color_index;
static int track_length = 240000;
static bool quantize = true;
static bool force_full_redraw = true;
static bool suppress_menu;
static bool suppress_play;
static bool suppress_left;
static bool suppress_right;

enum rbprep_seek_state {
    SEEK_IDLE,
    SEEK_DEBOUNCE,
    SEEK_SETTLE,
    SEEK_PREVIEW
};

static enum rbprep_seek_state seek_state;
static bool seek_preview;
static bool seek_was_paused;
static int seek_target;
static long seek_deadline;
static long seek_applied_tick;
static int play_clock_anchor;
static long play_clock_tick;

struct rbprep_track_record {
    uint32_t id;
    uint32_t path_offset;
    uint32_t title_offset;
    uint32_t artist_offset;
    uint32_t genre_offset;
    int bpm_x100;
    int rating;
    int color;
};

struct rbprep_node_record {
    uint32_t parent;
    uint32_t name_offset;
    uint32_t first_member;
    uint32_t member_count;
    int kind;
};

static int library_fd = -1;
static uint32_t library_track_count;
static uint32_t library_node_count;
static uint32_t library_member_count;
static uint32_t library_track_offset;
static uint32_t library_node_offset;
static uint32_t library_member_offset;
static uint32_t library_string_offset;
static uint32_t tree_parent = RBPREP_ROOT_NODE;
static int tree_selection;
static int tree_top;
static int tree_child_count;
static int tree_children[RBPREP_TREE_NODES];
static int track_selection;
static int track_top;
static int track_row_count;
static int active_playlist_node = -1;
static int selected_track_index = -1;
static int selected_track_id = -1;
static enum rbprep_mode deck_return_mode = MODE_LIBRARY;
static char selected_title[96];
static char selected_artist[72];
static char selected_genre[32];

static void stop_editor_audio(void);
static void reset_play_clock(int anchor, long tick);

static const char *color_labels[] = {
    "SAMPLE", "OPENER", "BUILDER", "PIVOTER",
    "MAINTAINER", "PEAK", "RESET", "TOOL"
};

static const char *mode_names[] = {
    "LIBRARY", "PLAYLISTS", "TRACKS", "PLAYBACK", "HOT CUES",
    "BEATGRID", "METADATA"
};

static const int cue_palette[] = {
    LCD_RGBPACK(255, 70, 70),
    LCD_RGBPACK(255, 145, 40),
    LCD_RGBPACK(250, 220, 45),
    LCD_RGBPACK(55, 235, 95),
    LCD_RGBPACK(50, 225, 225),
    LCD_RGBPACK(55, 135, 255),
    LCD_RGBPACK(175, 90, 255),
    LCD_RGBPACK(255, 80, 185)
};

static void text(int x, int y, const char *s, int color)
{
    rb->lcd_set_foreground(color);
    rb->lcd_putsxy(x, y, s);
}

static uint16_t read_u16(const unsigned char *data)
{
    return data[0] | (data[1] << 8);
}

static uint32_t read_u32(const unsigned char *data)
{
    return data[0] | (data[1] << 8) | (data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static bool read_index_at(uint32_t offset, void *data, size_t size)
{
    return library_fd >= 0 &&
           rb->lseek(library_fd, offset, SEEK_SET) >= 0 &&
           rb->read(library_fd, data, size) == (ssize_t)size;
}

static bool read_track_record(int index, struct rbprep_track_record *track)
{
    unsigned char data[RBPREP_TRACK_RECORD];

    if (index < 0 || (uint32_t)index >= library_track_count ||
        !read_index_at(library_track_offset + index * RBPREP_TRACK_RECORD,
                       data, sizeof(data)))
        return false;
    track->id = read_u32(data);
    track->path_offset = read_u32(data + 4);
    track->title_offset = read_u32(data + 8);
    track->artist_offset = read_u32(data + 12);
    track->genre_offset = read_u32(data + 16);
    track->bpm_x100 = read_u16(data + 20);
    track->rating = data[22];
    track->color = data[23];
    return true;
}

static bool read_node_record(int index, struct rbprep_node_record *node)
{
    unsigned char data[RBPREP_NODE_RECORD];

    if (index < 0 || (uint32_t)index >= library_node_count ||
        !read_index_at(library_node_offset + index * RBPREP_NODE_RECORD,
                       data, sizeof(data)))
        return false;
    node->parent = read_u32(data);
    node->name_offset = read_u32(data + 4);
    node->first_member = read_u32(data + 8);
    node->member_count = read_u32(data + 12);
    node->kind = data[16];
    return true;
}

static bool read_index_string(uint32_t offset, char *buffer, size_t size)
{
    size_t used = 0;
    unsigned char value;

    if (size == 0 || library_fd < 0 ||
        rb->lseek(library_fd, library_string_offset + offset, SEEK_SET) < 0)
        return false;
    while (used + 1 < size && rb->read(library_fd, &value, 1) == 1 && value)
        buffer[used++] = value;
    buffer[used] = '\0';
    return true;
}

static int child_node_at(uint32_t parent, int ordinal,
                         struct rbprep_node_record *result)
{
    int index;
    int found = 0;
    struct rbprep_node_record node;

    if (parent == tree_parent && ordinal >= 0 &&
        ordinal < tree_child_count) {
        index = tree_children[ordinal];
        if (result && !read_node_record(index, result))
            return -1;
        return index;
    }
    for (index = 0; (uint32_t)index < library_node_count; index++) {
        if (!read_node_record(index, &node))
            break;
        if (node.parent != parent)
            continue;
        if (found++ == ordinal) {
            if (result)
                *result = node;
            return index;
        }
    }
    return -1;
}

static void refresh_tree_children(uint32_t parent)
{
    int index;
    struct rbprep_node_record node;

    tree_parent = parent;
    tree_child_count = 0;
    for (index = 0; (uint32_t)index < library_node_count; index++) {
        if (!read_node_record(index, &node))
            break;
        if (node.parent == parent && tree_child_count < RBPREP_TREE_NODES)
            tree_children[tree_child_count++] = index;
    }
}

static int track_index_at_row(int row)
{
    unsigned char data[4];
    struct rbprep_node_record node;

    if (active_playlist_node < 0)
        return row;
    if (!read_node_record(active_playlist_node, &node) || row < 0 ||
        (uint32_t)row >= node.member_count ||
        !read_index_at(library_member_offset +
                       (node.first_member + row) * 4, data, sizeof(data)))
        return -1;
    return read_u32(data);
}

static bool open_library_index(void)
{
    unsigned char header[RBPREP_INDEX_HEADER];

    library_fd = rb->open(RBPREP_INDEX, O_RDONLY);
    if (library_fd < 0 ||
        rb->read(library_fd, header, sizeof(header)) != sizeof(header) ||
        rb->memcmp(header, "RBI1", 4) || read_u16(header + 4) != 1) {
        if (library_fd >= 0)
            rb->close(library_fd);
        library_fd = -1;
        return false;
    }
    library_track_count = read_u32(header + 8);
    library_node_count = read_u32(header + 12);
    library_member_count = read_u32(header + 16);
    library_track_offset = read_u32(header + 20);
    library_node_offset = read_u32(header + 24);
    library_member_offset = read_u32(header + 28);
    library_string_offset = read_u32(header + 32);
    refresh_tree_children(RBPREP_ROOT_NODE);
    return true;
}

static int clamp_playhead(int value)
{
    return MAX(0, MIN(track_length, value));
}

static int beat_period_ms(void)
{
    if (grid_bpm_x100 <= 0)
        return 500;
    return MAX(1, 6000000 / grid_bpm_x100);
}

static int adjusted_beat_number(int index)
{
    return ((beat_numbers[index] - 1 + grid_beat_shift) & 3) + 1;
}

static int nearest_beat_index(int time_ms)
{
    int low = 0;
    int high = beat_count;
    int target = time_ms - grid_offset;

    if (beat_count <= 0)
        return -1;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (beat_times[middle] < target)
            low = middle + 1;
        else
            high = middle;
    }
    if (low <= 0)
        return 0;
    if (low >= beat_count)
        return beat_count - 1;
    if (target - beat_times[low - 1] <= beat_times[low] - target)
        return low - 1;
    return low;
}

static int quantized_time(int time_ms)
{
    long long delta;
    long long beat;
    int index;
    int period;
    int base;

    if (!quantize)
        return clamp_playhead(time_ms);

    index = nearest_beat_index(time_ms);
    if (index >= 0)
        return clamp_playhead(beat_times[index] + grid_offset);

    period = beat_period_ms();
    base = grid_phase_ms + grid_offset;
    delta = (long long)time_ms - base;
    if (delta >= 0)
        beat = (delta + period / 2) / period;
    else
        beat = (delta - period / 2) / period;
    return clamp_playhead(base + beat * period);
}

static int scrub_step_ms(void)
{
    if (zoom >= 128) return 1;
    if (zoom >= 64) return 2;
    if (zoom >= 32) return 5;
    if (zoom >= 16) return 10;
    if (zoom >= 8) return 20;
    if (zoom >= 4) return 50;
    if (zoom >= 2) return 100;
    return 250;
}

static void viewport(int *first, int *span)
{
    int center;

    if (waveform_points <= 0) {
        *first = 0;
        *span = 1;
        return;
    }

    *span = MAX(1, waveform_points / zoom);
    center = (long long)playhead * waveform_points /
             MAX(1, waveform_duration_ms);
    *first = zoom == 1 ? 0 : center - *span / 2;
}

static int time_to_x(int time_ms, int first, int span)
{
    long long sample;

    if (waveform_points <= 0 || track_length <= 0)
        return -1;
    sample = (long long)time_ms * waveform_points /
             MAX(1, waveform_duration_ms);
    return RBPREP_DECK_X +
           (sample - first) * RBPREP_DECK_WIDTH / span;
}

static void draw_beatgrid(int first, int span)
{
    long long view_start;
    long long view_end;
    int period = beat_period_ms();
    int base = grid_phase_ms + grid_offset;
    int pixels_per_beat;
    int stride = 1;
    int i;

    if (waveform_points <= 0)
        return;

    view_start = MAX(0, (long long)first * waveform_duration_ms /
                        waveform_points);
    view_end = MIN(track_length,
                   (long long)(first + span) * waveform_duration_ms /
                   waveform_points);
    pixels_per_beat = (long long)period * RBPREP_DECK_WIDTH /
                      MAX(1, view_end - view_start);

    if (beat_count > 0) {
        for (i = 0; i < beat_count; i++) {
            int time_ms = beat_times[i] + grid_offset;
            int number = adjusted_beat_number(i);
            int x;

            if (time_ms < view_start)
                continue;
            if (time_ms > view_end)
                break;
            if (pixels_per_beat < 3 && number != 1)
                continue;
            if (pixels_per_beat * 4 < 3 &&
                number == 1 && ((i / 4) & 3))
                continue;
            x = time_to_x(time_ms, first, span);
            if (x >= RBPREP_DECK_X && x < LCD_WIDTH) {
                rb->lcd_set_foreground(number == 1
                    ? LCD_RGBPACK(235, 235, 235)
                    : LCD_RGBPACK(85, 105, 90));
                rb->lcd_vline(x, RBPREP_WAVE_TOP,
                              RBPREP_WAVE_BOTTOM);
            }
        }
        return;
    }

    if (pixels_per_beat < 3)
        stride = 4;
    if (pixels_per_beat * 4 < 3)
        stride = 16;
    for (i = 0; base + i * period <= view_end; i += stride) {
        int time_ms = base + i * period;
        int x;
        if (time_ms < view_start)
            continue;
        x = time_to_x(time_ms, first, span);
        if (x >= 0 && x < LCD_WIDTH) {
            rb->lcd_set_foreground((i & 3) == 0
                ? LCD_RGBPACK(235, 235, 235)
                : LCD_RGBPACK(85, 105, 90));
            rb->lcd_vline(x, RBPREP_WAVE_TOP, RBPREP_WAVE_BOTTOM);
        }
    }
}

static void draw_cues(int first, int span)
{
    int i;
    char label[3];

    for (i = 0; i < 16; i++) {
        int x;
        int box_x;
        int box_y;
        int color;

        if (hotcues[i] < 0)
            continue;
        x = time_to_x(hotcues[i], first, span);
        if (x < RBPREP_DECK_X || x >= LCD_WIDTH)
            continue;

        color = cue_palette[hotcue_colors[i] & 7];
        box_x = MAX(RBPREP_DECK_X, MIN(LCD_WIDTH - 11, x - 5));
        box_y = RBPREP_WAVE_TOP + 3 + (i & 1) * 12;
        rb->lcd_set_foreground(color);
        rb->lcd_vline(x, box_y + 10, RBPREP_WAVE_BOTTOM);
        rb->lcd_fillrect(box_x, box_y, 11, 11);
        rb->lcd_set_foreground(LCD_BLACK);
        rb->snprintf(label, sizeof(label), "%X", i + 1);
        rb->lcd_putsxy(box_x + 2, box_y + 1, label);
    }
}

static void draw_waveform(void)
{
    int first;
    int span;
    int x;
    int mid = (RBPREP_WAVE_TOP + RBPREP_WAVE_BOTTOM) / 2;
    int max_height = (RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP) / 2 - 2;

    viewport(&first, &span);
    for (x = 0; x < RBPREP_DECK_WIDTH; x++) {
        int begin;
        int end;
        int index;
        int peak_index;
        int peak;
        int height;
        int color;
        int screen_x = RBPREP_DECK_X + x;

        if (waveform_points <= 0) {
            rb->lcd_set_foreground(LCD_DARKGRAY);
            rb->lcd_vline(screen_x, mid - 3, mid + 3);
            continue;
        }

        begin = first + (long long)x * span / RBPREP_DECK_WIDTH;
        end = first + (long long)(x + 1) * span / RBPREP_DECK_WIDTH;
        if (end <= 0 || begin >= waveform_points) {
            rb->lcd_set_foreground(LCD_RGBPACK(15, 23, 17));
            rb->lcd_vline(screen_x, mid - 1, mid + 1);
            continue;
        }
        begin = MAX(0, begin);
        if (end <= begin)
            end = begin + 1;
        end = MIN(end, waveform_points);
        peak_index = MIN(begin, waveform_points - 1);
        peak = waveform[peak_index][0];
        for (index = begin + 1; index < end; index++) {
            if (waveform[index][0] > peak) {
                peak = waveform[index][0];
                peak_index = index;
            }
        }

        height = 2 + peak * max_height / 255;
        color = LCD_RGBPACK(waveform[peak_index][1],
                            waveform[peak_index][2],
                            waveform[peak_index][3]);
        rb->lcd_set_foreground(color);
        rb->lcd_vline(screen_x, mid - height, mid + height);
    }

    draw_beatgrid(first, span);
    draw_cues(first, span);
    rb->lcd_set_foreground(LCD_RGBPACK(255, 45, 45));
    x = MAX(RBPREP_DECK_X, MIN(LCD_WIDTH - 1,
                   time_to_x(playhead, first, span)));
    rb->lcd_vline(x, RBPREP_WAVE_TOP, RBPREP_WAVE_BOTTOM);
}

static void clear_analysis(void)
{
    int i;

    waveform_points = 0;
    waveform_duration_ms = 1;
    beat_count = 0;
    grid_offset = 0;
    grid_beat_shift = 0;
    deck_cue = -1;
    for (i = 0; i < 16; i++) {
        hotcues[i] = -1;
        hotcue_colors[i] = 3;
    }
}

static bool load_waveform(int track_id)
{
    int fd;
    int i;
    int cue_count;
    int declared_points;
    int declared_beats;
    char filename[MAX_PATH];
    unsigned char header[40];

    clear_analysis();
    rb->snprintf(filename, sizeof(filename), "%s/%06d.rbw",
                 RBPREP_TRACK_DIR, track_id);
    fd = rb->open(filename, O_RDONLY);
    if (fd < 0)
        return false;
    if (rb->read(fd, header, sizeof(header)) != sizeof(header) ||
        rb->memcmp(header, "RBW3", 4) || read_u16(header + 4) != 40) {
        rb->close(fd);
        return false;
    }

    declared_points = read_u32(header + 8);
    cue_count = read_u16(header + 12);
    declared_beats = read_u16(header + 14);
    track_length = MAX(1, (int)read_u32(header + 16));
    waveform_duration_ms = MAX(1, (int)read_u32(header + 20));
    grid_bpm_x100 = read_u16(header + 24);
    grid_phase_ms = read_u32(header + 28);
    rating = MIN(5, header[32]);
    color_index = header[33] & 7;

    waveform_points = MIN(declared_points, RBPREP_POINTS);
    if (rb->read(fd, waveform, waveform_points * 4) !=
        waveform_points * 4) {
        clear_analysis();
        rb->close(fd);
        return false;
    }
    if (declared_points > waveform_points)
        rb->lseek(fd, (declared_points - waveform_points) * 4, SEEK_CUR);

    for (i = 0; i < cue_count; i++) {
        unsigned char cue[8];
        int slot;
        if (rb->read(fd, cue, sizeof(cue)) != sizeof(cue))
            break;
        slot = cue[5];
        if (slot > 0 && slot <= 16) {
            hotcues[slot - 1] = read_u32(cue);
            hotcue_colors[slot - 1] = cue[4] & 7;
        }
    }
    beat_count = MIN(declared_beats, RBPREP_BEATS);
    for (i = 0; i < beat_count; i++) {
        unsigned char beat[8];
        if (rb->read(fd, beat, sizeof(beat)) != sizeof(beat)) {
            beat_count = i;
            break;
        }
        beat_times[i] = read_u32(beat);
        beat_numbers[i] = MAX(1, MIN(4, beat[4]));
    }
    rb->close(fd);
    return true;
}

static void open_track_browser(int playlist_node)
{
    struct rbprep_node_record node;

    active_playlist_node = playlist_node;
    track_selection = 0;
    track_top = 0;
    if (playlist_node < 0) {
        track_row_count = library_track_count;
    } else if (read_node_record(playlist_node, &node)) {
        track_row_count = node.member_count;
    } else {
        track_row_count = 0;
    }
    mode = MODE_TRACKS;
    force_full_redraw = true;
}

static bool play_track_row(int row)
{
    int index = track_index_at_row(row);
    struct rbprep_track_record track;
    char path[MAX_PATH];

    if (!read_track_record(index, &track) ||
        !read_index_string(track.path_offset, path, sizeof(path)))
        return false;
    if (!rb->file_exists(path)) {
        rb->splashf(HZ * 2, "Missing: %s", path);
        force_full_redraw = true;
        return false;
    }

    stop_editor_audio();
    if (rb->playlist_create(NULL, NULL) < 0 ||
        rb->playlist_insert_track(NULL, path, PLAYLIST_INSERT_LAST,
                                  false, true) < 0) {
        rb->splash(HZ * 2, "Could not load track");
        force_full_redraw = true;
        return false;
    }

    selected_track_index = index;
    selected_track_id = track.id;
    read_index_string(track.title_offset, selected_title,
                      sizeof(selected_title));
    read_index_string(track.artist_offset, selected_artist,
                      sizeof(selected_artist));
    read_index_string(track.genre_offset, selected_genre,
                      sizeof(selected_genre));
    grid_bpm_x100 = track.bpm_x100;
    rating = track.rating;
    color_index = track.color & 7;
    load_waveform(track.id);
    playhead = 0;
    rb->playlist_start(0, 0, 0);
    reset_play_clock(0, *rb->current_tick);
    deck_return_mode = MODE_TRACKS;
    mode = MODE_DECK;
    force_full_redraw = true;
    return true;
}

static void draw_playlist_browser(void)
{
    int row;
    char name[80];
    char line[96];

    text(7, 3, "PLAYLIST TREE", LCD_RGBPACK(70, 235, 125));
    if (tree_parent == RBPREP_ROOT_NODE) {
        text(112, 3, "/", LCD_LIGHTGRAY);
    } else {
        struct rbprep_node_record parent;
        if (read_node_record(tree_parent, &parent) &&
            read_index_string(parent.name_offset, name, sizeof(name)))
            text(112, 3, name, LCD_LIGHTGRAY);
    }

    for (row = 0; row < RBPREP_LIST_ROWS; row++) {
        int ordinal = tree_top + row;
        int node_index;
        int y = 24 + row * 20;
        struct rbprep_node_record node;

        if (ordinal >= tree_child_count)
            break;
        node_index = child_node_at(tree_parent, ordinal, &node);
        if (node_index < 0 ||
            !read_index_string(node.name_offset, name, sizeof(name)))
            continue;
        if (ordinal == tree_selection) {
            rb->lcd_set_foreground(LCD_RGBPACK(29, 102, 65));
            rb->lcd_fillrect(0, y - 2, LCD_WIDTH, 19);
        }
        rb->snprintf(line, sizeof(line), "%s %s",
                     node.kind == 0 ? "+" : ">", name);
        text(8, y + 2, line, LCD_WHITE);
        if (node.kind != 0) {
            rb->snprintf(line, sizeof(line), "%lu",
                         (unsigned long)node.member_count);
            text(280, y + 2, line, LCD_LIGHTGRAY);
        }
    }
    rb->lcd_set_foreground(LCD_RGBPACK(14, 24, 18));
    rb->lcd_fillrect(0, 210, LCD_WIDTH, 30);
    rb->snprintf(line, sizeof(line), "%d/%d", tree_selection + 1,
                 tree_child_count);
    text(7, 213, line, LCD_RGBPACK(70, 235, 125));
    text(70, 213, "WHEEL: BROWSE", LCD_LIGHTGRAY);
    text(7, 226, "SELECT: OPEN     MENU: BACK", LCD_WHITE);
}

static void draw_track_browser(void)
{
    int row;
    char title_buffer[96];
    char artist_buffer[72];
    char name[80];
    char line[96];

    text(7, 3, active_playlist_node < 0 ? "ALL TRACKS" : "PLAYLIST",
         LCD_RGBPACK(70, 235, 125));
    if (active_playlist_node >= 0) {
        struct rbprep_node_record node;
        if (read_node_record(active_playlist_node, &node) &&
            read_index_string(node.name_offset, name, sizeof(name)))
            text(75, 3, name, LCD_LIGHTGRAY);
    }

    for (row = 0; row < RBPREP_LIST_ROWS; row++) {
        int ordinal = track_top + row;
        int index;
        int y = 22 + row * 20;
        struct rbprep_track_record track;

        if (ordinal >= track_row_count)
            break;
        index = track_index_at_row(ordinal);
        if (!read_track_record(index, &track))
            continue;
        read_index_string(track.title_offset, title_buffer,
                          sizeof(title_buffer));
        read_index_string(track.artist_offset, artist_buffer,
                          sizeof(artist_buffer));
        if (ordinal == track_selection) {
            rb->lcd_set_foreground(LCD_RGBPACK(25, 91, 148));
            rb->lcd_fillrect(0, y - 1, LCD_WIDTH, 20);
        }
        rb->snprintf(line, sizeof(line), "%.41s", title_buffer);
        text(7, y, line, LCD_WHITE);
        rb->snprintf(line, sizeof(line), "%.38s", artist_buffer);
        text(14, y + 10, line, LCD_RGBPACK(145, 165, 151));
        rb->snprintf(line, sizeof(line), "%d.%02d",
                     track.bpm_x100 / 100, track.bpm_x100 % 100);
        text(276, y + 10, line, LCD_RGBPACK(95, 225, 145));
    }
    rb->lcd_set_foreground(LCD_RGBPACK(14, 24, 18));
    rb->lcd_fillrect(0, 210, LCD_WIDTH, 30);
    rb->snprintf(line, sizeof(line), "%d/%d", track_selection + 1,
                 track_row_count);
    text(7, 213, line, LCD_RGBPACK(70, 235, 125));
    text(78, 213, "WHEEL: BROWSE", LCD_LIGHTGRAY);
    text(7, 226, "SELECT: LOAD + PLAY     MENU: BACK", LCD_WHITE);
}

static void draw_hud(void)
{
    static const char *labels[] = { "PLY", "CUE", "GRD", "TAG" };
    static const enum rbprep_mode modes[] = {
        MODE_DECK, MODE_CUES, MODE_GRID, MODE_METADATA
    };
    int i;

    rb->lcd_set_foreground(LCD_RGBPACK(10, 17, 13));
    rb->lcd_fillrect(0, RBPREP_WAVE_TOP, RBPREP_HUD_WIDTH,
                     RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP + 1);
    for (i = 0; i < 4; i++) {
        int y = 29 + i * 35;
        if (mode == modes[i]) {
            rb->lcd_set_foreground(LCD_RGBPACK(36, 126, 76));
            rb->lcd_fillrect(2, y - 5, 36, 25);
            text(8, y + 1, labels[i], LCD_WHITE);
        } else {
            text(8, y + 1, labels[i], LCD_RGBPACK(105, 125, 112));
        }
    }
    rb->lcd_set_foreground(quantize ? LCD_RGBPACK(65, 225, 115)
                                    : LCD_RGBPACK(75, 85, 78));
    rb->lcd_drawrect(4, 174, 32, 22);
    text(8, 180, quantize ? "Q ON" : "Q --",
         quantize ? LCD_RGBPACK(65, 225, 115) : LCD_RGBPACK(105, 115, 108));
    rb->lcd_set_foreground(LCD_RGBPACK(42, 62, 49));
    rb->lcd_vline(RBPREP_HUD_WIDTH, RBPREP_WAVE_TOP,
                  RBPREP_WAVE_BOTTOM);
}

static void draw_screen(void)
{
    char line[80];

    rb->lcd_set_background(LCD_RGBPACK(6, 10, 7));
    rb->lcd_clear_display();
    rb->lcd_set_drawmode(DRMODE_SOLID);
    rb->lcd_set_foreground(LCD_RGBPACK(18, 27, 20));
    rb->lcd_fillrect(0, 0, LCD_WIDTH, RBPREP_WAVE_TOP);
    if (mode == MODE_PLAYLISTS) {
        draw_playlist_browser();
        rb->lcd_update();
        force_full_redraw = false;
        return;
    }
    if (mode == MODE_TRACKS) {
        draw_track_browser();
        rb->lcd_update();
        force_full_redraw = false;
        return;
    }
    text(3, 3, "RB", LCD_RGBPACK(70, 235, 125));
    text(mode == MODE_LIBRARY ? 23 : RBPREP_DECK_X, 3,
         mode_names[mode], LCD_WHITE);
    rb->snprintf(line, sizeof(line), "Q %s", quantize ? "ON" : "OFF");
    text(281, 3, line, quantize ? LCD_RGBPACK(70, 235, 125)
                                : LCD_LIGHTGRAY);
    if (mode >= MODE_DECK && selected_title[0]) {
        rb->snprintf(line, sizeof(line), "%.25s", selected_title);
        text(112, 3, line, LCD_RGBPACK(155, 175, 160));
    }

    if (mode == MODE_LIBRARY) {
        const char *items[] = {
            "LIBRARY", "PLAYLISTS", "PREP DECK", "PENDING EDITS",
            "INDEX STATUS"
        };
        int i;
        if (library_fd >= 0)
            rb->snprintf(line, sizeof(line), "%lu TRACKS  INDEX ONLINE",
                         (unsigned long)library_track_count);
        else
            rb->snprintf(line, sizeof(line), "RBPREP INDEX NOT FOUND");
        text(10, 32, line, library_fd >= 0
             ? LCD_RGBPACK(70, 235, 125) : LCD_RGBPACK(255, 90, 70));
        for (i = 0; i < 5; i++) {
            if (i == selection) {
                rb->lcd_set_foreground(LCD_RGBPACK(25, 105, 175));
                rb->lcd_fillrect(0, 58 + i * 27, LCD_WIDTH, 25);
            }
            text(10, 64 + i * 27, items[i], LCD_WHITE);
        }
    } else {
        int bpm_whole = grid_bpm_x100 / 100;
        int bpm_fraction = grid_bpm_x100 % 100;
        draw_hud();
        draw_waveform();
        rb->lcd_set_foreground(LCD_RGBPACK(10, 16, 12));
        rb->lcd_fillrect(0, RBPREP_WAVE_BOTTOM + 1, LCD_WIDTH,
                         LCD_HEIGHT - RBPREP_WAVE_BOTTOM - 1);
        rb->snprintf(line, sizeof(line),
                     "%02d:%02d.%03d  %d.%02d BPM  Z%dx  %dms",
                     playhead / 60000, (playhead / 1000) % 60,
                     playhead % 1000, bpm_whole, bpm_fraction,
                     zoom, scrub_step_ms());
        text(3, 211, line, LCD_WHITE);

        if (mode == MODE_DECK) {
            if (deck_cue >= 0)
                rb->snprintf(line, sizeof(line),
                             "CUE %02d:%02d.%03d  SEL: FIRE HOLD: SET",
                             deck_cue / 60000, (deck_cue / 1000) % 60,
                             deck_cue % 1000);
            else
                rb->snprintf(line, sizeof(line),
                             "SEL: PREVIEW  HOLD SEL: SET");
            text(3, 225, line, LCD_RGBPACK(255, 200, 70));
        } else if (mode == MODE_CUES) {
            rb->snprintf(line, sizeof(line),
                         "HC %02d %s  SEL: %s  HOLD: SET",
                         cue_slot + 1,
                         hotcues[cue_slot] >= 0 ? "SET" : "EMPTY",
                         hotcues[cue_slot] >= 0 ? "FIRE" : "PREVIEW");
            text(3, 225, line, cue_palette[hotcue_colors[cue_slot] & 7]);
        } else if (mode == MODE_GRID) {
            rb->snprintf(line, sizeof(line),
                         "GRID %dPTS %+dms  SEL:Q HOLD:ORIGIN",
                         beat_count, grid_offset);
            text(3, 225, line, LCD_RGBPACK(80, 210, 255));
        } else {
            rb->snprintf(line, sizeof(line),
                         "RATING %d/5  COLOR %s",
                         rating, color_labels[color_index]);
            text(3, 225, line, LCD_RGBPACK(255, 190, 65));
        }
    }
    if (force_full_redraw || mode == MODE_LIBRARY) {
        rb->lcd_update();
        force_full_redraw = false;
    } else {
        rb->lcd_update_rect(RBPREP_DECK_X, RBPREP_WAVE_TOP,
                            RBPREP_DECK_WIDTH,
                            RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP + 1);
        rb->lcd_update_rect(0, RBPREP_WAVE_BOTTOM + 1, LCD_WIDTH,
                            LCD_HEIGHT - RBPREP_WAVE_BOTTOM - 1);
    }
}

static void reset_play_clock(int anchor, long tick)
{
    play_clock_anchor = clamp_playhead(anchor);
    play_clock_tick = tick;
}

static bool update_play_clock(void)
{
    int status = rb->audio_status();
    int position;

    if (seek_state != SEEK_IDLE || !(status & AUDIO_STATUS_PLAY) ||
        (status & AUDIO_STATUS_PAUSE))
        return false;
    position = clamp_playhead(play_clock_anchor +
        (long long)(*rb->current_tick - play_clock_tick) * 1000 / HZ);
    if (position == playhead)
        return false;
    playhead = position;
    return true;
}

static void stop_editor_audio(void)
{
    if (seek_state == SEEK_IDLE)
        return;
    if (seek_state == SEEK_PREVIEW)
        rb->audio_pause();
    rb->audio_ff_rewind(playhead);
    seek_state = SEEK_IDLE;
    reset_play_clock(playhead, *rb->current_tick);
}

static void request_audio_seek(bool preview)
{
    int status = rb->audio_status();
    bool original_pause;

    if (!(status & AUDIO_STATUS_PLAY))
        return;
    original_pause = seek_state == SEEK_IDLE
                   ? !!(status & AUDIO_STATUS_PAUSE) : seek_was_paused;
    if (seek_state == SEEK_PREVIEW)
        rb->audio_pause();
    if (seek_state != SEEK_DEBOUNCE)
        rb->audio_pre_ff_rewind();
    seek_target = playhead;
    seek_preview = preview;
    seek_was_paused = original_pause;
    seek_state = SEEK_DEBOUNCE;
    seek_deadline = *rb->current_tick + RBPREP_SEEK_DEBOUNCE;
    reset_play_clock(playhead, *rb->current_tick);
}

static bool service_audio_seek(void)
{
    long now = *rb->current_tick;

    if (seek_state == SEEK_IDLE ||
        (TIME_BEFORE(now, seek_deadline) && now != seek_deadline))
        return false;

    if (seek_state == SEEK_DEBOUNCE) {
        rb->audio_ff_rewind(seek_target);
        seek_applied_tick = now;
        seek_state = SEEK_SETTLE;
        seek_deadline = now + RBPREP_SEEK_SETTLE;
    } else if (seek_state == SEEK_SETTLE) {
        if (seek_preview && seek_was_paused) {
            rb->audio_resume();
            seek_state = SEEK_PREVIEW;
            seek_applied_tick = now;
            seek_deadline = now + RBPREP_PREVIEW_TICKS;
        } else {
            seek_state = SEEK_IDLE;
            reset_play_clock(seek_target, seek_applied_tick);
        }
    } else {
        rb->audio_pause();
        rb->audio_pre_ff_rewind();
        rb->audio_ff_rewind(seek_target);
        playhead = seek_target;
        seek_state = SEEK_IDLE;
        reset_play_clock(playhead, now);
    }
    return true;
}

static void audition_playhead(void)
{
    request_audio_seek(true);
}

static void seek_by(int delta, bool audition)
{
    playhead = clamp_playhead(playhead + delta);
    if (audition)
        request_audio_seek(true);
}

static void toggle_playback(void)
{
    int status = rb->audio_status();

    if (seek_state == SEEK_PREVIEW) {
        playhead = clamp_playhead(seek_target +
            (long long)(*rb->current_tick - seek_applied_tick) * 1000 / HZ);
        seek_state = SEEK_IDLE;
        reset_play_clock(playhead, *rb->current_tick);
        return;
    }
    if (seek_state != SEEK_IDLE)
        stop_editor_audio();
    if (status & AUDIO_STATUS_PAUSE) {
        rb->audio_resume();
        reset_play_clock(playhead, *rb->current_tick);
    } else if (status & AUDIO_STATUS_PLAY) {
        update_play_clock();
        rb->audio_pause();
    } else if (rb->global_status->resume_index != -1 &&
               rb->playlist_resume() != -1) {
        rb->playlist_resume_track(rb->global_status->resume_index,
                                  rb->global_status->resume_crc32,
                                  rb->global_status->resume_elapsed,
                                  rb->global_status->resume_offset);
        playhead = rb->global_status->resume_elapsed;
        reset_play_clock(playhead, *rb->current_tick);
    } else {
        rb->splash(HZ, "No Rockbox track loaded");
    }
}

static enum rbprep_mode previous_mode(enum rbprep_mode current)
{
    if (current == MODE_DECK)
        return MODE_METADATA;
    return current - 1;
}

static enum rbprep_mode next_mode(enum rbprep_mode current)
{
    if (current == MODE_METADATA)
        return MODE_DECK;
    return current + 1;
}

static void short_select(void)
{
    if (mode == MODE_LIBRARY) {
        if (selection == 0 && library_fd >= 0) {
            open_track_browser(-1);
        } else if (selection == 1 && library_fd >= 0) {
            tree_selection = tree_top = 0;
            refresh_tree_children(RBPREP_ROOT_NODE);
            mode = MODE_PLAYLISTS;
            force_full_redraw = true;
        } else if (selection == 2 && selected_track_index >= 0) {
            deck_return_mode = MODE_LIBRARY;
            mode = MODE_DECK;
            force_full_redraw = true;
        } else if (selection <= 2) {
            rb->splash(HZ, "Choose a track first");
            force_full_redraw = true;
        }
    } else if (mode == MODE_PLAYLISTS) {
        struct rbprep_node_record node;
        int index = child_node_at(tree_parent, tree_selection, &node);
        if (index >= 0 && node.kind == 0) {
            tree_selection = tree_top = 0;
            refresh_tree_children(index);
            force_full_redraw = true;
        } else if (index >= 0) {
            open_track_browser(index);
        }
    } else if (mode == MODE_TRACKS) {
        if (track_row_count > 0)
            play_track_row(track_selection);
    } else if (mode == MODE_DECK) {
        if (deck_cue >= 0)
            playhead = deck_cue;
        audition_playhead();
    } else if (mode == MODE_CUES) {
        if (hotcues[cue_slot] >= 0)
            playhead = hotcues[cue_slot];
        audition_playhead();
    } else if (mode == MODE_GRID) {
        quantize = !quantize;
        force_full_redraw = true;
    } else if (mode == MODE_METADATA) {
        rating = (rating + 1) % 6;
    }
}

static void long_select(void)
{
    if (mode == MODE_DECK) {
        deck_cue = quantized_time(playhead);
        playhead = deck_cue;
    } else if (mode == MODE_CUES) {
        hotcues[cue_slot] = quantized_time(playhead);
        hotcue_colors[cue_slot] = color_index;
        playhead = hotcues[cue_slot];
    } else if (mode == MODE_GRID) {
        int index = nearest_beat_index(playhead);
        if (index >= 0) {
            grid_offset += playhead - (beat_times[index] + grid_offset);
            grid_beat_shift = (1 - beat_numbers[index]) & 3;
        } else {
            grid_phase_ms = playhead;
            grid_offset = 0;
        }
    } else if (mode == MODE_METADATA) {
        color_index = (color_index + 1) & 7;
    }
}

static void change_zoom(bool zoom_in)
{
    if (zoom_in)
        zoom = MIN(RBPREP_MAX_ZOOM, zoom * 2);
    else
        zoom = MAX(1, zoom / 2);
}

enum plugin_status plugin_start(const void *parameter)
{
    int button;
    int pressed = BUTTON_NONE;
    bool select_hold_fired = false;
    bool redraw = true;

    (void)parameter;
    rb->lcd_setfont(FONT_SYSFIXED);
    mode = MODE_LIBRARY;
    selection = 0;
    playhead = 0;
    zoom = 1;
    grid_offset = 0;
    grid_beat_shift = 0;
    cue_slot = 0;
    rating = 0;
    color_index = 0;
    quantize = true;
    seek_state = SEEK_IDLE;
    suppress_menu = suppress_play = false;
    suppress_left = suppress_right = false;
    force_full_redraw = true;
    selected_title[0] = selected_artist[0] = selected_genre[0] = '\0';
    selected_track_index = selected_track_id = -1;
    clear_analysis();
    open_library_index();
    reset_play_clock(playhead, *rb->current_tick);

    /* Discard the release of SELECT used to launch the plugin. */
    rb->button_clear_queue();
    while (true) {
        struct mp3entry *id3 = rb->audio_current_track();
        if (selected_track_id >= 0 && id3 && id3->length > 0)
            track_length = id3->length;
        if (service_audio_seek())
            redraw = true;
        if (update_play_clock())
            redraw = true;

        if (redraw) {
            draw_screen();
            redraw = false;
        }
        button = rb->button_get_w_tmo(HZ / 20);
        if (button != BUTTON_NONE)
            redraw = true;
        switch (button) {
        case BUTTON_MENU:
            if (!suppress_menu)
                pressed = button;
            break;
        case BUTTON_PLAY:
            if (!suppress_play)
                pressed = button;
            break;
        case BUTTON_SELECT:
            pressed = BUTTON_SELECT;
            select_hold_fired = false;
            break;
        case BUTTON_MENU | BUTTON_REL:
            if (suppress_menu) {
                suppress_menu = false;
                pressed = BUTTON_NONE;
                break;
            }
            if (pressed != BUTTON_MENU)
                break;
            pressed = BUTTON_NONE;
            stop_editor_audio();
            if (mode == MODE_LIBRARY) {
                if (library_fd >= 0)
                    rb->close(library_fd);
                return PLUGIN_OK;
            }
            if (mode == MODE_PLAYLISTS && tree_parent != RBPREP_ROOT_NODE) {
                struct rbprep_node_record parent;
                if (read_node_record(tree_parent, &parent))
                    refresh_tree_children(parent.parent);
                else
                    refresh_tree_children(RBPREP_ROOT_NODE);
                tree_selection = tree_top = 0;
            } else if (mode == MODE_TRACKS && active_playlist_node >= 0) {
                mode = MODE_PLAYLISTS;
            } else if (mode >= MODE_DECK &&
                       deck_return_mode == MODE_TRACKS) {
                mode = MODE_TRACKS;
            } else {
                mode = MODE_LIBRARY;
            }
            force_full_redraw = true;
            break;
        case BUTTON_PLAY | BUTTON_REL:
            if (suppress_play) {
                suppress_play = false;
                pressed = BUTTON_NONE;
                break;
            }
            if (pressed != BUTTON_PLAY)
                break;
            pressed = BUTTON_NONE;
            toggle_playback();
            break;
        case BUTTON_SELECT | BUTTON_REL:
            if (pressed != BUTTON_SELECT)
                break;
            pressed = BUTTON_NONE;
            if (!select_hold_fired)
                short_select();
            select_hold_fired = false;
            break;
        case BUTTON_SELECT | BUTTON_REPEAT:
            if (pressed == BUTTON_SELECT && !select_hold_fired) {
                select_hold_fired = true;
                long_select();
            }
            break;
        case BUTTON_SELECT | BUTTON_LEFT:
        case BUTTON_SELECT | BUTTON_LEFT | BUTTON_REPEAT:
            if (pressed == BUTTON_SELECT && !select_hold_fired &&
                mode >= MODE_DECK) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_left = true;
                mode = previous_mode(mode);
                force_full_redraw = true;
            }
            break;
        case BUTTON_SELECT | BUTTON_RIGHT:
        case BUTTON_SELECT | BUTTON_RIGHT | BUTTON_REPEAT:
            if (pressed == BUTTON_SELECT && !select_hold_fired &&
                mode >= MODE_DECK) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_right = true;
                mode = next_mode(mode);
                force_full_redraw = true;
            }
            break;
        case BUTTON_SELECT | BUTTON_MENU:
        case BUTTON_SELECT | BUTTON_MENU | BUTTON_REPEAT:
            if (pressed == BUTTON_SELECT && !select_hold_fired &&
                mode >= MODE_DECK) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_menu = true;
                change_zoom(false);
            }
            break;
        case BUTTON_SELECT | BUTTON_PLAY:
        case BUTTON_SELECT | BUTTON_PLAY | BUTTON_REPEAT:
            if (pressed == BUTTON_SELECT && !select_hold_fired &&
                mode >= MODE_DECK) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_play = true;
                change_zoom(true);
            }
            break;
        case BUTTON_SELECT | BUTTON_MENU | BUTTON_REL:
            suppress_menu = false;
            pressed = BUTTON_NONE;
            break;
        case BUTTON_SELECT | BUTTON_PLAY | BUTTON_REL:
            suppress_play = false;
            pressed = BUTTON_NONE;
            break;
        case BUTTON_SCROLL_FWD:
        case BUTTON_SCROLL_FWD | BUTTON_REPEAT:
            if (mode == MODE_LIBRARY)
                selection = MIN(4, selection + 1);
            else if (mode == MODE_PLAYLISTS && tree_child_count > 0) {
                tree_selection = MIN(tree_child_count - 1,
                                     tree_selection + 1);
                if (tree_selection >= tree_top + RBPREP_LIST_ROWS)
                    tree_top = tree_selection - RBPREP_LIST_ROWS + 1;
            } else if (mode == MODE_TRACKS && track_row_count > 0) {
                track_selection = MIN(track_row_count - 1,
                                      track_selection + 1);
                if (track_selection >= track_top + RBPREP_LIST_ROWS)
                    track_top = track_selection - RBPREP_LIST_ROWS + 1;
            }
            else
                seek_by(scrub_step_ms(), true);
            break;
        case BUTTON_SCROLL_BACK:
        case BUTTON_SCROLL_BACK | BUTTON_REPEAT:
            if (mode == MODE_LIBRARY)
                selection = MAX(0, selection - 1);
            else if (mode == MODE_PLAYLISTS) {
                tree_selection = MAX(0, tree_selection - 1);
                if (tree_selection < tree_top)
                    tree_top = tree_selection;
            } else if (mode == MODE_TRACKS) {
                track_selection = MAX(0, track_selection - 1);
                if (track_selection < track_top)
                    track_top = track_selection;
            }
            else
                seek_by(-scrub_step_ms(), true);
            break;
        case BUTTON_LEFT:
        case BUTTON_LEFT | BUTTON_REPEAT:
            if (suppress_left)
                break;
            if (mode == MODE_GRID)
                grid_offset--;
            else if (mode == MODE_CUES)
                cue_slot = (cue_slot + 15) & 15;
            else if (mode == MODE_METADATA)
                color_index = (color_index + 7) & 7;
            else if (mode >= MODE_DECK)
                seek_by(-1000, true);
            break;
        case BUTTON_LEFT | BUTTON_REL:
            suppress_left = false;
            break;
        case BUTTON_RIGHT:
        case BUTTON_RIGHT | BUTTON_REPEAT:
            if (suppress_right)
                break;
            if (mode == MODE_GRID)
                grid_offset++;
            else if (mode == MODE_CUES)
                cue_slot = (cue_slot + 1) & 15;
            else if (mode == MODE_METADATA)
                color_index = (color_index + 1) & 7;
            else if (mode >= MODE_DECK)
                seek_by(1000, true);
            break;
        case BUTTON_RIGHT | BUTTON_REL:
            suppress_right = false;
            break;
        default:
            if (rb->default_event_handler(button) == SYS_USB_CONNECTED) {
                stop_editor_audio();
                if (library_fd >= 0)
                    rb->close(library_fd);
                return PLUGIN_USB_CONNECTED;
            }
            break;
        }
    }
}
