#include "plugin.h"
#include "lib/configfile.h"
#include "lib/helper.h"
#include "lib/pluginlib_exit.h"
#include "lib/xlcd.h"

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
#define RBPREP_DECK_X 0
#define RBPREP_VU_WIDTH 20
#define RBPREP_DECK_WIDTH (LCD_WIDTH - RBPREP_VU_WIDTH - 1)
#define RBPREP_WAVE_TOP 70
#define RBPREP_WAVE_BOTTOM (LCD_HEIGHT - 1)
#define RBPREP_OVERVIEW_X 220
#define RBPREP_OVERVIEW_Y 1
#define RBPREP_OVERVIEW_WIDTH (LCD_WIDTH - RBPREP_OVERVIEW_X - 1)
#define RBPREP_OVERVIEW_HEIGHT 23
#define RBPREP_MAX_ZOOM 128
#define RBPREP_SEEK_DEBOUNCE MAX(1, HZ / 20)
#define RBPREP_SEEK_SETTLE MAX(1, HZ / 10)
#define RBPREP_PREVIEW_TICKS MAX(1, HZ / 5)
#define RBPREP_OVERVIEW_TICKS MAX(1, HZ / 2)
#define RBPREP_FRAME_TICKS MAX(1, HZ / 25)
#define RBPREP_CONFIG_VERSION 1
#define RBPREP_CONFIG_FILE "/.rockbox/rbprep/rbprep.cfg"

enum rbprep_mode {
    MODE_LIBRARY,
    MODE_PLAYLISTS,
    MODE_TRACKS,
    MODE_SETTINGS,
    MODE_DECK,
    MODE_CUES,
    MODE_GRID,
    MODE_METADATA
};

enum rbprep_tool {
    TOOL_SEEK,
    TOOL_ZOOM,
    TOOL_GAIN,
    TOOL_CUE_SLOT,
    TOOL_CUE_MOVE,
    TOOL_CUE_COLOR,
    TOOL_CUE_DELETE,
    TOOL_GRID_NUDGE,
    TOOL_GRID_BPM,
    TOOL_GRID_ORIGIN,
    TOOL_GRID_QUANTIZE,
    TOOL_META_RATING,
    TOOL_META_COLOR,
    TOOL_META_YEAR,
    TOOL_META_GENRE
};

static enum rbprep_mode mode;
static int selection;
static int playhead;
static unsigned char waveform[RBPREP_POINTS][4];
static unsigned char overview_waveform[RBPREP_OVERVIEW_WIDTH][4];
static unsigned char waveform_height_lut[256];
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
static int track_year;
static int track_length = 240000;
static bool quantize = true;
static bool force_full_redraw = true;
static bool overview_dirty = true;
static long overview_deadline;
static int deck_tool;
static int cue_tool;
static int grid_tool;
static int metadata_tool;
static int waveform_half;
static int deleted_cue_slot = -1;
static int deleted_cue_time;
static unsigned char deleted_cue_color;
static uint32_t vu_left;
static uint32_t vu_right;
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
static int seek_applied_target;
static long seek_deadline;
static long seek_applied_tick;
static int play_clock_anchor;
static long play_clock_tick;
static int reported_audio_elapsed = -1;
static long audio_sync_after;
static bool overview_playhead_white;
static bool display_locked;

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

static void rbprep_cleanup(void)
{
    backlight_use_settings();
}

static bool service_display_lock(void)
{
#ifdef HAS_BUTTON_HOLD
    bool locked = rb->button_hold();

    if (locked == display_locked)
        return false;
    display_locked = locked;
    if (locked) {
        backlight_use_settings();
    } else {
        backlight_ignore_timeout();
#ifdef HAVE_BACKLIGHT
        rb->backlight_on();
#endif
    }
    force_full_redraw = true;
    return true;
#else
    return false;
#endif
}

static const char *color_labels[] = {
    "SAMPLE", "OPENER", "BUILDER", "PIVOTER",
    "MAINTAINER", "PEAK", "RESET", "TOOL"
};

static const char *mode_names[] = {
    "LIBRARY", "PLAYLISTS", "TRACKS", "SETTINGS", "PLAYBACK",
    "HOT CUES", "BEATGRID", "METADATA"
};

static char *waveform_styles[] = { "full", "half" };

static const struct configdata rbprep_config[] = {
    { TYPE_ENUM, 0, 1, { .int_p = &waveform_half },
      "waveform style", waveform_styles }
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

static void rebuild_waveform_height_lut(void)
{
    int amplitude;
    int height = waveform_half
               ? RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP - 2
               : (RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP) / 2 - 2;

    for (amplitude = 0; amplitude < 256; amplitude++)
        waveform_height_lut[amplitude] = 2 + amplitude * height / 255;
}

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

static int current_beat_index(int time_ms)
{
    int low = 0;
    int high = beat_count;
    int target = time_ms - grid_offset;

    if (beat_count <= 0 || target < beat_times[0])
        return -1;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (beat_times[middle] <= target)
            low = middle + 1;
        else
            high = middle;
    }
    return low - 1;
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
            if (x >= RBPREP_DECK_X &&
                x < RBPREP_DECK_X + RBPREP_DECK_WIDTH) {
                rb->lcd_set_foreground(number == 1
                    ? LCD_RGBPACK(255, 45, 45)
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
        if (x >= RBPREP_DECK_X &&
            x < RBPREP_DECK_X + RBPREP_DECK_WIDTH) {
            rb->lcd_set_foreground((i & 3) == 0
                ? LCD_RGBPACK(255, 45, 45)
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
        if (x < RBPREP_DECK_X ||
            x >= RBPREP_DECK_X + RBPREP_DECK_WIDTH)
            continue;

        color = cue_palette[hotcue_colors[i] & 7];
        box_x = MAX(RBPREP_DECK_X,
                    MIN(RBPREP_DECK_X + RBPREP_DECK_WIDTH - 11, x - 5));
        box_y = RBPREP_WAVE_TOP + 3 + (i & 1) * 12;
        rb->lcd_set_foreground(color);
        rb->lcd_fillrect(box_x, box_y, 11, 11);
        /* The stem is registered to the cue's exact time, at box center. */
        rb->lcd_vline(x, box_y + 10, RBPREP_WAVE_BOTTOM);
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
            if (waveform_half)
                rb->lcd_vline(screen_x, RBPREP_WAVE_BOTTOM - 5,
                              RBPREP_WAVE_BOTTOM - 2);
            else
                rb->lcd_vline(screen_x, mid - 3, mid + 3);
            continue;
        }

        begin = first + (long long)x * span / RBPREP_DECK_WIDTH;
        end = first + (long long)(x + 1) * span / RBPREP_DECK_WIDTH;
        if (end <= 0 || begin >= waveform_points) {
            rb->lcd_set_foreground(LCD_RGBPACK(15, 23, 17));
            if (waveform_half)
                rb->lcd_vline(screen_x, RBPREP_WAVE_BOTTOM - 3,
                              RBPREP_WAVE_BOTTOM - 2);
            else
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

        height = waveform_height_lut[peak];
        color = LCD_RGBPACK(waveform[peak_index][1],
                            waveform[peak_index][2],
                            waveform[peak_index][3]);
        rb->lcd_set_foreground(color);
        if (waveform_half)
            rb->lcd_vline(screen_x, RBPREP_WAVE_BOTTOM - height,
                          RBPREP_WAVE_BOTTOM - 1);
        else
            rb->lcd_vline(screen_x, mid - height, mid + height);
    }

    draw_beatgrid(first, span);
    draw_cues(first, span);
    rb->lcd_set_foreground(LCD_RGBPACK(255, 45, 45));
    x = MAX(RBPREP_DECK_X,
            MIN(RBPREP_DECK_X + RBPREP_DECK_WIDTH - 1,
                   time_to_x(playhead, first, span)));
    rb->lcd_vline(x, RBPREP_WAVE_TOP, RBPREP_WAVE_BOTTOM);
}

static void rebuild_overview_waveform(void)
{
    int x;

    rb->memset(overview_waveform, 0, sizeof(overview_waveform));
    if (waveform_points <= 0)
        return;

    for (x = 0; x < RBPREP_OVERVIEW_WIDTH; x++) {
        int begin = (long long)x * waveform_points /
                    RBPREP_OVERVIEW_WIDTH;
        int end = (long long)(x + 1) * waveform_points /
                  RBPREP_OVERVIEW_WIDTH;
        int index;
        int peak_index = MIN(begin, waveform_points - 1);

        if (end <= begin)
            end = begin + 1;
        end = MIN(end, waveform_points);
        for (index = begin + 1; index < end; index++) {
            if (waveform[index][0] > waveform[peak_index][0])
                peak_index = index;
        }
        rb->memcpy(overview_waveform[x], waveform[peak_index], 4);
    }
}

static void draw_overview_waveform(void)
{
    int x;
    int mid = RBPREP_OVERVIEW_Y + RBPREP_OVERVIEW_HEIGHT / 2;
    int max_height = RBPREP_OVERVIEW_HEIGHT / 2 - 2;

    rb->lcd_set_foreground(LCD_RGBPACK(7, 14, 10));
    rb->lcd_fillrect(RBPREP_OVERVIEW_X, RBPREP_OVERVIEW_Y,
                     RBPREP_OVERVIEW_WIDTH, RBPREP_OVERVIEW_HEIGHT);
    for (x = 0; x < RBPREP_OVERVIEW_WIDTH; x++) {
        int height = 1 + overview_waveform[x][0] * max_height / 255;
        int color = waveform_points > 0
                  ? LCD_RGBPACK(overview_waveform[x][1],
                                overview_waveform[x][2],
                                overview_waveform[x][3])
                  : LCD_DARKGRAY;
        rb->lcd_set_foreground(color);
        rb->lcd_vline(RBPREP_OVERVIEW_X + x, mid - height,
                      mid + height);
    }

    for (x = 0; x < 16; x++) {
        int cue_x;
        if (hotcues[x] < 0)
            continue;
        cue_x = RBPREP_OVERVIEW_X +
                (long long)hotcues[x] * (RBPREP_OVERVIEW_WIDTH - 1) /
                MAX(1, track_length);
        rb->lcd_set_foreground(cue_palette[hotcue_colors[x] & 7]);
        rb->lcd_vline(cue_x, RBPREP_OVERVIEW_Y,
                      RBPREP_OVERVIEW_Y + 3);
    }

    x = RBPREP_OVERVIEW_X +
        (long long)playhead * (RBPREP_OVERVIEW_WIDTH - 1) /
        MAX(1, track_length);
    rb->lcd_set_foreground(overview_playhead_white
                           ? LCD_RGBPACK(205, 225, 212)
                           : LCD_RGBPACK(105, 125, 112));
    if (x > RBPREP_OVERVIEW_X)
        rb->lcd_vline(x - 1, RBPREP_OVERVIEW_Y + 1,
                      RBPREP_OVERVIEW_Y + RBPREP_OVERVIEW_HEIGHT - 2);
    if (x < RBPREP_OVERVIEW_X + RBPREP_OVERVIEW_WIDTH - 1)
        rb->lcd_vline(x + 1, RBPREP_OVERVIEW_Y + 1,
                      RBPREP_OVERVIEW_Y + RBPREP_OVERVIEW_HEIGHT - 2);
    rb->lcd_set_foreground(overview_playhead_white ? LCD_WHITE : LCD_BLACK);
    rb->lcd_vline(x, RBPREP_OVERVIEW_Y,
                  RBPREP_OVERVIEW_Y + RBPREP_OVERVIEW_HEIGHT - 1);
    rb->lcd_set_foreground(LCD_RGBPACK(55, 75, 62));
    rb->lcd_drawrect(RBPREP_OVERVIEW_X, RBPREP_OVERVIEW_Y,
                     RBPREP_OVERVIEW_WIDTH, RBPREP_OVERVIEW_HEIGHT);
}

static void update_vu_levels(void)
{
    static struct pcm_peaks peaks;

    rb->mixer_channel_calculate_peaks(PCM_MIXER_CHAN_PLAYBACK, &peaks);
    vu_left = MAX(peaks.left, vu_left * 7 / 8);
    vu_right = MAX(peaks.right, vu_right * 7 / 8);
}

static void draw_vu_meter(void)
{
    const int segments = 14;
    const int gap = 2;
    int segment_height =
        (RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP + 1 -
         (segments - 1) * gap) / segments;
    int lit_left = MIN(segments, (int)(vu_left * segments / 32768));
    int lit_right = MIN(segments, (int)(vu_right * segments / 32768));
    int segment;
    int x = RBPREP_DECK_X + RBPREP_DECK_WIDTH + 2;

    rb->lcd_set_foreground(LCD_RGBPACK(6, 13, 9));
    rb->lcd_fillrect(RBPREP_DECK_X + RBPREP_DECK_WIDTH + 1,
                     RBPREP_WAVE_TOP, RBPREP_VU_WIDTH,
                     RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP + 1);
    for (segment = 0; segment < segments; segment++) {
        int y = RBPREP_WAVE_BOTTOM -
                (segment + 1) * segment_height - segment * gap + 1;
        int active_color = segment >= 12 ? LCD_RGBPACK(255, 65, 55)
                         : segment >= 9 ? LCD_RGBPACK(255, 205, 55)
                         : LCD_RGBPACK(55, 225, 105);
        rb->lcd_set_foreground(segment < lit_left
                              ? active_color : LCD_RGBPACK(28, 39, 31));
        rb->lcd_fillrect(x, y, 7, segment_height);
        rb->lcd_set_foreground(segment < lit_right
                              ? active_color : LCD_RGBPACK(28, 39, 31));
        rb->lcd_fillrect(x + 9, y, 7, segment_height);
    }
}

static void clear_analysis(void)
{
    int i;

    waveform_points = 0;
    rb->memset(overview_waveform, 0, sizeof(overview_waveform));
    waveform_duration_ms = 1;
    beat_count = 0;
    grid_offset = 0;
    grid_beat_shift = 0;
    deck_cue = -1;
    deleted_cue_slot = -1;
    for (i = 0; i < 16; i++) {
        hotcues[i] = -1;
        hotcue_colors[i] = 3;
    }
    overview_dirty = true;
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

    rebuild_overview_waveform();

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
    overview_dirty = true;
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
    track_year = 0;
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

static enum rbprep_tool active_tool(void)
{
    if (mode == MODE_DECK)
        return TOOL_SEEK + deck_tool;
    if (mode == MODE_CUES)
        return TOOL_CUE_SLOT + cue_tool;
    if (mode == MODE_GRID)
        return TOOL_GRID_NUDGE + grid_tool;
    return TOOL_META_RATING + metadata_tool;
}

static int tool_count(void)
{
    if (mode == MODE_DECK)
        return 3;
    if (mode == MODE_CUES || mode == MODE_GRID)
        return 4;
    return 4;
}

static int *tool_selection(void)
{
    if (mode == MODE_DECK)
        return &deck_tool;
    if (mode == MODE_CUES)
        return &cue_tool;
    if (mode == MODE_GRID)
        return &grid_tool;
    return &metadata_tool;
}

static const char *tool_icon(enum rbprep_tool tool)
{
    static const char *icons[] = {
        "<>", "+", "G", "#", "M", "C", "X", "N", "B", "1",
        "Q", "*", "C", "Y", "T"
    };
    return icons[tool];
}

static const char *page_label(void)
{
    if (mode == MODE_DECK) return "PLAY";
    if (mode == MODE_CUES) return "CUES";
    if (mode == MODE_GRID) return "GRID";
    return "META";
}

static void draw_beat_phase(void)
{
    int beat = 1;
    int bar = 1;
    int index = current_beat_index(playhead);
    int i;
    char counter[12];

    if (index >= 0) {
        beat = adjusted_beat_number(index);
        bar = (index + adjusted_beat_number(0) - 1) / 4 + 1;
    } else if (beat_count <= 0) {
        int ordinal = (playhead - grid_phase_ms - grid_offset) /
                      beat_period_ms();
        if (ordinal >= 0) {
            beat = (ordinal & 3) + 1;
            bar = ordinal / 4 + 1;
        }
    }

    rb->lcd_set_foreground(LCD_RGBPACK(12, 20, 15));
    rb->lcd_fillrect(172, 1, 45, 22);
    for (i = 0; i < 4; i++) {
        int x = 173 + i * 11;
        rb->lcd_set_foreground(i + 1 == beat
                              ? LCD_RGBPACK(70, 235, 125)
                              : LCD_RGBPACK(35, 53, 42));
        rb->lcd_fillrect(x, 4, 9, 9);
        if (i + 1 == beat) {
            char number[2] = { '1' + i, '\0' };
            text(x + 2, 3, number, LCD_BLACK);
        }
    }
    rb->snprintf(counter, sizeof(counter), "%03d.%d", bar, beat);
    text(178, 14, counter, LCD_RGBPACK(135, 158, 142));
}

static void draw_rating_stars(int x, int y)
{
    int star;

    for (star = 0; star < 5; star++) {
        int sx = x + star * 8;
        rb->lcd_set_foreground(star < rating
                              ? LCD_RGBPACK(255, 205, 55)
                              : LCD_RGBPACK(55, 68, 59));
        rb->lcd_drawpixel(sx + 3, y);
        rb->lcd_hline(sx + 2, sx + 4, y + 1);
        rb->lcd_hline(sx, sx + 6, y + 2);
        rb->lcd_hline(sx + 1, sx + 5, y + 3);
        rb->lcd_drawpixel(sx + 1, y + 4);
        rb->lcd_drawpixel(sx + 3, y + 4);
        rb->lcd_drawpixel(sx + 5, y + 4);
    }
}

static void draw_metadata_line(void)
{
    char line[96];

    rb->lcd_set_foreground(LCD_RGBPACK(12, 20, 15));
    rb->lcd_fillrect(0, 24, LCD_WIDTH, 13);
    rb->snprintf(line, sizeof(line), "%02d:%02d.%03d",
                 playhead / 60000, (playhead / 1000) % 60,
                 playhead % 1000);
    text(2, 25, line, LCD_WHITE);
    rb->snprintf(line, sizeof(line), "%d.%02d BPM",
                 grid_bpm_x100 / 100, grid_bpm_x100 % 100);
    text(79, 25, line, LCD_RGBPACK(85, 220, 255));
    draw_rating_stars(155, 27);
    rb->snprintf(line, sizeof(line), "%.9s", color_labels[color_index]);
    text(199, 25, line, cue_palette[color_index & 7]);
    text(286, 25, quantize ? "Q ON" : "Q --",
         quantize ? LCD_RGBPACK(70, 235, 125) : LCD_RGBPACK(105, 115, 108));
}

static void draw_tool_orbs(void)
{
    int count = tool_count();
    int selected = *tool_selection();
    int i;

    rb->lcd_set_foreground(LCD_RGBPACK(8, 15, 11));
    rb->lcd_fillrect(0, 37, LCD_WIDTH, 21);
    text(3, 42, page_label(), LCD_RGBPACK(125, 148, 132));
    for (i = 0; i < count; i++) {
        int cx = 52 + i * 25;
        int color = i == selected ? LCD_RGBPACK(70, 235, 125)
                                  : LCD_RGBPACK(48, 70, 56);
        rb->lcd_set_foreground(color);
        xlcd_fillcircle(cx, 47, i == selected ? 9 : 8);
        text(cx - (rb->strlen(tool_icon(active_tool() - selected + i)) > 1
                   ? 6 : 3), 42,
             tool_icon(active_tool() - selected + i),
             i == selected ? LCD_BLACK : LCD_RGBPACK(195, 215, 201));
    }
    text(180, 42, "SEL+<> TOOL", LCD_RGBPACK(82, 103, 89));
    text(252, 42, "SEL+UD PAGE", LCD_RGBPACK(82, 103, 89));
}

static void draw_tool_status(void)
{
    char line[96];
    enum rbprep_tool tool = active_tool();
    int color = LCD_RGBPACK(70, 235, 125);

    rb->lcd_set_foreground(LCD_RGBPACK(10, 17, 13));
    rb->lcd_fillrect(0, 58, LCD_WIDTH, 12);
    if (tool == TOOL_SEEK)
        rb->snprintf(line, sizeof(line), "SEEK  %dms/tick   SELECT: AUDITION",
                     scrub_step_ms());
    else if (tool == TOOL_ZOOM)
        rb->snprintf(line, sizeof(line), "ZOOM  %dx   WHEEL: WRAP 1-128x", zoom);
    else if (tool == TOOL_GAIN)
        rb->snprintf(line, sizeof(line), "GAIN  %d %s",
                     rb->sound_val2phys(SOUND_VOLUME,
                                        rb->global_status->volume),
                     rb->sound_unit(SOUND_VOLUME));
    else if (tool == TOOL_CUE_SLOT)
        rb->snprintf(line, sizeof(line),
                     "CUE %02d %s  SEL:AUDITION  HOLD:%s",
                     cue_slot + 1,
                     hotcues[cue_slot] >= 0 ? "SET" : "EMPTY",
                     hotcues[cue_slot] >= 0 ? "REPLACE" : "CREATE");
    else if (tool == TOOL_CUE_MOVE)
        rb->snprintf(line, sizeof(line), "MOVE CUE %02d   HOLD SELECT: SET",
                     cue_slot + 1);
    else if (tool == TOOL_CUE_COLOR)
        rb->snprintf(line, sizeof(line), "CUE COLOR  %s",
                     color_labels[hotcue_colors[cue_slot] & 7]);
    else if (tool == TOOL_CUE_DELETE) {
        if (deleted_cue_slot >= 0)
            rb->snprintf(line, sizeof(line), "CUE %02d DELETED   SELECT: UNDO",
                         deleted_cue_slot + 1);
        else
            rb->snprintf(line, sizeof(line), "DELETE CUE %02d   HOLD SELECT",
                         cue_slot + 1);
        color = LCD_RGBPACK(255, 90, 70);
    } else if (tool == TOOL_GRID_NUDGE)
        rb->snprintf(line, sizeof(line), "GRID NUDGE  %+dms", grid_offset);
    else if (tool == TOOL_GRID_BPM)
        rb->snprintf(line, sizeof(line), "BPM  %d.%02d   WHEEL: 0.01",
                     grid_bpm_x100 / 100, grid_bpm_x100 % 100);
    else if (tool == TOOL_GRID_ORIGIN)
        rb->snprintf(line, sizeof(line), "DOWNBEAT   HOLD SELECT: SET HERE");
    else if (tool == TOOL_GRID_QUANTIZE)
        rb->snprintf(line, sizeof(line), "QUANTIZE  %s   SELECT: TOGGLE",
                     quantize ? "ON" : "OFF");
    else if (tool == TOOL_META_RATING)
        rb->snprintf(line, sizeof(line), "RATING  %d/5", rating);
    else if (tool == TOOL_META_COLOR)
        rb->snprintf(line, sizeof(line), "COLOR  %s", color_labels[color_index]);
    else if (tool == TOOL_META_YEAR)
        rb->snprintf(line, sizeof(line), "YEAR  %04d", track_year);
    else
        rb->snprintf(line, sizeof(line), "GENRE  %.30s", selected_genre);
    text(3, 59, line, color);
}

static void draw_top_hud(void)
{
    char line[64];

    rb->lcd_set_foreground(LCD_RGBPACK(12, 20, 15));
    rb->lcd_fillrect(0, 0, LCD_WIDTH, RBPREP_WAVE_TOP);
    rb->snprintf(line, sizeof(line), "%.21s", selected_title[0]
                 ? selected_title : "RBPREP");
    text(2, 2, line, LCD_WHITE);
    rb->snprintf(line, sizeof(line), "%.21s", selected_artist[0]
                 ? selected_artist : mode_names[mode]);
    text(2, 13, line, LCD_RGBPACK(145, 165, 151));
    if (track_year > 0) {
        rb->snprintf(line, sizeof(line), "%04d", track_year);
        text(145, 13, line, LCD_LIGHTGRAY);
    }
    draw_beat_phase();
    draw_overview_waveform();
    draw_metadata_line();
    draw_tool_orbs();
    draw_tool_status();
}

static void draw_settings(void)
{
    char line[64];

    text(7, 3, "RBPREP SETTINGS", LCD_RGBPACK(70, 235, 125));
    rb->lcd_set_foreground(LCD_RGBPACK(25, 105, 175));
    rb->lcd_fillrect(0, 42, LCD_WIDTH, 27);
    text(10, 49, "WAVEFORM", LCD_WHITE);
    rb->snprintf(line, sizeof(line), "%s", waveform_half ? "HALF" : "FULL");
    text(260, 49, line, LCD_RGBPACK(70, 235, 125));
    text(10, 84, waveform_half
         ? "One-sided: maximum vertical definition"
         : "Mirrored: traditional full waveform",
         LCD_RGBPACK(145, 165, 151));
    text(10, 216, "WHEEL/SELECT: CHANGE     MENU: BACK", LCD_WHITE);
}

static void draw_screen(void)
{
    char line[80];

    rb->lcd_set_background(LCD_RGBPACK(6, 10, 7));
    rb->lcd_set_drawmode(DRMODE_SOLID);
    if (mode == MODE_PLAYLISTS || mode == MODE_TRACKS ||
        mode == MODE_LIBRARY || mode == MODE_SETTINGS) {
        rb->lcd_clear_display();
        if (mode == MODE_PLAYLISTS)
            draw_playlist_browser();
        else if (mode == MODE_TRACKS)
            draw_track_browser();
        else if (mode == MODE_SETTINGS)
            draw_settings();
        else {
            const char *items[] = {
                "LIBRARY", "PLAYLISTS", "PREP DECK", "SETTINGS",
                "PENDING EDITS", "INDEX STATUS"
            };
            int i;
            text(3, 3, "RB", LCD_RGBPACK(70, 235, 125));
            text(23, 3, "LIBRARY", LCD_WHITE);
            if (library_fd >= 0)
                rb->snprintf(line, sizeof(line), "%lu TRACKS  INDEX ONLINE",
                             (unsigned long)library_track_count);
            else
                rb->snprintf(line, sizeof(line), "RBPREP INDEX NOT FOUND");
            text(10, 28, line, library_fd >= 0
                 ? LCD_RGBPACK(70, 235, 125) : LCD_RGBPACK(255, 90, 70));
            for (i = 0; i < 6; i++) {
                if (i == selection) {
                    rb->lcd_set_foreground(LCD_RGBPACK(25, 105, 175));
                    rb->lcd_fillrect(0, 50 + i * 27, LCD_WIDTH, 25);
                }
                text(10, 56 + i * 27, items[i], LCD_WHITE);
            }
        }
        rb->lcd_update();
        force_full_redraw = false;
        return;
    }

    update_vu_levels();
    if (force_full_redraw) {
        rb->lcd_clear_display();
        draw_top_hud();
    } else {
        draw_beat_phase();
        draw_metadata_line();
        draw_tool_status();
        if (overview_dirty ||
            !TIME_BEFORE(*rb->current_tick, overview_deadline)) {
            if (!TIME_BEFORE(*rb->current_tick, overview_deadline))
                overview_playhead_white = !overview_playhead_white;
            draw_overview_waveform();
            overview_dirty = false;
            overview_deadline = *rb->current_tick + RBPREP_OVERVIEW_TICKS;
            rb->lcd_update_rect(RBPREP_OVERVIEW_X, RBPREP_OVERVIEW_Y,
                                RBPREP_OVERVIEW_WIDTH,
                                RBPREP_OVERVIEW_HEIGHT);
        }
    }

    rb->lcd_set_foreground(LCD_RGBPACK(5, 10, 7));
    rb->lcd_fillrect(RBPREP_DECK_X, RBPREP_WAVE_TOP,
                     RBPREP_DECK_WIDTH,
                     RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP + 1);
    draw_waveform();
    draw_vu_meter();

    if (force_full_redraw) {
        rb->lcd_update();
        force_full_redraw = false;
        overview_dirty = false;
        overview_deadline = *rb->current_tick + RBPREP_OVERVIEW_TICKS;
    } else {
        rb->lcd_update_rect(172, 1, 45, 22);
        rb->lcd_update_rect(0, 24, LCD_WIDTH, 13);
        rb->lcd_update_rect(0, 58, LCD_WIDTH, 12);
        rb->lcd_update_rect(0, RBPREP_WAVE_TOP, LCD_WIDTH,
                            RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP + 1);
    }
}

static void reset_play_clock(int anchor, long tick)
{
    play_clock_anchor = clamp_playhead(anchor);
    play_clock_tick = tick;
    reported_audio_elapsed = -1;
    audio_sync_after = tick + MAX(1, HZ / 4);
}

static bool update_play_clock(void)
{
    int status = rb->audio_status();
    int position;
    long now = *rb->current_tick;
    struct mp3entry *id3;

    if (seek_state != SEEK_IDLE || !(status & AUDIO_STATUS_PLAY) ||
        (status & AUDIO_STATUS_PAUSE))
        return false;
    id3 = rb->audio_current_track();
    if (id3 && !TIME_BEFORE(now, audio_sync_after)) {
        int observed = clamp_playhead(id3->elapsed);
        if (observed != reported_audio_elapsed) {
            /* Audio elapsed is authoritative. Interpolate only between its
               updates so UI work and button traffic can never become clock. */
            reported_audio_elapsed = observed;
            play_clock_anchor = observed;
            play_clock_tick = now;
        }
    }
    position = clamp_playhead(play_clock_anchor +
        (long long)(now - play_clock_tick) * 1000 / HZ);
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
    seek_target = playhead;
    seek_preview = preview;
    seek_was_paused = original_pause;
    if (seek_state != SEEK_IDLE)
        return;

    rb->audio_pre_ff_rewind();
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
        seek_applied_target = seek_target;
        seek_applied_tick = now;
        seek_state = SEEK_SETTLE;
        seek_deadline = now + RBPREP_SEEK_SETTLE;
    } else if (seek_state == SEEK_SETTLE) {
        if (seek_preview && seek_was_paused) {
            rb->audio_resume();
            seek_state = SEEK_PREVIEW;
            seek_applied_tick = now;
            seek_deadline = now + RBPREP_PREVIEW_TICKS;
        } else if (seek_target != seek_applied_target) {
            rb->audio_ff_rewind(seek_target);
            seek_applied_target = seek_target;
            seek_applied_tick = now;
            seek_deadline = now + RBPREP_SEEK_SETTLE;
        } else {
            seek_state = SEEK_IDLE;
            playhead = seek_applied_target;
            reset_play_clock(playhead, seek_applied_tick);
        }
    } else {
        bool continue_scrub = seek_target != seek_applied_target;
        rb->audio_pause();
        rb->audio_pre_ff_rewind();
        rb->audio_ff_rewind(seek_target);
        seek_applied_target = seek_target;
        playhead = seek_target;
        if (continue_scrub) {
            seek_state = SEEK_SETTLE;
            seek_deadline = now + RBPREP_SEEK_SETTLE;
        } else {
            seek_state = SEEK_IDLE;
            reset_play_clock(playhead, now);
        }
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
        playhead = clamp_playhead(seek_applied_target +
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
        } else if (selection == 3) {
            mode = MODE_SETTINGS;
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
    } else if (mode == MODE_SETTINGS) {
        waveform_half = !waveform_half;
        rebuild_waveform_height_lut();
        configfile_save(RBPREP_CONFIG_FILE, rbprep_config,
                        ARRAYLEN(rbprep_config), RBPREP_CONFIG_VERSION);
        force_full_redraw = true;
    } else if (mode == MODE_DECK) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_SEEK) {
            if (deck_cue >= 0)
                playhead = deck_cue;
            audition_playhead();
        } else if (tool == TOOL_ZOOM) {
            zoom = 1;
        }
    } else if (mode == MODE_CUES) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_CUE_DELETE && deleted_cue_slot >= 0) {
            hotcues[deleted_cue_slot] = deleted_cue_time;
            hotcue_colors[deleted_cue_slot] = deleted_cue_color;
            cue_slot = deleted_cue_slot;
            deleted_cue_slot = -1;
            overview_dirty = true;
        } else if (tool == TOOL_CUE_SLOT || tool == TOOL_CUE_MOVE) {
            if (hotcues[cue_slot] >= 0)
                playhead = hotcues[cue_slot];
            audition_playhead();
        }
    } else if (mode == MODE_GRID) {
        if (active_tool() == TOOL_GRID_QUANTIZE)
            quantize = !quantize;
    } else if (mode == MODE_METADATA) {
        if (active_tool() == TOOL_META_GENRE)
            rb->splash(HZ, "Genre keyboard: next pass");
    }
}

static void long_select(void)
{
    if (mode == MODE_DECK) {
        if (active_tool() == TOOL_SEEK) {
            deck_cue = quantized_time(playhead);
            playhead = deck_cue;
        }
    } else if (mode == MODE_CUES) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_CUE_DELETE) {
            if (hotcues[cue_slot] >= 0) {
                deleted_cue_slot = cue_slot;
                deleted_cue_time = hotcues[cue_slot];
                deleted_cue_color = hotcue_colors[cue_slot];
                hotcues[cue_slot] = -1;
                overview_dirty = true;
            }
        } else if (tool == TOOL_CUE_SLOT || tool == TOOL_CUE_MOVE) {
            hotcues[cue_slot] = quantized_time(playhead);
            hotcue_colors[cue_slot] = color_index;
            playhead = hotcues[cue_slot];
            deleted_cue_slot = -1;
            overview_dirty = true;
        }
    } else if (mode == MODE_GRID) {
        if (active_tool() == TOOL_GRID_ORIGIN) {
            int index = nearest_beat_index(playhead);
            if (index >= 0) {
                grid_offset += playhead - (beat_times[index] + grid_offset);
                grid_beat_shift = (1 - beat_numbers[index]) & 3;
            } else {
                grid_phase_ms = playhead;
                grid_offset = 0;
            }
        }
    }
}

static void change_zoom(bool zoom_in)
{
    if (zoom_in)
        zoom = zoom >= RBPREP_MAX_ZOOM ? 1 : zoom * 2;
    else
        zoom = zoom <= 1 ? RBPREP_MAX_ZOOM : zoom / 2;
}

static void change_tool(bool next)
{
    int *selected = tool_selection();
    int count = tool_count();

    *selected = (*selected + (next ? 1 : count - 1)) % count;
    force_full_redraw = true;
}

static void change_volume(int direction)
{
    int volume = rb->global_status->volume + direction;

    volume = MAX(rb->sound_min(SOUND_VOLUME),
                 MIN(rb->sound_max(SOUND_VOLUME), volume));
    if (volume != rb->global_status->volume)
        rb->sound_set(SOUND_VOLUME, volume);
}

static void adjust_active_tool(int direction)
{
    enum rbprep_tool tool = active_tool();

    if (tool == TOOL_SEEK || tool == TOOL_CUE_MOVE ||
        tool == TOOL_GRID_ORIGIN)
        seek_by(direction * scrub_step_ms(), true);
    else if (tool == TOOL_ZOOM)
        change_zoom(direction > 0);
    else if (tool == TOOL_GAIN)
        change_volume(direction);
    else if (tool == TOOL_CUE_SLOT || tool == TOOL_CUE_DELETE)
        cue_slot = (cue_slot + (direction > 0 ? 1 : 15)) & 15;
    else if (tool == TOOL_CUE_COLOR) {
        hotcue_colors[cue_slot] =
            (hotcue_colors[cue_slot] + (direction > 0 ? 1 : 7)) & 7;
        color_index = hotcue_colors[cue_slot];
        overview_dirty = true;
    } else if (tool == TOOL_GRID_NUDGE)
        grid_offset += direction;
    else if (tool == TOOL_GRID_BPM)
        grid_bpm_x100 = MAX(3000, MIN(30000,
                                     grid_bpm_x100 + direction));
    else if (tool == TOOL_GRID_QUANTIZE)
        quantize = direction > 0;
    else if (tool == TOOL_META_RATING)
        rating = MAX(0, MIN(5, rating + direction));
    else if (tool == TOOL_META_COLOR)
        color_index = (color_index + (direction > 0 ? 1 : 7)) & 7;
    else if (tool == TOOL_META_YEAR)
        track_year = MAX(0, MIN(9999, track_year + direction));
}

static void adjust_active_tool_coarse(int direction)
{
    enum rbprep_tool tool = active_tool();

    if (tool == TOOL_SEEK || tool == TOOL_CUE_MOVE ||
        tool == TOOL_GRID_ORIGIN)
        seek_by(direction * 1000, true);
    else if (tool == TOOL_GAIN) {
        int i;
        for (i = 0; i < 5; i++)
            change_volume(direction);
    } else if (tool == TOOL_GRID_NUDGE)
        grid_offset += direction * 10;
    else if (tool == TOOL_GRID_BPM)
        grid_bpm_x100 = MAX(3000, MIN(30000,
                                     grid_bpm_x100 + direction * 100));
    else if (tool == TOOL_META_YEAR)
        track_year = MAX(0, MIN(9999, track_year + direction * 10));
    else
        adjust_active_tool(direction);
}

enum plugin_status plugin_start(const void *parameter)
{
    int button;
    int pressed = BUTTON_NONE;
    bool select_hold_fired = false;
    bool redraw = true;
    long frame_deadline;

    (void)parameter;
    atexit(rbprep_cleanup);
#ifdef HAS_BUTTON_HOLD
    display_locked = rb->button_hold();
#else
    display_locked = false;
#endif
    if (display_locked)
        backlight_use_settings();
    else
        backlight_ignore_timeout();
#ifdef HAVE_BACKLIGHT
    if (!display_locked)
        rb->backlight_on();
#endif
    rb->lcd_setfont(FONT_SYSFIXED);
    mode = MODE_LIBRARY;
    selection = 0;
    playhead = 0;
    zoom = 1;
    grid_offset = 0;
    grid_beat_shift = 0;
    cue_slot = 0;
    deck_tool = cue_tool = grid_tool = metadata_tool = 0;
    rating = 0;
    color_index = 0;
    track_year = 0;
    quantize = true;
    overview_playhead_white = true;
    seek_state = SEEK_IDLE;
    suppress_menu = suppress_play = false;
    suppress_left = suppress_right = false;
    force_full_redraw = true;
    selected_title[0] = selected_artist[0] = selected_genre[0] = '\0';
    selected_track_index = selected_track_id = -1;
    configfile_load(RBPREP_CONFIG_FILE, rbprep_config,
                    ARRAYLEN(rbprep_config), RBPREP_CONFIG_VERSION);
    waveform_half = !!waveform_half;
    rebuild_waveform_height_lut();
    clear_analysis();
    open_library_index();
    reset_play_clock(playhead, *rb->current_tick);
    overview_deadline = *rb->current_tick;
    frame_deadline = *rb->current_tick;

    /* Discard the release of SELECT used to launch the plugin. */
    rb->button_clear_queue();
    while (true) {
        struct mp3entry *id3 = rb->audio_current_track();
        if (service_display_lock())
            redraw = true;
        if (selected_track_id >= 0 && id3 && id3->length > 0) {
            track_length = id3->length;
            if (track_year == 0 && id3->year > 0)
                track_year = id3->year;
        }
        if (service_audio_seek())
            redraw = true;
        if (update_play_clock())
            redraw = true;

        if (mode >= MODE_DECK && !display_locked && !redraw &&
            !TIME_BEFORE(*rb->current_tick, overview_deadline)) {
            overview_playhead_white = !overview_playhead_white;
            draw_overview_waveform();
            rb->lcd_update_rect(RBPREP_OVERVIEW_X, RBPREP_OVERVIEW_Y,
                                RBPREP_OVERVIEW_WIDTH,
                                RBPREP_OVERVIEW_HEIGHT);
            overview_deadline = *rb->current_tick +
                                RBPREP_OVERVIEW_TICKS;
        }

        if (!display_locked && redraw && (force_full_redraw ||
            !TIME_BEFORE(*rb->current_tick, frame_deadline))) {
            draw_screen();
            redraw = false;
            frame_deadline = *rb->current_tick + RBPREP_FRAME_TICKS;
        }
        button = rb->button_get_w_tmo(1);
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
            select_hold_fired = false;
            if (mode >= MODE_DECK &&
                (rb->button_status() & BUTTON_LEFT)) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_left = true;
                change_tool(false);
            } else if (mode >= MODE_DECK &&
                       (rb->button_status() & BUTTON_RIGHT)) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_right = true;
                change_tool(true);
            } else if (mode >= MODE_DECK &&
                       (rb->button_status() & BUTTON_MENU)) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_menu = true;
                mode = previous_mode(mode);
                force_full_redraw = true;
            } else if (mode >= MODE_DECK &&
                       (rb->button_status() & BUTTON_PLAY)) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_play = true;
                mode = next_mode(mode);
                force_full_redraw = true;
            } else {
                pressed = BUTTON_SELECT;
            }
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
            if (mode == MODE_SETTINGS) {
                mode = MODE_LIBRARY;
            } else if (mode == MODE_PLAYLISTS &&
                       tree_parent != RBPREP_ROOT_NODE) {
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
            if (!select_hold_fired && mode >= MODE_DECK) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_left = true;
                change_tool(false);
            }
            break;
        case BUTTON_SELECT | BUTTON_RIGHT:
        case BUTTON_SELECT | BUTTON_RIGHT | BUTTON_REPEAT:
            if (!select_hold_fired && mode >= MODE_DECK) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_right = true;
                change_tool(true);
            }
            break;
        case BUTTON_SELECT | BUTTON_MENU:
        case BUTTON_SELECT | BUTTON_MENU | BUTTON_REPEAT:
            if (!select_hold_fired && mode >= MODE_DECK) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_menu = true;
                mode = previous_mode(mode);
                force_full_redraw = true;
            }
            break;
        case BUTTON_SELECT | BUTTON_PLAY:
        case BUTTON_SELECT | BUTTON_PLAY | BUTTON_REPEAT:
            if (!select_hold_fired && mode >= MODE_DECK) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_play = true;
                mode = next_mode(mode);
                force_full_redraw = true;
            }
            break;
        case BUTTON_SELECT | BUTTON_LEFT | BUTTON_REL:
            suppress_left = false;
            pressed = BUTTON_NONE;
            break;
        case BUTTON_SELECT | BUTTON_RIGHT | BUTTON_REL:
            suppress_right = false;
            pressed = BUTTON_NONE;
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
                selection = MIN(5, selection + 1);
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
            else if (mode == MODE_SETTINGS) {
                waveform_half = 1;
                rebuild_waveform_height_lut();
                configfile_save(RBPREP_CONFIG_FILE, rbprep_config,
                                ARRAYLEN(rbprep_config),
                                RBPREP_CONFIG_VERSION);
                force_full_redraw = true;
            } else
                adjust_active_tool(1);
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
            } else if (mode == MODE_SETTINGS) {
                waveform_half = 0;
                rebuild_waveform_height_lut();
                configfile_save(RBPREP_CONFIG_FILE, rbprep_config,
                                ARRAYLEN(rbprep_config),
                                RBPREP_CONFIG_VERSION);
                force_full_redraw = true;
            } else
                adjust_active_tool(-1);
            break;
        case BUTTON_LEFT:
            if (suppress_left)
                break;
            if (mode >= MODE_DECK)
                pressed = BUTTON_LEFT;
            break;
        case BUTTON_LEFT | BUTTON_REPEAT:
            if (!suppress_left && mode >= MODE_DECK)
                adjust_active_tool_coarse(-1);
            break;
        case BUTTON_LEFT | BUTTON_REL:
            if (suppress_left) {
                suppress_left = false;
            } else if (pressed == BUTTON_LEFT && mode >= MODE_DECK) {
                pressed = BUTTON_NONE;
                adjust_active_tool_coarse(-1);
            }
            break;
        case BUTTON_RIGHT:
            if (suppress_right)
                break;
            if (mode >= MODE_DECK)
                pressed = BUTTON_RIGHT;
            break;
        case BUTTON_RIGHT | BUTTON_REPEAT:
            if (!suppress_right && mode >= MODE_DECK)
                adjust_active_tool_coarse(1);
            break;
        case BUTTON_RIGHT | BUTTON_REL:
            if (suppress_right) {
                suppress_right = false;
            } else if (pressed == BUTTON_RIGHT && mode >= MODE_DECK) {
                pressed = BUTTON_NONE;
                adjust_active_tool_coarse(1);
            }
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
