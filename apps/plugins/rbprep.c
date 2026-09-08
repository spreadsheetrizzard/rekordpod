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
#define RBPREP_GENRES "/.rockbox/rbprep/genres.rbg"
#define RBPREP_CUSTOM_GENRES "/.rockbox/rbprep/custom-genres.txt"
#define RBPREP_TRACK_DIR "/.rockbox/rbprep/tracks"
#define RBPREP_POINTS 131072
#define RBPREP_BEATS 16384
#define RBPREP_INDEX_HEADER 64
#define RBPREP_TRACK_RECORD 44
#define RBPREP_NODE_RECORD 24
#define RBPREP_TRACK_RECORD_V2 28
#define RBPREP_TRACK_RECORD_V1 24
#define RBPREP_NODE_RECORD_V1 20
#define RBPREP_ROOT_NODE 0xffffffffu
#define RBPREP_LIST_ROWS 9
#define RBPREP_TREE_NODES 2048
#define RBPREP_DECK_X 0
#define RBPREP_VU_WIDTH 20
#define RBPREP_DECK_WIDTH (LCD_WIDTH - RBPREP_VU_WIDTH - 1)
#define RBPREP_WAVE_TOP 86
#define RBPREP_WAVE_BOTTOM (LCD_HEIGHT - 1)
#define RBPREP_SIGNAL_HEIGHT 128
#define RBPREP_SIGNAL_TOP \
    (RBPREP_WAVE_TOP + (RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP + 1 \
                        - RBPREP_SIGNAL_HEIGHT) / 2)
#define RBPREP_SIGNAL_BOTTOM (RBPREP_SIGNAL_TOP + RBPREP_SIGNAL_HEIGHT - 1)
#define RBPREP_OVERVIEW_X 0
#define RBPREP_OVERVIEW_Y 34
#define RBPREP_OVERVIEW_WIDTH LCD_WIDTH
#define RBPREP_OVERVIEW_HEIGHT 28
#define RBPREP_TOOL_TOP 64
#define RBPREP_TOOL_HEIGHT 20
#define RBPREP_MAX_ZOOM 128
#define RBPREP_SEEK_DEBOUNCE MAX(1, HZ / 20)
#define RBPREP_SEEK_SETTLE MAX(1, HZ / 10)
#define RBPREP_CUE_SETTLE MAX(1, HZ / 50)
#define RBPREP_PREVIEW_TICKS MAX(1, HZ * 4 / 25)
#define RBPREP_OVERVIEW_TICKS MAX(1, HZ / 2)
#define RBPREP_FRAME_TICKS MAX(1, HZ / 20)
#define RBPREP_HUD_SCROLL_TICKS MAX(1, HZ / 10)
#define RBPREP_CONFIG_VERSION 3
#define RBPREP_CONFIG_FILE "/.rockbox/rbprep/rbprep.cfg"
#define RBPREP_PLAYLIST_JOURNAL "/.rockbox/rbprep/playlist-adds.rba"
#define RBPREP_EDIT_JOURNAL "/.rockbox/rbprep/edits.rbe"
#define RBPREP_BURN_STATE "/.rockbox/rbprep/local-burn.rbs"
/* Fixed-size, little-endian RBE1 snapshots make interrupted appends harmless
   and keep the eventual macOS importer independent of compiler struct layout. */
#define RBPREP_EDIT_RECORD_SIZE 216
#define RBPREP_PENDING_ROWS 8
#define RBPREP_PENDING_MAX 512
#define RBPREP_SEARCH_MAX 8192
#define RBPREP_SPECTRUM_BANDS 20
#define RBPREP_PCM_FRAMES 1024
#define RBPREP_GREEN LCD_RGBPACK(70, 235, 125)
#define RBPREP_GREEN_DIM LCD_RGBPACK(28, 75, 48)
#define RBPREP_MENU_TEXT LCD_RGBPACK(188, 205, 193)

enum rbprep_mode {
    MODE_LIBRARY,
    MODE_PLAYLISTS,
    MODE_TRACKS,
    MODE_FILTER,
    MODE_USB,
    MODE_SETTINGS,
    MODE_PENDING,
    MODE_INDEX,
    MODE_GENRES,
    MODE_DECK,
    MODE_GRID,
    MODE_CUES,
    MODE_LOOP,
    MODE_METADATA
};

enum rbprep_tool {
    TOOL_SEEK,
    TOOL_SCRUB_STEP,
    TOOL_ZOOM,
    TOOL_GAIN,
    TOOL_HOST_RPM,
    TOOL_PLAY_RPM,
    TOOL_PITCH_BEND,
    TOOL_TEMPO,
    TOOL_PLAYLIST_MODE,
    TOOL_ADD_PLAYLIST,
    TOOL_WAVEFORM_STYLE,
    TOOL_GRID_NUDGE,
    TOOL_GRID_BPM,
    TOOL_GRID_ORIGIN,
    TOOL_GRID_QUANTIZE,
    TOOL_CUE_SLOT,
    TOOL_CUE_MOVE,
    TOOL_CUE_COLOR,
    TOOL_CUE_DELETE,
    TOOL_LOOP_LENGTH,
    TOOL_LOOP_IN,
    TOOL_LOOP_OUT,
    TOOL_LOOP_ACTIVE,
    TOOL_META_RATING,
    TOOL_META_COLOR,
    TOOL_META_YEAR,
    TOOL_META_GENRE,
    TOOL_COUNT
};

enum rbprep_confirm_action {
    CONFIRM_NONE,
    CONFIRM_EXIT,
    CONFIRM_DECK_CUE,
    CONFIRM_CUE_SET,
    CONFIRM_CUE_DELETE,
    CONFIRM_GRID_ORIGIN,
    CONFIRM_KEEP_EDIT,
    CONFIRM_ADD_PLAYLIST,
    CONFIRM_GENRE,
    CONFIRM_BURN,
    CONFIRM_DELETE_PENDING,
    CONFIRM_DELETE_PLAYLIST
};

struct rbprep_wave_column {
    unsigned char amplitude;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    bool valid;
};

struct rbprep_pending_entry {
    uint32_t track_id;
    uint32_t saved_tick;
    int bpm_x100;
    int rating;
    int color;
    char title[64];
};

struct rbprep_pending_playlist {
    uint32_t track_id;
    uint32_t playlist_id;
    char name[64];
};

static enum rbprep_mode mode;
static int selection;
static int playhead;
static unsigned char waveform[RBPREP_POINTS][4];
static unsigned char overview_waveform[RBPREP_OVERVIEW_WIDTH][4];
static struct rbprep_wave_column waveform_columns[RBPREP_DECK_WIDTH];
static unsigned char waveform_height_lut[256];
static int waveform_column_first;
static int waveform_column_span;
static bool waveform_columns_valid;
static int waveform_points;
static int waveform_duration_ms = 1;
static int beat_times[RBPREP_BEATS];
static unsigned char beat_numbers[RBPREP_BEATS];
static int beat_count;
static int zoom = 1;
static int grid_offset;
static int grid_phase_ms = 26;
static int grid_bpm_x100 = 15000;
static int grid_source_bpm_x100 = 15000;
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
static long hud_scroll_deadline;
static int title_scroll_px;
static int metadata_scroll_px;
static bool hud_scroll_active;
static int deck_tool;
static int grid_tool;
static int cue_tool;
static int loop_tool;
static int metadata_tool;
static int waveform_half;
static int visualizer_mode;
static int save_on_track_load;
static int scrub_step_index = 4;
static int settings_selection;
static int usb_selection;
static int genre_selection;
static int genre_top;
static int genre_count;
static int built_genre_count;
static int custom_genre_count;
#define RBPREP_CUSTOM_GENRE_MAX 32
static char custom_genres[RBPREP_CUSTOM_GENRE_MAX][32];
static char confirm_genre[32];
static int16_t pcm_capture[2][RBPREP_PCM_FRAMES * 2] MEM_ALIGN_ATTR;
static int16_t pcm_snapshot[RBPREP_PCM_FRAMES * 2] MEM_ALIGN_ATTR;
static int16_t pcm_mono[RBPREP_PCM_FRAMES] MEM_ALIGN_ATTR;
static int16_t pcm_pitch[RBPREP_PCM_FRAMES] MEM_ALIGN_ATTR;
static volatile int pcm_capture_index;
static volatile int pcm_capture_frames[2];
static volatile unsigned int pcm_capture_generation;
static volatile unsigned long pcm_sample_rate = 44100;
static unsigned int spectrum_generation;
static unsigned char spectrum_levels[RBPREP_SPECTRUM_BANDS];
static unsigned char spectrum_bass;
static unsigned char spectrum_bass_hit;
static int bass_frequency_x10;
static int bass_note_index = -1;
static unsigned char bass_pitch_confidence;
static int32_t bass_filter_state;
static int host_rpm_index;
static int played_rpm_index;
static int pitch_bend_x100;
static int tempo_x100 = PITCH_SPEED_100;
static int32_t original_pitch = PITCH_SPEED_100;
static int32_t original_stretch = PITCH_SPEED_100;
static bool original_timestretch_enabled;
static bool playback_rate_changed;
static bool playlist_playback;
static bool playlist_add_mode;
static int playing_track_row = -1;
static bool audio_was_running;
static bool confirm_active;
static bool confirm_ok;
static bool confirm_wait_release;
static bool exit_requested;
static enum rbprep_confirm_action confirm_action;
static char confirm_message[64];
static int confirm_slot;
static int confirm_time;
static unsigned char confirm_color;
static int confirm_playlist_node;
static int staged_tool = -1;
static int staged_original;
static int pending_snapshot_count;
static int pending_playlist_count;
static int pending_visible_count;
static struct rbprep_pending_entry pending_entries[RBPREP_PENDING_MAX];
static struct rbprep_pending_playlist
    pending_playlists[RBPREP_PENDING_MAX];
static int pending_selection;
static int pending_top;
static bool track_edit_dirty;
static bool cue_audition_active;
static bool cue_audition_latched;
static int cue_audition_position;
static uint32_t vu_left;
static uint32_t vu_right;
static bool suppress_menu;
static bool suppress_play;
static bool suppress_left;
static bool suppress_right;
static bool tool_menu_active;
static enum rbprep_tool tool_menu_original;
static bool menu_button_down;
static bool menu_hold_fired;
static long menu_pressed_tick;
static int loop_length_index = 7;
static int loop_in = -1;
static int loop_out = -1;
static bool loop_active;

enum rbprep_seek_state {
    SEEK_IDLE,
    SEEK_DEBOUNCE,
    SEEK_SETTLE,
    SEEK_PREVIEW,
    SEEK_CUE_HOLD
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
    uint32_t key_offset;
    int bpm_x100;
    int rating;
    int color;
    int year;
    uint32_t comments_offset;
    uint32_t tags_offset;
    uint32_t search_offset;
};

enum rbprep_track_sort {
    TRACK_SORT_TITLE,
    TRACK_SORT_BPM,
    TRACK_SORT_YEAR,
    TRACK_SORT_KEY,
    TRACK_SORT_COMMENTS,
    TRACK_SORT_TAGS,
    TRACK_SORT_COUNT
};

struct rbprep_node_record {
    uint32_t parent;
    uint32_t name_offset;
    uint32_t first_member;
    uint32_t member_count;
    int kind;
    uint32_t source_id;
};

static int library_fd = -1;
static int genre_fd = -1;
static uint32_t genre_offsets_base;
static uint32_t genre_strings_base;
static int library_index_version;
static int library_track_record_size;
static int library_node_record_size;
static uint32_t library_track_count;
static uint32_t library_node_count;
static uint32_t library_member_count;
static uint32_t library_track_offset;
static uint32_t library_node_offset;
static uint32_t library_member_offset;
static uint32_t library_string_offset;
static uint32_t library_sort_offsets[TRACK_SORT_COUNT];
static uint32_t tree_parent = RBPREP_ROOT_NODE;
static int tree_selection;
static int tree_top;
static int tree_child_count;
static int tree_children[RBPREP_TREE_NODES];
static int track_selection;
static int track_top;
static int track_row_count;
static int track_sort_key;
static bool track_sort_descending;
static int filter_selection;
static char track_search[40];
static uint32_t search_results[RBPREP_SEARCH_MAX];
static int search_result_count;
static bool search_active;
static int active_playlist_node = -1;
static int selected_track_index = -1;
static int selected_track_id = -1;
static enum rbprep_mode deck_return_mode = MODE_LIBRARY;
static char selected_title[96];
static char selected_artist[72];
static char selected_genre[32];
static char selected_key[24];
static char selected_extension[12];

static void stop_editor_audio(void);
static void reset_play_clock(int anchor, long tick);
static void jump_to_time(int target);
static bool flush_deferred_edit(void);
static void stop_spectrum_capture(void);
static void restore_playback_rate(void);

static void set_storage_performance_mode(bool enabled)
{
#ifdef DISK_SPINDOWN
    /* RBPrep's animation should not stall while ATA/iFlash wakes for the
       periodic playback-buffer refill. Restore the user's policy on lock or
       exit so this performance mode does not leak into normal Rockbox use. */
    rb->storage_spindown(enabled ? 254
                                 : rb->global_settings->disk_spindown);
#else
    (void)enabled;
#endif
}

static void rbprep_cleanup(void)
{
    flush_deferred_edit();
    stop_spectrum_capture();
    restore_playback_rate();
    set_storage_performance_mode(false);
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
        set_storage_performance_mode(false);
        backlight_use_settings();
    } else {
        set_storage_performance_mode(true);
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

static const char *cue_color_names[] = {
    "RED", "ORANGE", "YELLOW", "GREEN",
    "AQUA", "BLUE", "PURPLE", "PINK"
};

static const char *mode_names[] = {
    "LIBRARY", "PLAYLISTS", "TRACKS", "FILTER", "USB", "SETTINGS", "PENDING",
    "INDEX", "GENRES", "PLAYBACK", "BEATGRID", "HOT CUES", "LOOP",
    "METADATA"
};

static char *waveform_styles[] = { "full", "half" };
static char *visualizer_styles[] = {
    "rgb waveform", "boombox bass", "20-band EQ", "turntable"
};
static char *rpm_styles[] = { "33", "45", "78" };
/* All values share a denominator of three: 33 1/3, 45 and 78 RPM. */
static const int rpm_values_x3[] = { 100, 135, 234 };
static const char *spectrum_labels[] = {
    "45", "63", "90", "125", "180", "250", "355", "500", "710",
    "1K", "1.4", "2K", "2.8", "4K", "5.6", "8K", "11K", "15K",
    "18K", "21K"
};
static char *save_on_load_styles[] = { "immediate", "next track" };
static const char *track_sort_names[] = {
    "TITLE", "BPM", "YEAR", "KEY", "COMMENTS", "TAGS"
};
static const int scrub_steps[] = {
    1, 2, 5, 10, 20, 50, 100, 250, 500, 1000
};

static const struct configdata rbprep_config[] = {
    { TYPE_ENUM, 0, 1, { .int_p = &waveform_half },
      "waveform style", waveform_styles },
    { TYPE_ENUM, 0, ARRAYLEN(visualizer_styles) - 1,
      { .int_p = &visualizer_mode },
      "visualizer", visualizer_styles },
    { TYPE_ENUM, 0, 1, { .int_p = &save_on_track_load },
      "save edits", save_on_load_styles },
    { TYPE_INT, 0, ARRAYLEN(scrub_steps) - 1,
      { .int_p = &scrub_step_index }, "scrub step", NULL }
};

static int rpm_pitch_x100(void)
{
    return (long long)rpm_values_x3[played_rpm_index] * PITCH_SPEED_100 /
           rpm_values_x3[host_rpm_index];
}

static int deck_pitch_x100(void)
{
    return (long long)rpm_pitch_x100() *
           (PITCH_SPEED_100 + pitch_bend_x100) / PITCH_SPEED_100;
}

static int deck_speed_x100(void)
{
    return (long long)deck_pitch_x100() * tempo_x100 /
           PITCH_SPEED_100;
}

static void apply_playback_rate(void)
{
    int32_t pitch = deck_pitch_x100();

    rb->dsp_timestretch_enable(true);
    if (rb->dsp_timestretch_available()) {
        rb->sound_set_pitch(pitch);
        rb->dsp_set_timestretch(tempo_x100);
    } else {
        /* A codec without timestretch still gets the requested total rate;
           like a physical deck, its pitch follows the speed. */
        rb->sound_set_pitch(deck_speed_x100());
    }
    playback_rate_changed = true;
    reset_play_clock(playhead, *rb->current_tick);
}

static void restore_playback_rate(void)
{
    if (!playback_rate_changed)
        return;
    rb->sound_set_pitch(original_pitch);
    rb->dsp_set_timestretch(original_stretch);
    rb->dsp_timestretch_enable(original_timestretch_enabled);
    playback_rate_changed = false;
}

static void spectrum_sample_rate_changed(uint32_t samplerate)
{
    pcm_sample_rate = samplerate ? samplerate : 44100;
}

static void spectrum_buffer_callback(const void *start, size_t size)
{
    const int16_t *samples = start;
    int frames = size / (2 * sizeof(int16_t));
    int index;

    if (frames <= 0)
        return;
    if (frames > RBPREP_PCM_FRAMES) {
        samples += (frames - RBPREP_PCM_FRAMES) * 2;
        frames = RBPREP_PCM_FRAMES;
    }
    index = pcm_capture_index ^ 1;
    memcpy(pcm_capture[index], samples,
           frames * 2 * sizeof(int16_t));
    pcm_capture_frames[index] = frames;
    pcm_capture_index = index;
    pcm_capture_generation++;
}

static const struct mixer_buffer_cbs spectrum_buffer_cbs = {
    .next_buffer = spectrum_buffer_callback,
    .sampr_changed = spectrum_sample_rate_changed,
};

static void start_spectrum_capture(void)
{
    pcm_capture_index = 0;
    pcm_capture_frames[0] = pcm_capture_frames[1] = 0;
    pcm_capture_generation = spectrum_generation = 0;
    pcm_sample_rate = rb->mixer_get_frequency();
    rb->memset(spectrum_levels, 0, sizeof(spectrum_levels));
    spectrum_bass = spectrum_bass_hit = 0;
    bass_frequency_x10 = 0;
    bass_note_index = -1;
    bass_pitch_confidence = 0;
    bass_filter_state = 0;
    rb->mixer_channel_set_buffer_hook(PCM_MIXER_CHAN_PLAYBACK,
                                      &spectrum_buffer_cbs);
}

static void stop_spectrum_capture(void)
{
    rb->mixer_channel_set_buffer_hook(PCM_MIXER_CHAN_PLAYBACK, NULL);
}

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

/* Three-pixel hexadecimal glyphs, packed as five rows of three bits. */
static const uint16_t micro_hex_glyphs[16] = {
    0x7B6F, 0x2492, 0x73E7, 0x73CF,
    0x5BC9, 0x79CF, 0x79EF, 0x7249,
    0x7BEF, 0x7BCF, 0x7BED, 0x6BAE,
    0x7927, 0x6B6E, 0x79E7, 0x79E4
};

static void draw_micro_cue_marker(int center_x, int top, int slot, int color)
{
    int row;
    int column;
    /* Cue slots are presented as 1..9, A..F, 0 in the tiny 3px label. */
    uint16_t glyph = micro_hex_glyphs[(slot + 1) & 15];

    rb->lcd_set_foreground(color);
    rb->lcd_fillrect(center_x - 2, top, 5, 5);
    rb->lcd_hline(center_x - 1, center_x + 1, top + 5);
    rb->lcd_drawpixel(center_x, top + 6);
    rb->lcd_set_foreground(LCD_BLACK);
    for (row = 0; row < 5; row++) {
        int bits = (glyph >> ((4 - row) * 3)) & 7;
        for (column = 0; column < 3; column++) {
            if (bits & (4 >> column))
                rb->lcd_drawpixel(center_x - 1 + column, top + row);
        }
    }
}

static void draw_overview_cue_marker(int center_x, int slot, int color)
{
    int x = MAX(RBPREP_OVERVIEW_X + 2,
                MIN(RBPREP_OVERVIEW_X + RBPREP_OVERVIEW_WIDTH - 3,
                    center_x));

    draw_micro_cue_marker(x, RBPREP_OVERVIEW_Y, slot, color);
}

static void draw_radial_cue_marker(int platter_x, int platter_y,
                                   int tip_x, int tip_y,
                                   int slot, int color)
{
    uint16_t glyph = micro_hex_glyphs[(slot + 1) & 15];
    int dx = tip_x - platter_x;
    int dy = tip_y - platter_y;
    int body_x;
    int body_y;
    int row;
    int column;

    /* The marker tip is planted on the cue's groove.  Its 5px labeled body
       always grows away from the spindle, using cardinal shapes so the tiny
       glyph remains upright and legible while the record rotates. */
    rb->lcd_set_foreground(color);
    if (ABS(dx) >= ABS(dy)) {
        body_x = dx >= 0 ? tip_x + 2 : tip_x - 6;
        body_y = tip_y - 2;
        rb->lcd_vline(dx >= 0 ? tip_x + 1 : tip_x - 1,
                      tip_y - 1, tip_y + 1);
    } else {
        body_x = tip_x - 2;
        body_y = dy >= 0 ? tip_y + 2 : tip_y - 6;
        rb->lcd_hline(tip_x - 1, tip_x + 1,
                      dy >= 0 ? tip_y + 1 : tip_y - 1);
    }
    rb->lcd_drawpixel(tip_x, tip_y);
    rb->lcd_fillrect(body_x, body_y, 5, 5);

    rb->lcd_set_foreground(LCD_BLACK);
    for (row = 0; row < 5; row++) {
        int bits = (glyph >> ((4 - row) * 3)) & 7;
        for (column = 0; column < 3; column++) {
            if (bits & (4 >> column))
                rb->lcd_drawpixel(body_x + 1 + column, body_y + row);
        }
    }
}

/* Beat lengths are stored in thirty-seconds so sub-beat loops stay exact. */
static const int loop_beats_x32[] = {
    1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024
};

static const char *loop_length_names[] = {
    "1/32", "1/16", "1/8", "1/4", "1/2", "1",
    "2", "4", "8", "16", "32"
};

static void rebuild_waveform_height_lut(void)
{
    int amplitude;
    int height = waveform_half
               ? RBPREP_SIGNAL_HEIGHT - 2
               : RBPREP_SIGNAL_HEIGHT / 2 - 2;

    for (amplitude = 0; amplitude < 256; amplitude++)
        waveform_height_lut[amplitude] = 1 + amplitude * height / 255;
}

static void text(int x, int y, const char *s, int color)
{
    rb->lcd_set_foreground(color);
    rb->lcd_putsxy(x, y, s);
}

static void centered_text(int left, int width, int y,
                          const char *s, int color)
{
    int text_width;

    rb->lcd_getstringsize(s, &text_width, NULL);
    text(left + MAX(0, (width - text_width) / 2), y, s, color);
}

static void format_percent_delta(char *buffer, size_t size, int delta_x100)
{
    char sign = delta_x100 < 0 ? '-' : '+';
    int magnitude = ABS(delta_x100);

    rb->snprintf(buffer, size, "%c%d.%02d", sign,
                 magnitude / 100, magnitude % 100);
}

static void restore_black_canvas(void)
{
    rb->lcd_set_background(LCD_BLACK);
    rb->lcd_set_foreground(LCD_WHITE);
    rb->lcd_set_drawmode(DRMODE_SOLID);
    rb->lcd_clear_display();
    force_full_redraw = true;
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
        !read_index_at(library_track_offset + index * library_track_record_size,
                       data, library_track_record_size))
        return false;
    track->id = read_u32(data);
    track->path_offset = read_u32(data + 4);
    track->title_offset = read_u32(data + 8);
    track->artist_offset = read_u32(data + 12);
    track->genre_offset = read_u32(data + 16);
    if (library_index_version >= 2) {
        track->key_offset = read_u32(data + 20);
        track->bpm_x100 = read_u16(data + 24);
        track->rating = data[26];
        track->color = data[27];
    } else {
        track->key_offset = 0;
        track->bpm_x100 = read_u16(data + 20);
        track->rating = data[22];
        track->color = data[23];
    }
    if (library_index_version >= 3) {
        track->year = read_u16(data + 28);
        track->comments_offset = read_u32(data + 32);
        track->tags_offset = read_u32(data + 36);
        track->search_offset = read_u32(data + 40);
    } else {
        track->year = 0;
        track->comments_offset = 0;
        track->tags_offset = 0;
        track->search_offset = 0;
    }
    return true;
}

static bool read_node_record(int index, struct rbprep_node_record *node)
{
    unsigned char data[RBPREP_NODE_RECORD];

    if (index < 0 || (uint32_t)index >= library_node_count ||
        !read_index_at(library_node_offset + index * library_node_record_size,
                       data, library_node_record_size))
        return false;
    node->parent = read_u32(data);
    node->name_offset = read_u32(data + 4);
    node->first_member = read_u32(data + 8);
    node->member_count = read_u32(data + 12);
    node->kind = data[16];
    node->source_id = library_index_version >= 2 ? read_u32(data + 20) : 0;
    return true;
}

static bool read_index_string(uint32_t offset, char *buffer, size_t size)
{
    ssize_t bytes;
    size_t used;

    if (size == 0 || library_fd < 0 ||
        rb->lseek(library_fd, library_string_offset + offset, SEEK_SET) < 0)
        return false;
    bytes = rb->read(library_fd, buffer, size - 1);
    if (bytes < 0)
        return false;
    used = 0;
    while (used < (size_t)bytes && buffer[used])
        used++;
    buffer[used] = '\0';
    return true;
}

static bool read_built_genre(int index, char *buffer, size_t size)
{
    unsigned char raw[4];
    uint32_t offset;
    size_t used = 0;
    unsigned char value;

    if (index < 0 || index >= built_genre_count || genre_fd < 0 ||
        size == 0 ||
        rb->lseek(genre_fd, genre_offsets_base + index * 4, SEEK_SET) < 0 ||
        rb->read(genre_fd, raw, sizeof(raw)) != sizeof(raw))
        return false;
    offset = read_u32(raw);
    if (rb->lseek(genre_fd, genre_strings_base + offset, SEEK_SET) < 0)
        return false;
    while (used + 1 < size && rb->read(genre_fd, &value, 1) == 1 && value)
        buffer[used++] = value;
    buffer[used] = '\0';
    return true;
}

static bool genre_name_at(int index, char *buffer, size_t size)
{
    if (index < 0 || index >= genre_count)
        return false;
    if (index < built_genre_count)
        return read_built_genre(index, buffer, size);
    rb->strlcpy(buffer, custom_genres[index - built_genre_count], size);
    return true;
}

static bool genre_exists(const char *name)
{
    int i;
    char candidate[32];

    for (i = 0; i < genre_count; i++) {
        if (genre_name_at(i, candidate, sizeof(candidate)) &&
            !rb->strcasecmp(candidate, name))
            return true;
    }
    return false;
}

static void load_genre_rollup(void)
{
    unsigned char header[16];
    char line[32];
    int fd;
    int length = 0;
    unsigned char value;

    if (genre_fd >= 0)
        rb->close(genre_fd);
    genre_fd = rb->open(RBPREP_GENRES, O_RDONLY);
    built_genre_count = 0;
    custom_genre_count = 0;
    if (genre_fd >= 0 &&
        rb->read(genre_fd, header, sizeof(header)) == sizeof(header) &&
        !rb->memcmp(header, "RBG1", 4) && read_u16(header + 4) == 1 &&
        read_u16(header + 6) == sizeof(header)) {
        built_genre_count = read_u32(header + 8);
        genre_offsets_base = sizeof(header);
        genre_strings_base = genre_offsets_base + built_genre_count * 4;
    } else if (genre_fd >= 0) {
        rb->close(genre_fd);
        genre_fd = -1;
    }
    genre_count = built_genre_count;

    fd = rb->open(RBPREP_CUSTOM_GENRES, O_RDONLY);
    if (fd >= 0) {
        while (custom_genre_count < RBPREP_CUSTOM_GENRE_MAX &&
               rb->read(fd, &value, 1) == 1) {
            if (value == '\n' || value == '\r') {
                if (length > 0) {
                    line[length] = '\0';
                    if (!genre_exists(line)) {
                        rb->strlcpy(custom_genres[custom_genre_count++],
                                    line, sizeof(custom_genres[0]));
                        genre_count = built_genre_count + custom_genre_count;
                    }
                    length = 0;
                }
            } else if (value >= 32 && length + 1 < (int)sizeof(line)) {
                line[length++] = value;
            }
        }
        if (length > 0 && custom_genre_count < RBPREP_CUSTOM_GENRE_MAX) {
            line[length] = '\0';
            if (!genre_exists(line))
                rb->strlcpy(custom_genres[custom_genre_count++], line,
                            sizeof(custom_genres[0]));
        }
        rb->close(fd);
    }
    genre_count = built_genre_count + custom_genre_count;
}

static bool append_custom_genre(const char *name)
{
    char line[40];
    int fd;
    int length;

    if (!name[0] || genre_exists(name))
        return true;
    if (custom_genre_count >= RBPREP_CUSTOM_GENRE_MAX)
        return false;
    fd = rb->open(RBPREP_CUSTOM_GENRES,
                  O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return false;
    length = rb->snprintf(line, sizeof(line), "%s\n", name);
    if (rb->write(fd, line, length) != length) {
        rb->close(fd);
        return false;
    }
    rb->close(fd);
    rb->strlcpy(custom_genres[custom_genre_count++], name,
                sizeof(custom_genres[0]));
    genre_count = built_genre_count + custom_genre_count;
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

static int collection_sorted_index_at(int row)
{
    unsigned char data[4];
    int position;

    if (row < 0 || (uint32_t)row >= library_track_count)
        return -1;
    position = track_sort_descending
             ? (int)library_track_count - 1 - row : row;
    if (track_sort_key == TRACK_SORT_TITLE ||
        library_sort_offsets[track_sort_key] == 0)
        return position;
    if (!read_index_at(library_sort_offsets[track_sort_key] +
                       position * 4, data, sizeof(data)))
        return -1;
    return read_u32(data);
}

static int track_index_at_row(int row)
{
    unsigned char data[4];
    struct rbprep_node_record node;

    if (active_playlist_node < 0) {
        if (search_active)
            return row >= 0 && row < search_result_count
                 ? (int)search_results[row] : -1;
        return collection_sorted_index_at(row);
    }
    if (!read_node_record(active_playlist_node, &node) || row < 0 ||
        (uint32_t)row >= node.member_count ||
        !read_index_at(library_member_offset +
                       (node.first_member + row) * 4, data, sizeof(data)))
        return -1;
    return read_u32(data);
}

static bool contains_ascii_nocase(const char *text_value,
                                  const char *needle)
{
    const unsigned char *text_start = (const unsigned char *)text_value;
    const unsigned char *candidate;

    if (!needle[0])
        return true;
    for (candidate = text_start; *candidate; candidate++) {
        const unsigned char *left = candidate;
        const unsigned char *right = (const unsigned char *)needle;
        while (*left && *right) {
            int a = *left;
            int b = *right;
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b)
                break;
            left++;
            right++;
        }
        if (!*right)
            return true;
    }
    return false;
}

static bool track_matches_search(int index)
{
    struct rbprep_track_record track;
    char searchable[320];
    char token[40];
    const char *cursor = track_search;

    if (!read_track_record(index, &track) ||
        !read_index_string(track.search_offset, searchable,
                           sizeof(searchable)))
        return false;

    while (*cursor) {
        int length = 0;
        while (*cursor == ' ' || *cursor == '\t')
            cursor++;
        while (*cursor && *cursor != ' ' && *cursor != '\t' &&
               length + 1 < (int)sizeof(token))
            token[length++] = *cursor++;
        token[length] = '\0';
        if (!length)
            break;
        if (!contains_ascii_nocase(searchable, token))
            return false;
    }
    return true;
}

static void rebuild_search_results(void)
{
    int row;

    search_active = track_search[0] != '\0';
    search_result_count = 0;
    if (!search_active) {
        track_row_count = library_track_count;
        return;
    }
    for (row = 0; (uint32_t)row < library_track_count; row++) {
        int index = collection_sorted_index_at(row);
        if ((row & 127) == 0)
            rb->splash_progress(row, library_track_count,
                                "Searching %.24s", track_search);
        if (index >= 0 && track_matches_search(index) &&
            search_result_count < RBPREP_SEARCH_MAX)
            search_results[search_result_count++] = index;
    }
    track_row_count = search_result_count;
    track_selection = track_top = 0;
    force_full_redraw = true;
}

static int find_track_index_by_id(uint32_t track_id)
{
    int index;
    struct rbprep_track_record track;

    for (index = 0; (uint32_t)index < library_track_count; index++) {
        if (read_track_record(index, &track) && track.id == track_id)
            return index;
    }
    return -1;
}

static bool open_library_index(void)
{
    unsigned char header[RBPREP_INDEX_HEADER];
    int version;
    int header_size;

    library_fd = rb->open(RBPREP_INDEX, O_RDONLY);
    version = 0;
    if (library_fd < 0 ||
        rb->read(library_fd, header, 40) != 40 ||
        rb->memcmp(header, "RBI1", 4) ||
        ((version = read_u16(header + 4)) != 1 && version != 2 &&
         version != 3)) {
        if (library_fd >= 0)
            rb->close(library_fd);
        library_fd = -1;
        return false;
    }
    header_size = read_u16(header + 6);
    if (version >= 3 &&
        (header_size < RBPREP_INDEX_HEADER ||
         rb->read(library_fd, header + 40,
                  RBPREP_INDEX_HEADER - 40) != RBPREP_INDEX_HEADER - 40)) {
        rb->close(library_fd);
        library_fd = -1;
        return false;
    }
    library_index_version = version;
    library_track_record_size = version >= 3 ? RBPREP_TRACK_RECORD
                              : version >= 2 ? RBPREP_TRACK_RECORD_V2
                                             : RBPREP_TRACK_RECORD_V1;
    library_node_record_size = version >= 2
                             ? RBPREP_NODE_RECORD
                             : RBPREP_NODE_RECORD_V1;
    library_track_count = read_u32(header + 8);
    library_node_count = read_u32(header + 12);
    library_member_count = read_u32(header + 16);
    library_track_offset = read_u32(header + 20);
    library_node_offset = read_u32(header + 24);
    library_member_offset = read_u32(header + 28);
    library_string_offset = read_u32(header + 32);
    library_sort_offsets[TRACK_SORT_TITLE] = 0;
    library_sort_offsets[TRACK_SORT_BPM] = version >= 3
                                          ? read_u32(header + 40) : 0;
    library_sort_offsets[TRACK_SORT_YEAR] = version >= 3
                                           ? read_u32(header + 44) : 0;
    library_sort_offsets[TRACK_SORT_KEY] = version >= 3
                                          ? read_u32(header + 48) : 0;
    library_sort_offsets[TRACK_SORT_COMMENTS] = version >= 3
                                               ? read_u32(header + 52) : 0;
    library_sort_offsets[TRACK_SORT_TAGS] = version >= 3
                                           ? read_u32(header + 56) : 0;
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

static int adjusted_beat_time(int index)
{
    long long delta;

    if (index < 0 || index >= beat_count)
        return grid_phase_ms + grid_offset;
    delta = (long long)beat_times[index] - grid_phase_ms;
    if (grid_bpm_x100 > 0 && grid_source_bpm_x100 > 0)
        delta = delta * grid_source_bpm_x100 / grid_bpm_x100;
    return grid_phase_ms + grid_offset + delta;
}

static int nearest_beat_index(int time_ms)
{
    int low = 0;
    int high = beat_count;

    if (beat_count <= 0)
        return -1;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (adjusted_beat_time(middle) < time_ms)
            low = middle + 1;
        else
            high = middle;
    }
    if (low <= 0)
        return 0;
    if (low >= beat_count)
        return beat_count - 1;
    if (time_ms - adjusted_beat_time(low - 1) <=
        adjusted_beat_time(low) - time_ms)
        return low - 1;
    return low;
}

static int current_beat_index(int time_ms)
{
    int low = 0;
    int high = beat_count;

    if (beat_count <= 0 || time_ms < adjusted_beat_time(0))
        return -1;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (adjusted_beat_time(middle) <= time_ms)
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

    /* DJ cue quantize floors to the beat currently under the playhead.
       It must never pull a late-in-beat cue forward into the next beat. */
    index = current_beat_index(time_ms);
    if (index >= 0)
        return clamp_playhead(adjusted_beat_time(index));

    period = beat_period_ms();
    base = grid_phase_ms + grid_offset;
    delta = (long long)time_ms - base;
    if (delta >= 0)
        beat = delta / period;
    else
        beat = (delta - period + 1) / period;
    return clamp_playhead(base + beat * period);
}

static int scrub_step_ms(void)
{
    return scrub_steps[MAX(0, MIN((int)ARRAYLEN(scrub_steps) - 1,
                                  scrub_step_index))];
}

static int loop_duration_ms(void)
{
    return MAX(1, (long long)beat_period_ms() *
                    loop_beats_x32[loop_length_index] / 32);
}

static void make_auto_loop(void)
{
    loop_in = quantized_time(playhead);
    loop_out = clamp_playhead(loop_in + loop_duration_ms());
    loop_active = loop_out > loop_in;
    overview_dirty = true;
}

static void set_loop_in(void)
{
    loop_in = quantized_time(playhead);
    if (loop_out <= loop_in)
        loop_out = clamp_playhead(loop_in + loop_duration_ms());
    loop_active = loop_out > loop_in;
    overview_dirty = true;
}

static void set_loop_out(void)
{
    loop_out = quantized_time(playhead);
    if (loop_in < 0 || loop_in >= loop_out)
        loop_in = MAX(0, loop_out - loop_duration_ms());
    loop_active = loop_out > loop_in;
    overview_dirty = true;
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
            int time_ms = adjusted_beat_time(i);
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

static void draw_loop_markers(int first, int span)
{
    int in_x;
    int out_x;
    int color;

    if (loop_in < 0 || loop_out <= loop_in)
        return;
    in_x = time_to_x(loop_in, first, span);
    out_x = time_to_x(loop_out, first, span);
    color = loop_active ? LCD_RGBPACK(255, 145, 40)
                        : LCD_RGBPACK(92, 98, 94);
    rb->lcd_set_foreground(color);
    if (in_x >= RBPREP_DECK_X &&
        in_x < RBPREP_DECK_X + RBPREP_DECK_WIDTH) {
        rb->lcd_vline(in_x, RBPREP_WAVE_TOP, RBPREP_WAVE_BOTTOM);
        rb->lcd_hline(in_x, MIN(in_x + 6,
                      RBPREP_DECK_X + RBPREP_DECK_WIDTH - 1),
                      RBPREP_WAVE_TOP + 2);
    }
    if (out_x >= RBPREP_DECK_X &&
        out_x < RBPREP_DECK_X + RBPREP_DECK_WIDTH) {
        rb->lcd_vline(out_x, RBPREP_WAVE_TOP, RBPREP_WAVE_BOTTOM);
        rb->lcd_hline(MAX(RBPREP_DECK_X, out_x - 6), out_x,
                      RBPREP_WAVE_TOP + 2);
    }
}

static void draw_loop_zone(int first, int span)
{
    int in_x;
    int out_x;
    int left;
    int right;

    if (loop_in < 0 || loop_out <= loop_in)
        return;
    in_x = time_to_x(loop_in, first, span);
    out_x = time_to_x(loop_out, first, span);
    left = MAX(RBPREP_DECK_X, in_x);
    right = MIN(RBPREP_DECK_X + RBPREP_DECK_WIDTH - 1, out_x);
    if (right < left)
        return;
    rb->lcd_set_foreground(loop_active ? LCD_RGBPACK(43, 25, 3)
                                       : LCD_RGBPACK(25, 20, 7));
    rb->lcd_fillrect(left, RBPREP_WAVE_TOP, right - left + 1,
                     RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP + 1);
}

static void rebuild_waveform_columns(int first, int span)
{
    int x;

    if (waveform_columns_valid && first == waveform_column_first &&
        span == waveform_column_span)
        return;

    for (x = 0; x < RBPREP_DECK_WIDTH; x++) {
        int begin;
        int end;
        int index;
        int peak_index;
        int peak;

        begin = first + (long long)x * span / RBPREP_DECK_WIDTH;
        end = first + (long long)(x + 1) * span / RBPREP_DECK_WIDTH;
        waveform_columns[x].valid = false;
        if (end <= 0 || begin >= waveform_points) {
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

        waveform_columns[x].amplitude = peak;
        waveform_columns[x].red = waveform[peak_index][1];
        waveform_columns[x].green = waveform[peak_index][2];
        waveform_columns[x].blue = waveform[peak_index][3];
        waveform_columns[x].valid = true;
    }
    waveform_column_first = first;
    waveform_column_span = span;
    waveform_columns_valid = true;
}

static int integer_log2(uint64_t value)
{
    int bits = 0;

    while (value > 1) {
        value >>= 1;
        bits++;
    }
    return bits;
}

/* Convert Goertzel power to a useful -60..0 dBFS display range.  A
   full-scale sine through our triangular window has approximately
   (frames * 32)^2 power after the input shift. */
static int spectrum_power_level(uint64_t power, int frames)
{
    uint64_t reference = (uint64_t)frames * frames * 1024;
    int db;

    if (!power || frames <= 0)
        return 0;
    db = (integer_log2(power) - integer_log2(reference)) * 3;
    db = MAX(-60, MIN(0, db));
    return (db + 60) * 255 / 60;
}

static int spectrum_level_db(int level)
{
    return -60 + level * 60 / 255;
}

static void update_bass_pitch(int frames)
{
    static const int note_frequency_x10[] = {
        462, 490, 519, 550, 583, 617, 654, 693, 734,
        778, 824, 873, 925, 980, 1038, 1100, 1165
    };
    int sample_rate = pcm_sample_rate ? pcm_sample_rate : 44100;
    int min_lag = MAX(1, sample_rate / 120);
    int max_lag = MIN(frames - 80, sample_rate / 45);
    uint32_t best_score = 0xffffffffu;
    uint32_t mean_amplitude = 0;
    int best_lag = 0;
    int lag;
    int i;

    if (max_lag <= min_lag || spectrum_bass < 70) {
        bass_pitch_confidence = bass_pitch_confidence * 3 / 4;
        if (bass_pitch_confidence < 20)
            bass_note_index = -1;
        return;
    }
    for (i = 0; i < frames; i += 4)
        mean_amplitude += ABS(pcm_pitch[i]);
    mean_amplitude /= MAX(1, (frames + 3) / 4);
    if (mean_amplitude < 8) {
        bass_pitch_confidence = 0;
        bass_note_index = -1;
        return;
    }

    /* Average-magnitude difference is cheap enough for the PortalPlayer and
       gives the sub display a genuinely observed pitch rather than an EQ-bin
       label.  A four-sample stride keeps this under the redraw budget. */
    for (lag = min_lag; lag <= max_lag; lag += 2) {
        uint32_t difference = 0;
        int count = 0;

        for (i = lag; i < frames; i += 4) {
            difference += ABS((int)pcm_pitch[i] - pcm_pitch[i - lag]);
            count++;
        }
        if (count > 0) {
            uint32_t score = difference / count;
            if (score < best_score) {
                best_score = score;
                best_lag = lag;
            }
        }
    }
    if (best_lag > 0) {
        int frequency_x10 = (long long)sample_rate * 10 / best_lag;
        int best_note = 0;
        int best_delta = ABS(frequency_x10 - note_frequency_x10[0]);

        bass_pitch_confidence = MAX(0, MIN(255,
            255 - (long long)best_score * 180 / MAX(1, mean_amplitude)));
        if (bass_pitch_confidence < 36) {
            bass_note_index = -1;
            return;
        }
        if (bass_frequency_x10 > 0)
            frequency_x10 = (bass_frequency_x10 * 3 + frequency_x10) / 4;
        bass_frequency_x10 = frequency_x10;
        for (i = 1; i < (int)ARRAYLEN(note_frequency_x10); i++) {
            int delta = ABS(frequency_x10 - note_frequency_x10[i]);
            if (delta < best_delta) {
                best_delta = delta;
                best_note = i;
            }
        }
        bass_note_index = best_note;
    }
}

static void update_spectrum_levels(void)
{
    static const int32_t coefficients_44100[RBPREP_SPECTRUM_BANDS] = {
        2097109, 2097068, 2096980, 2096819, 2096462, 2095822,
        2094470, 2091833, 2086431, 2075903, 2055571, 2012585,
        1932476, 1765704, 1464309, 876127, 7470, -1125224,
        -1757598, -2073729
    };
    static const int32_t coefficients_48000[RBPREP_SPECTRUM_BANDS] = {
        2097116, 2097081, 2097006, 2096871, 2096570, 2096029,
        2094888, 2092662, 2088101, 2079211, 2062035, 2025693,
        1957860, 1816187, 1558488, 1048576, 273733, -802545,
        -1482910, -1937516
    };
    const int32_t *coefficients;
    unsigned int generation;
    int capture;
    int frames;
    int i;
    int band;
    int bass_raw;

    generation = pcm_capture_generation;
    if (generation == spectrum_generation) {
        if (!(rb->audio_status() & AUDIO_STATUS_PLAY) ||
            (rb->audio_status() & AUDIO_STATUS_PAUSE)) {
            for (band = 0; band < RBPREP_SPECTRUM_BANDS; band++)
                spectrum_levels[band] = spectrum_levels[band] * 7 / 8;
            spectrum_bass = spectrum_bass * 7 / 8;
            spectrum_bass_hit = spectrum_bass_hit * 3 / 4;
        }
        return;
    }

    rb->pcm_play_lock();
    capture = pcm_capture_index;
    frames = pcm_capture_frames[capture];
    frames = MAX(0, MIN(RBPREP_PCM_FRAMES, frames));
    if (frames > 0)
        rb->memcpy(pcm_snapshot, pcm_capture[capture],
                   frames * 2 * sizeof(int16_t));
    generation = pcm_capture_generation;
    rb->pcm_play_unlock();
    spectrum_generation = generation;
    if (frames < 64)
        return;

    for (i = 0; i < frames; i++) {
        int weight = i <= frames / 2 ? i : frames - 1 - i;
        int sample = ((int)pcm_snapshot[i * 2] +
                      (int)pcm_snapshot[i * 2 + 1]) >> 1;
        /* About a 110 Hz one-pole low pass at 44.1 kHz keeps the pitch
           detector focused on the woofer range instead of upper harmonics. */
        bass_filter_state += (sample - bass_filter_state) / 64;
        pcm_pitch[i] = bass_filter_state >> 4;
        pcm_mono[i] = (sample * weight / MAX(1, frames / 2)) >> 8;
    }
    coefficients = pcm_sample_rate >= 46000
                 ? coefficients_48000 : coefficients_44100;
    for (band = 0; band < RBPREP_SPECTRUM_BANDS; band++) {
        int32_t s1 = 0;
        int32_t s2 = 0;
        int coefficient = coefficients[band];
        int raw;

        for (i = 0; i < frames; i++) {
            int32_t s0 = pcm_mono[i] +
                (int32_t)(((int64_t)coefficient * s1) >> 20) - s2;
            s2 = s1;
            s1 = s0;
        }
        {
            int64_t signed_power = (int64_t)s1 * s1 +
                (int64_t)s2 * s2 -
                (((int64_t)coefficient * s1 >> 20) * s2);
            raw = spectrum_power_level(signed_power > 0
                                     ? signed_power : 0, frames);
        }
        if (raw > spectrum_levels[band])
            spectrum_levels[band] =
                (spectrum_levels[band] + raw * 3) / 4;
        else
            spectrum_levels[band] =
                (spectrum_levels[band] * 7 + raw) / 8;
    }

    bass_raw = (spectrum_levels[0] + spectrum_levels[1] +
                spectrum_levels[2]) / 3;
    if (bass_raw > spectrum_bass + 8)
        spectrum_bass_hit = MIN(255, spectrum_bass_hit +
                                (bass_raw - spectrum_bass) * 3);
    else
        spectrum_bass_hit = spectrum_bass_hit * 3 / 4;
    spectrum_bass = (spectrum_bass * 3 + bass_raw) / 4;
    update_bass_pitch(frames);
}

static void draw_boombox(void)
{
    static const int16_t cosine[12] = {
        256, 222, 128, 0, -128, -222,
        -256, -222, -128, 0, 128, 222
    };
    static const int16_t sine[12] = {
        0, 128, 222, 256, 222, 128,
        0, -128, -222, -256, -222, -128
    };
    static const char *note_names[] = {
        "F#1", "G1", "G#1", "A1", "A#1", "B1", "C2", "C#2", "D2",
        "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2"
    };
    int bass = MIN(255, spectrum_bass + spectrum_bass_hit / 2);
    bool rattling = spectrum_bass >= 204;
    int rattle = rattling && ((*rb->current_tick / MAX(1, HZ / 20)) & 1)
               ? 1 : 0;
    int centers[2] = { 78 - rattle, 222 + rattle };
    int speaker;
    int note;
    char line[32];

    rb->lcd_set_foreground(LCD_RGBPACK(8, 12, 10));
    rb->lcd_fillrect(8 + rattle, RBPREP_WAVE_TOP + 10,
                     RBPREP_DECK_WIDTH - 16, 132);
    rb->lcd_set_foreground(rattling ? LCD_WHITE
                                   : LCD_RGBPACK(93, 108, 99));
    rb->lcd_drawrect(8 + rattle, RBPREP_WAVE_TOP + 10,
                     RBPREP_DECK_WIDTH - 16, 132);
    centered_text(0, RBPREP_DECK_WIDTH, RBPREP_WAVE_TOP + 14,
                  "SUB RESONANCE", RBPREP_GREEN);
    if (rattling) {
        rb->lcd_set_foreground(LCD_WHITE);
        rb->lcd_hline(3, 6, RBPREP_WAVE_TOP + 55 + rattle);
        rb->lcd_hline(RBPREP_DECK_WIDTH - 6, RBPREP_DECK_WIDTH - 3,
                      RBPREP_WAVE_TOP + 111 - rattle);
    }

    for (speaker = 0; speaker < 2; speaker++) {
        int cx = centers[speaker];
        int cy = RBPREP_WAVE_TOP + 78;
        int channel = speaker ? vu_right : vu_left;
        int cone = 25 + bass / 18 + MIN(5, channel / 6554);

        rb->lcd_set_foreground(LCD_RGBPACK(4, 7, 6));
        xlcd_fillcircle(cx, cy, 49);
        rb->lcd_set_foreground(LCD_RGBPACK(60, 72, 64));
        xlcd_drawcircle(cx, cy, 48);
        xlcd_drawcircle(cx, cy, 43);
        rb->lcd_set_foreground(LCD_RGBPACK(16 + bass / 12,
                                           30 + bass / 7,
                                           24 + bass / 16));
        xlcd_fillcircle(cx, cy, cone);
        rb->lcd_set_foreground(LCD_RGBPACK(74 + bass / 3,
                                           88 + bass / 5,
                                           79 + bass / 7));
        xlcd_drawcircle(cx, cy, cone);
        rb->lcd_set_foreground(LCD_RGBPACK(8, 12, 10));
        xlcd_fillcircle(cx, cy, 9 + spectrum_bass_hit / 40);

        /* Two chromatic pitch rings: F#1..B1 inside, C2..A#2 outside. */
        for (note = 0; note < (int)ARRAYLEN(note_names); note++) {
            int angle = note % 12;
            int radius = note < 6 ? 18 : 29;
            int x = cx + cosine[angle] * radius / 256;
            int y = cy + sine[angle] * radius / 256;
            bool active = note == bass_note_index &&
                          bass_pitch_confidence >= 36;

            rb->lcd_set_foreground(active ? LCD_WHITE
                                          : LCD_RGBPACK(42, 91, 61));
            if (active)
                xlcd_fillcircle(x, y, 2);
            else
                rb->lcd_drawpixel(x, y);
        }
    }

    if (bass_note_index >= 0 && bass_pitch_confidence >= 36) {
        centered_text(118, 64, RBPREP_WAVE_TOP + 51,
                      note_names[bass_note_index], LCD_WHITE);
        rb->snprintf(line, sizeof(line), "%d.%dHz",
                     bass_frequency_x10 / 10, bass_frequency_x10 % 10);
        centered_text(118, 64, RBPREP_WAVE_TOP + 69, line, RBPREP_GREEN);
    } else {
        centered_text(118, 64, RBPREP_WAVE_TOP + 51, "--", LCD_LIGHTGRAY);
        centered_text(118, 64, RBPREP_WAVE_TOP + 69,
                      "LISTEN", RBPREP_GREEN_DIM);
    }
    rb->snprintf(line, sizeof(line), "%ddB", spectrum_level_db(spectrum_bass));
    centered_text(118, 64, RBPREP_WAVE_TOP + 87, line,
                  rattling ? LCD_WHITE : LCD_RGBPACK(155, 174, 161));
    if (rattling)
        centered_text(118, 64, RBPREP_WAVE_TOP + 105,
                      "RATTLE", LCD_WHITE);
}

static void draw_twenty_band_eq(void)
{
    static const char *db_labels[] = {
        "0", "-12", "-24", "-36", "-48", "-60"
    };
    int band;
    int mark;
    const int top = RBPREP_WAVE_TOP + 8;
    const int bottom = RBPREP_WAVE_BOTTOM - 21;
    const int maximum = bottom - top;

    for (mark = 0; mark <= 5; mark++) {
        int y = top + mark * maximum / 5;
        rb->lcd_set_foreground(mark == 0 ? LCD_RGBPACK(95, 112, 101)
                                        : LCD_RGBPACK(18, 43, 28));
        rb->lcd_hline(2, 260, y);
        text(266, y - 4, db_labels[mark],
             mark == 0 ? LCD_WHITE : LCD_RGBPACK(105, 125, 112));
    }
    for (band = 0; band < RBPREP_SPECTRUM_BANDS; band++) {
        int x = 3 + band * 13;
        int height = spectrum_levels[band] * maximum / 255;
        int y;

        for (y = 0; y < maximum; y += 6) {
            bool lit = y < height;
            bool zero_cap = y >= maximum - 6 &&
                            spectrum_levels[band] >= 250;
            int color = zero_cap ? LCD_WHITE
                      : y > maximum * 4 / 5
                      ? LCD_RGBPACK(180, 255, 210)
                      : RBPREP_GREEN;
            rb->lcd_set_foreground(lit ? color
                                       : LCD_RGBPACK(9, 27, 16));
            rb->lcd_fillrect(x, bottom - y - 4, 8, 4);
        }
        if ((band % 4) == 0)
            text(x - 1, RBPREP_WAVE_BOTTOM - 12,
                 spectrum_labels[band], LCD_RGBPACK(105, 125, 112));
    }
}

static void draw_turntable(void)
{
    static const int16_t cosine[64] = {
        256,255,251,245,237,226,213,198,181,162,142,121,98,74,50,25,
        0,-25,-50,-74,-98,-121,-142,-162,-181,-198,-213,-226,-237,
        -245,-251,-255,-256,-255,-251,-245,-237,-226,-213,-198,-181,
        -162,-142,-121,-98,-74,-50,-25,0,25,50,74,98,121,142,162,
        181,198,213,226,237,245,251,255
    };
    static const int16_t sine[64] = {
        0,25,50,74,98,121,142,162,181,198,213,226,237,245,251,255,
        256,255,251,245,237,226,213,198,181,162,142,121,98,74,50,25,
        0,-25,-50,-74,-98,-121,-142,-162,-181,-198,-213,-226,-237,
        -245,-251,-255,-256,-255,-251,-245,-237,-226,-213,-198,-181,
        -162,-142,-121,-98,-74,-50,-25
    };
    static const int target_rpm_x100[] = { 3333, 4500, 7800 };
    const int cx = 86;
    const int cy = (RBPREP_WAVE_TOP + RBPREP_WAVE_BOTTOM) / 2;
    int first;
    int span;
    int phase;
    int point;
    int slot;
    int lamp_phase;
    int progress_x1000;
    int groove_radius;
    int stylus_x;
    int stylus_y;
    int arm_mid1_x;
    int arm_mid1_y;
    int arm_mid2_x;
    int arm_mid2_y;
    int pitch_slider_y;
    int previous_x = cx;
    int previous_y = cy;
    char line[48];
    char delta[16];

    viewport(&first, &span);
    phase = (long long)playhead * target_rpm_x100[played_rpm_index] * 64 /
            6000000 % 64;

    /* Black direct-drive chassis, corner screws, and inset control field. */
    rb->lcd_set_foreground(LCD_RGBPACK(4, 5, 5));
    rb->lcd_fillrect(2, RBPREP_WAVE_TOP + 3,
                     RBPREP_DECK_WIDTH - 4,
                     RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP - 5);
    rb->lcd_set_foreground(LCD_RGBPACK(60, 67, 63));
    rb->lcd_drawrect(2, RBPREP_WAVE_TOP + 3,
                     RBPREP_DECK_WIDTH - 4,
                     RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP - 5);
    rb->lcd_drawpixel(7, RBPREP_WAVE_TOP + 8);
    rb->lcd_drawpixel(RBPREP_DECK_WIDTH - 7, RBPREP_WAVE_TOP + 8);
    rb->lcd_drawpixel(7, RBPREP_WAVE_BOTTOM - 7);
    rb->lcd_drawpixel(RBPREP_DECK_WIDTH - 7, RBPREP_WAVE_BOTTOM - 7);

    rb->lcd_set_foreground(LCD_RGBPACK(7, 9, 8));
    xlcd_fillcircle(cx, cy, 69);
    rb->lcd_set_foreground(LCD_RGBPACK(125, 135, 129));
    xlcd_drawcircle(cx, cy, 69);
    xlcd_drawcircle(cx, cy, 61);
    rb->lcd_set_foreground(LCD_RGBPACK(34, 39, 36));
    xlcd_drawcircle(cx, cy, 55);
    xlcd_drawcircle(cx, cy, 48);
    xlcd_drawcircle(cx, cy, 37);
    xlcd_drawcircle(cx, cy, 27);
    xlcd_drawcircle(cx, cy, 18);

    /* Platter strobe dots rotate at the selected played RPM. */
    for (point = 0; point < 24; point++) {
        int angle = (point * 64 / 24 + phase) & 63;
        int x = cx + cosine[angle] * 65 / 256;
        int y = cy + sine[angle] * 65 / 256;

        rb->lcd_set_foreground((point + (phase >> 1)) % 6 == 0
                               ? LCD_WHITE : LCD_RGBPACK(48, 66, 55));
        if ((point + phase) % 6 == 0)
            xlcd_fillcircle(x, y, 1);
        else
            rb->lcd_drawpixel(x, y);
    }

    for (point = 0; point <= 64; point++) {
        int angle = (point + phase) & 63;
        int index = first + (long long)(point & 63) * span / 64;
        int amplitude = 0;
        int radius;
        int x;
        int y;
        int color = LCD_RGBPACK(55, 190, 125);

        if (waveform_points > 0) {
            index = MAX(0, MIN(waveform_points - 1, index));
            amplitude = waveform[index][0];
            color = LCD_RGBPACK(waveform[index][1], waveform[index][2],
                                waveform[index][3]);
        }
        radius = 34 + amplitude * 23 / 255;
        x = cx + cosine[angle] * radius / 256;
        y = cy + sine[angle] * radius / 256;
        if (point > 0) {
            rb->lcd_set_foreground(color);
            rb->lcd_drawline(previous_x, previous_y, x, y);
        }
        previous_x = x;
        previous_y = y;
    }

    rb->lcd_set_foreground(LCD_WHITE);
    xlcd_fillcircle(cx, cy, 13);

    /* Cue flags are record-relative: their timeline position chooses a groove
       angle and the platter phase rotates the whole cue map. */
    for (slot = 0; slot < 16; slot++) {
        int angle;
        int cue_progress_x1000;
        int cue_radius;
        int x;
        int y;

        if (hotcues[slot] < 0 || track_length <= 0)
            continue;
        cue_progress_x1000 = MAX(0, MIN(1000,
            (long long)hotcues[slot] * 1000 / track_length));
        cue_radius = 56 - cue_progress_x1000 * 22 / 1000;
        angle = (phase + (long long)hotcues[slot] * 64 / track_length) & 63;
        x = cx + cosine[angle] * cue_radius / 256;
        y = cy + sine[angle] * cue_radius / 256;
        draw_radial_cue_marker(cx, cy, x, y, slot,
                               cue_palette[hotcue_colors[slot] & 7]);
    }

    rb->lcd_set_foreground(LCD_RGBPACK(225, 230, 226));
    xlcd_fillcircle(cx, cy, 4);

    /* A real tonearm moves inward over the full side.  The stylus follows a
       fixed pickup angle while its groove radius falls from 56 to 34 pixels. */
    progress_x1000 = track_length > 0
                   ? MAX(0, MIN(1000,
                       (long long)playhead * 1000 / track_length)) : 0;
    groove_radius = 56 - progress_x1000 * 22 / 1000;
    stylus_x = cx + cosine[7] * groove_radius / 256;
    stylus_y = cy + sine[7] * groove_radius / 256;
    arm_mid1_x = (192 * 2 + stylus_x) / 3 + 5;
    arm_mid1_y = ((RBPREP_WAVE_TOP + 30) * 2 + stylus_y) / 3;
    arm_mid2_x = (192 + stylus_x * 2) / 3 - 5;
    arm_mid2_y = (RBPREP_WAVE_TOP + 30 + stylus_y * 2) / 3;
    rb->lcd_set_foreground(LCD_RGBPACK(178, 186, 181));
    rb->lcd_drawline(192, RBPREP_WAVE_TOP + 30, arm_mid1_x, arm_mid1_y);
    rb->lcd_drawline(arm_mid1_x, arm_mid1_y, arm_mid2_x, arm_mid2_y);
    rb->lcd_drawline(arm_mid2_x, arm_mid2_y, stylus_x, stylus_y);
    rb->lcd_set_foreground(LCD_WHITE);
    rb->lcd_drawline(stylus_x - 2, stylus_y - 1,
                     stylus_x + 2, stylus_y + 2);
    rb->lcd_drawpixel(stylus_x, stylus_y + 3);
    rb->lcd_set_foreground(LCD_RGBPACK(115, 124, 119));
    xlcd_fillcircle(192, RBPREP_WAVE_TOP + 30, 10);
    rb->lcd_set_foreground(LCD_BLACK);
    xlcd_fillcircle(192, RBPREP_WAVE_TOP + 30, 4);
    rb->lcd_set_foreground(LCD_RGBPACK(115, 124, 119));
    rb->lcd_drawline(198, RBPREP_WAVE_TOP + 22,
                     209, RBPREP_WAVE_TOP + 10);
    rb->lcd_fillrect(205, RBPREP_WAVE_TOP + 7, 11, 7);

    lamp_phase = (long long)*rb->current_tick *
                 target_rpm_x100[played_rpm_index] * 8 /
                 (HZ * 6000);
    rb->lcd_set_foreground((rb->audio_status() & AUDIO_STATUS_PLAY) &&
                           !(rb->audio_status() & AUDIO_STATUS_PAUSE)
                           ? ((lamp_phase & 1) ? LCD_RGBPACK(255, 60, 45)
                                               : LCD_RGBPACK(120, 12, 8))
                           : LCD_RGBPACK(45, 9, 7));
    rb->lcd_vline(164, RBPREP_WAVE_TOP + 7, RBPREP_WAVE_TOP + 19);
    xlcd_fillcircle(164, RBPREP_WAVE_TOP + 20, 3);
    if (lamp_phase & 1)
        xlcd_drawcircle(164, RBPREP_WAVE_TOP + 20, 5);

    /* Rectangular transport in the lower-left deck corner. */
    {
        bool playing = (rb->audio_status() & AUDIO_STATUS_PLAY) &&
                       !(rb->audio_status() & AUDIO_STATUS_PAUSE);
        int button_y = RBPREP_WAVE_BOTTOM - 22;

        rb->lcd_set_foreground(playing ? LCD_RGBPACK(12, 49, 29)
                                      : LCD_RGBPACK(11, 14, 12));
        rb->lcd_fillrect(8, button_y, 42, 17);
        rb->lcd_set_foreground(RBPREP_GREEN);
        rb->lcd_drawrect(8, button_y, 42, 17);
        rb->lcd_fillrect(8, button_y, 4, 17);
        rb->lcd_set_foreground(LCD_WHITE);
        if (playing) {
            rb->lcd_fillrect(26, button_y + 4, 3, 9);
            rb->lcd_fillrect(33, button_y + 4, 3, 9);
        } else {
            xlcd_filltriangle(25, button_y + 3,
                              25, button_y + 13,
                              36, button_y + 8);
        }
    }

    /* Familiar three-speed selector. */
    for (point = 0; point < 3; point++) {
        int x = 158 + point * 20;
        bool selected = point == played_rpm_index;
        rb->lcd_set_foreground(selected ? RBPREP_GREEN
                                       : LCD_RGBPACK(40, 52, 44));
        rb->lcd_fillrect(x, RBPREP_WAVE_BOTTOM - 12, 16, 7);
        text(x + 1, RBPREP_WAVE_BOTTOM - 12, rpm_styles[point],
             selected ? LCD_BLACK : LCD_RGBPACK(145, 158, 149));
    }

    /* The pitch-bend tool owns this fader; the white tick is exact zero. */
    rb->lcd_set_foreground(LCD_RGBPACK(65, 75, 69));
    rb->lcd_vline(288, RBPREP_WAVE_TOP + 31, RBPREP_WAVE_BOTTOM - 18);
    rb->lcd_hline(284, 292, RBPREP_WAVE_TOP + 75);
    rb->lcd_set_foreground(LCD_WHITE);
    rb->lcd_hline(282, 294, RBPREP_WAVE_TOP + 75);
    pitch_slider_y = RBPREP_WAVE_TOP + 75 -
                     pitch_bend_x100 * 42 / 1600;
    rb->lcd_set_foreground(RBPREP_GREEN);
    rb->lcd_fillrect(282, pitch_slider_y - 2, 13, 5);

    /* Keep the readout to the right of the tonearm's entire sweep. */
    text(218, RBPREP_WAVE_TOP + 7, "QUARTZ DD", RBPREP_GREEN);
    rb->snprintf(line, sizeof(line), "%s>%s RPM",
                 rpm_styles[host_rpm_index], rpm_styles[played_rpm_index]);
    text(212, RBPREP_WAVE_TOP + 26, line, LCD_WHITE);
    format_percent_delta(delta, sizeof(delta),
                         deck_pitch_x100() - PITCH_SPEED_100);
    rb->snprintf(line, sizeof(line), "P %s%%", delta);
    text(212, RBPREP_WAVE_TOP + 45, line, LCD_WHITE);
    format_percent_delta(delta, sizeof(delta), pitch_bend_x100);
    rb->snprintf(line, sizeof(line), "B %s%%", delta);
    text(212, RBPREP_WAVE_TOP + 64, line, RBPREP_GREEN);
    format_percent_delta(delta, sizeof(delta),
                         tempo_x100 - PITCH_SPEED_100);
    rb->snprintf(line, sizeof(line), "T %s%%", delta);
    text(212, RBPREP_WAVE_TOP + 83, line, RBPREP_GREEN);
    format_percent_delta(delta, sizeof(delta),
                         deck_speed_x100() - PITCH_SPEED_100);
    rb->snprintf(line, sizeof(line), "R %s%%", delta);
    text(212, RBPREP_WAVE_TOP + 102, line,
         LCD_RGBPACK(155, 174, 161));
}

static void draw_waveform(void)
{
    int first;
    int span;
    int x;
    int mid = (RBPREP_SIGNAL_TOP + RBPREP_SIGNAL_BOTTOM) / 2;

    if (visualizer_mode == 1 || visualizer_mode == 2)
        update_spectrum_levels();
    if (visualizer_mode == 1) {
        draw_boombox();
        return;
    } else if (visualizer_mode == 2) {
        draw_twenty_band_eq();
        return;
    } else if (visualizer_mode == 3) {
        draw_turntable();
        return;
    }
    viewport(&first, &span);
    draw_loop_zone(first, span);
    if (waveform_points > 0)
        rebuild_waveform_columns(first, span);
    for (x = 0; x < RBPREP_DECK_WIDTH; x++) {
        struct rbprep_wave_column *column = &waveform_columns[x];
        int screen_x = RBPREP_DECK_X + x;
        int height;
        int color;

        if (waveform_points <= 0) {
            rb->lcd_set_foreground(LCD_DARKGRAY);
            if (waveform_half)
                rb->lcd_vline(screen_x, RBPREP_SIGNAL_BOTTOM - 5,
                              RBPREP_SIGNAL_BOTTOM - 2);
            else
                rb->lcd_vline(screen_x, mid - 3, mid + 3);
            continue;
        }
        if (!column->valid) {
            rb->lcd_set_foreground(LCD_RGBPACK(15, 23, 17));
            if (waveform_half)
                rb->lcd_vline(screen_x, RBPREP_SIGNAL_BOTTOM - 3,
                              RBPREP_SIGNAL_BOTTOM - 2);
            else
                rb->lcd_vline(screen_x, mid - 1, mid + 1);
            continue;
        }

        height = waveform_height_lut[column->amplitude];
        color = LCD_RGBPACK(column->red, column->green, column->blue);
        rb->lcd_set_foreground(color);
        if (waveform_half)
            rb->lcd_vline(screen_x, RBPREP_SIGNAL_BOTTOM - height,
                          RBPREP_SIGNAL_BOTTOM - 1);
        else
            rb->lcd_vline(screen_x, mid - height, mid + height);
    }

    draw_beatgrid(first, span);
    draw_loop_markers(first, span);
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
        draw_overview_cue_marker(cue_x, x,
                                 cue_palette[hotcue_colors[x] & 7]);
    }

    if (loop_in >= 0 && loop_out > loop_in) {
        int loop_x = RBPREP_OVERVIEW_X +
            (long long)loop_in * (RBPREP_OVERVIEW_WIDTH - 1) /
            MAX(1, track_length);
        int loop_end_x = RBPREP_OVERVIEW_X +
            (long long)loop_out * (RBPREP_OVERVIEW_WIDTH - 1) /
            MAX(1, track_length);
        rb->lcd_set_foreground(loop_active ? LCD_RGBPACK(255, 145, 40)
                                           : LCD_RGBPACK(92, 98, 94));
        rb->lcd_hline(loop_x, loop_end_x,
                      RBPREP_OVERVIEW_Y + RBPREP_OVERVIEW_HEIGHT - 2);
        rb->lcd_vline(loop_x,
                      RBPREP_OVERVIEW_Y + RBPREP_OVERVIEW_HEIGHT - 5,
                      RBPREP_OVERVIEW_Y + RBPREP_OVERVIEW_HEIGHT - 1);
        rb->lcd_vline(loop_end_x,
                      RBPREP_OVERVIEW_Y + RBPREP_OVERVIEW_HEIGHT - 5,
                      RBPREP_OVERVIEW_Y + RBPREP_OVERVIEW_HEIGHT - 1);
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

static void draw_vu_lozenge(int center_x, int top)
{
    rb->lcd_drawpixel(center_x, top);
    rb->lcd_hline(center_x - 1, center_x + 1, top + 1);
    rb->lcd_hline(center_x - 2, center_x + 2, top + 2);
    rb->lcd_hline(center_x - 2, center_x + 2, top + 3);
    rb->lcd_hline(center_x - 2, center_x + 2, top + 4);
    rb->lcd_hline(center_x - 1, center_x + 1, top + 5);
    rb->lcd_drawpixel(center_x, top + 6);
}

static void draw_vu_meter(void)
{
    const int segments = 14;
    const int segment_pitch = 9;
    int lit_left = MIN(segments, (int)(vu_left * segments / 32768));
    int lit_right = MIN(segments, (int)(vu_right * segments / 32768));
    int segment;
    int x = RBPREP_DECK_X + RBPREP_DECK_WIDTH + 5;

    rb->lcd_set_foreground(LCD_RGBPACK(6, 13, 9));
    rb->lcd_fillrect(RBPREP_DECK_X + RBPREP_DECK_WIDTH + 1,
                     RBPREP_WAVE_TOP, RBPREP_VU_WIDTH,
                     RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP + 1);
    for (segment = 0; segment < segments; segment++) {
        int y = RBPREP_SIGNAL_BOTTOM - segment * segment_pitch - 7;
        int active_color = segment >= 12 ? LCD_WHITE
                         : segment >= 9 ? LCD_RGBPACK(255, 205, 55)
                         : LCD_RGBPACK(55, 225, 105);
        rb->lcd_set_foreground(segment < lit_left
                              ? active_color : LCD_RGBPACK(28, 39, 31));
        draw_vu_lozenge(x, y);
        rb->lcd_set_foreground(segment < lit_right
                              ? active_color : LCD_RGBPACK(28, 39, 31));
        draw_vu_lozenge(x + 10, y);
    }
}

static void clear_analysis(void)
{
    int i;

    waveform_points = 0;
    waveform_columns_valid = false;
    rb->memset(overview_waveform, 0, sizeof(overview_waveform));
    waveform_duration_ms = 1;
    beat_count = 0;
    grid_offset = 0;
    grid_phase_ms = 0;
    grid_beat_shift = 0;
    deck_cue = -1;
    loop_in = -1;
    loop_out = -1;
    loop_active = false;
    cue_audition_active = false;
    cue_audition_latched = false;
    for (i = 0; i < 16; i++) {
        hotcues[i] = -1;
        hotcue_colors[i] = 3;
    }
    overview_dirty = true;
}

static bool valid_edit_record(const unsigned char *data)
{
    return !rb->memcmp(data, "RBE1", 4) &&
           read_u16(data + 4) == RBPREP_EDIT_RECORD_SIZE &&
           read_u16(data + 6) == 1;
}

static void apply_edit_record(const unsigned char *data)
{
    int i;

    rating = MIN(5, data[16]);
    color_index = data[17] & 7;
    quantize = !!data[18];
    grid_beat_shift = data[19] & 3;
    track_year = read_u16(data + 20);
    grid_bpm_x100 = MAX(3000, MIN(30000, (int)read_u32(data + 24)));
    grid_phase_ms = (int32_t)read_u32(data + 28);
    grid_offset = (int32_t)read_u32(data + 32);
    deck_cue = (int32_t)read_u32(data + 36);
    for (i = 0; i < 16; i++) {
        hotcues[i] = (int32_t)read_u32(data + 40 + i * 4);
        if (hotcues[i] >= 0)
            hotcues[i] = clamp_playhead(hotcues[i]);
        hotcue_colors[i] = data[104 + i] & 7;
    }
    rb->memcpy(selected_genre, data + 120, sizeof(selected_genre));
    selected_genre[sizeof(selected_genre) - 1] = '\0';
    overview_dirty = true;
}

static bool load_latest_edit(int track_id)
{
    unsigned char data[RBPREP_EDIT_RECORD_SIZE];
    unsigned char latest[RBPREP_EDIT_RECORD_SIZE];
    bool found = false;
    int fd = rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);

    if (fd < 0)
        return false;
    while (rb->read(fd, data, sizeof(data)) == sizeof(data)) {
        if (valid_edit_record(data) && (int)read_u32(data + 8) == track_id) {
            rb->memcpy(latest, data, sizeof(latest));
            found = true;
        }
    }
    rb->close(fd);
    if (found)
        apply_edit_record(latest);
    return found;
}

static bool save_edit_snapshot(void)
{
    unsigned char data[RBPREP_EDIT_RECORD_SIZE];
    int fd;
    int i;

    if (selected_track_id < 0)
        return false;
    rb->memset(data, 0, sizeof(data));
    rb->memcpy(data, "RBE1", 4);
    write_u16(data + 4, RBPREP_EDIT_RECORD_SIZE);
    write_u16(data + 6, 1);
    write_u32(data + 8, selected_track_id);
    write_u32(data + 12, *rb->current_tick);
    data[16] = rating;
    data[17] = color_index;
    data[18] = quantize;
    data[19] = grid_beat_shift;
    write_u16(data + 20, track_year);
    write_u32(data + 24, grid_bpm_x100);
    write_u32(data + 28, grid_phase_ms);
    write_u32(data + 32, grid_offset);
    write_u32(data + 36, deck_cue);
    for (i = 0; i < 16; i++) {
        write_u32(data + 40 + i * 4, hotcues[i]);
        data[104 + i] = hotcue_colors[i];
    }
    rb->strlcpy((char *)data + 120, selected_genre, 32);
    rb->strlcpy((char *)data + 152, selected_title, 64);

    fd = rb->open(RBPREP_EDIT_JOURNAL,
                  O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return false;
    if (rb->write(fd, data, sizeof(data)) != sizeof(data)) {
        rb->close(fd);
        return false;
    }
    rb->close(fd);
    return true;
}

static bool flush_deferred_edit(void)
{
    if (!track_edit_dirty)
        return true;
    if (!save_edit_snapshot())
        return false;
    track_edit_dirty = false;
    return true;
}

static bool record_edit_change(void)
{
    track_edit_dirty = true;
    if (save_on_track_load)
        return true;
    return flush_deferred_edit();
}

static void read_burn_offsets(uint32_t *edit_offset,
                              uint32_t *playlist_offset)
{
    unsigned char data[12];
    int fd;

    *edit_offset = *playlist_offset = 0;
    fd = rb->open(RBPREP_BURN_STATE, O_RDONLY);
    if (fd < 0)
        return;
    if (rb->read(fd, data, sizeof(data)) == sizeof(data) &&
        !rb->memcmp(data, "RBL1", 4)) {
        *edit_offset = read_u32(data + 4);
        *playlist_offset = read_u32(data + 8);
    }
    rb->close(fd);
}

static bool write_burn_offsets(uint32_t edit_offset,
                               uint32_t playlist_offset)
{
    unsigned char data[12];
    int fd;

    rb->memcpy(data, "RBL1", 4);
    write_u32(data + 4, edit_offset);
    write_u32(data + 8, playlist_offset);
    fd = rb->open(RBPREP_BURN_STATE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    if (rb->write(fd, data, sizeof(data)) != sizeof(data)) {
        rb->close(fd);
        return false;
    }
    rb->close(fd);
    return true;
}

static void refresh_pending_summary(void)
{
    unsigned char data[RBPREP_EDIT_RECORD_SIZE];
    char line[160];
    int fd;
    int i;
    int length;
    uint32_t edit_offset;
    uint32_t playlist_offset;

    read_burn_offsets(&edit_offset, &playlist_offset);

    pending_snapshot_count = 0;
    pending_playlist_count = 0;
    pending_visible_count = 0;
    fd = rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);
    if (fd >= 0) {
        if (edit_offset > (uint32_t)rb->filesize(fd) ||
            edit_offset % RBPREP_EDIT_RECORD_SIZE)
            edit_offset = 0;
        if (edit_offset > 0)
            rb->lseek(fd, edit_offset, SEEK_SET);
        while (rb->read(fd, data, sizeof(data)) == sizeof(data)) {
            struct rbprep_pending_entry *entry;
            int slot = -1;

            if (!valid_edit_record(data))
                continue;
            for (i = 0; i < pending_snapshot_count; i++) {
                if (pending_entries[i].track_id == read_u32(data + 8)) {
                    slot = i;
                    break;
                }
            }
            if (slot >= 0) {
                struct rbprep_pending_entry existing = pending_entries[slot];
                for (; slot + 1 < pending_snapshot_count; slot++)
                    pending_entries[slot] = pending_entries[slot + 1];
                pending_entries[pending_snapshot_count - 1] = existing;
                slot = pending_snapshot_count - 1;
            } else if (pending_snapshot_count < RBPREP_PENDING_MAX) {
                slot = pending_snapshot_count++;
            } else {
                continue;
            }
            entry = &pending_entries[slot];
            entry->track_id = read_u32(data + 8);
            entry->saved_tick = read_u32(data + 12);
            entry->rating = MIN(5, data[16]);
            entry->color = data[17] & 7;
            entry->bpm_x100 = read_u32(data + 24);
            rb->memcpy(entry->title, data + 152, sizeof(entry->title));
            entry->title[sizeof(entry->title) - 1] = '\0';
        }
        rb->close(fd);
    }
    fd = rb->open(RBPREP_PLAYLIST_JOURNAL, O_RDONLY);
    if (fd >= 0) {
        if (playlist_offset <= (uint32_t)rb->filesize(fd)) {
            rb->lseek(fd, playlist_offset, SEEK_SET);
        } else {
            playlist_offset = 0;
        }
        length = 0;
        while (true) {
            unsigned char value;
            int got = rb->read(fd, &value, 1);
            if (got != 1 && length == 0)
                break;
            if (got != 1 || value == '\n') {
                char *first;
                char *second;
                uint32_t track;
                uint32_t playlist;
                int slot = -1;

                line[length] = '\0';
                first = rb->strchr(line, '\t');
                second = first ? rb->strchr(first + 1, '\t') : NULL;
                if (first && second) {
                    *first = *second = '\0';
                    track = rb->strtoul(line, NULL, 10);
                    playlist = rb->strtoul(first + 1, NULL, 10);
                    for (i = 0; i < pending_playlist_count; i++) {
                        if (pending_playlists[i].track_id == track &&
                            pending_playlists[i].playlist_id == playlist) {
                            slot = i;
                            break;
                        }
                    }
                    if (slot < 0 &&
                        pending_playlist_count < RBPREP_PENDING_MAX)
                        slot = pending_playlist_count++;
                    if (slot >= 0) {
                        pending_playlists[slot].track_id = track;
                        pending_playlists[slot].playlist_id = playlist;
                        rb->strlcpy(pending_playlists[slot].name, second + 1,
                                    sizeof(pending_playlists[slot].name));
                    }
                }
                length = 0;
                if (got != 1)
                    break;
            } else if (value != '\r' &&
                       length + 1 < (int)sizeof(line)) {
                line[length++] = value;
            }
        }
        rb->close(fd);
    }
    pending_visible_count = MIN(pending_snapshot_count +
                                pending_playlist_count,
                                RBPREP_PENDING_ROWS);
    pending_selection = MAX(0, MIN(pending_snapshot_count +
                                   pending_playlist_count - 1,
                                   pending_selection));
    pending_top = MAX(0, MIN(MAX(0, pending_snapshot_count +
                                pending_playlist_count -
                                RBPREP_PENDING_ROWS), pending_top));
}

static bool delete_pending_edit(uint32_t track_id)
{
    const char *temporary = RBPREP_EDIT_JOURNAL ".rbprep-new";
    const char *previous = RBPREP_EDIT_JOURNAL ".rbprep-prev";
    unsigned char data[RBPREP_EDIT_RECORD_SIZE];
    uint32_t edit_offset;
    uint32_t playlist_offset;
    int input;
    int output;

    read_burn_offsets(&edit_offset, &playlist_offset);
    (void)playlist_offset;
    input = rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);
    if (input < 0)
        return false;
    if (edit_offset > (uint32_t)rb->filesize(input) ||
        edit_offset % RBPREP_EDIT_RECORD_SIZE)
        edit_offset = 0;
    rb->remove(temporary);
    output = rb->open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output < 0) {
        rb->close(input);
        return false;
    }
    while (rb->read(input, data, sizeof(data)) == sizeof(data)) {
        off_t after = rb->lseek(input, 0, SEEK_CUR);
        bool pending = after >= (off_t)sizeof(data) &&
                       (uint32_t)(after - sizeof(data)) >= edit_offset;
        if (pending && valid_edit_record(data) &&
            read_u32(data + 8) == track_id)
            continue;
        if (rb->write(output, data, sizeof(data)) != sizeof(data)) {
            rb->close(input);
            rb->close(output);
            rb->remove(temporary);
            return false;
        }
    }
    rb->close(input);
    rb->close(output);
    rb->remove(previous);
    if (rb->rename(RBPREP_EDIT_JOURNAL, previous) < 0)
        return false;
    if (rb->rename(temporary, RBPREP_EDIT_JOURNAL) < 0) {
        rb->rename(previous, RBPREP_EDIT_JOURNAL);
        return false;
    }
    rb->remove(previous);
    return true;
}

static bool delete_pending_playlist(uint32_t track_id, uint32_t playlist_id)
{
    const char *temporary = RBPREP_PLAYLIST_JOURNAL ".rbprep-new";
    const char *previous = RBPREP_PLAYLIST_JOURNAL ".rbprep-prev";
    char line[160];
    uint32_t edit_offset;
    uint32_t playlist_offset;
    uint32_t position = 0;
    int input;
    int output;
    int length = 0;

    read_burn_offsets(&edit_offset, &playlist_offset);
    (void)edit_offset;
    input = rb->open(RBPREP_PLAYLIST_JOURNAL, O_RDONLY);
    if (input < 0)
        return false;
    if (playlist_offset > (uint32_t)rb->filesize(input))
        playlist_offset = 0;
    rb->remove(temporary);
    output = rb->open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output < 0) {
        rb->close(input);
        return false;
    }
    while (true) {
        unsigned char value;
        int got = rb->read(input, &value, 1);

        if (got != 1 && length == 0)
            break;
        if (position < playlist_offset && got == 1) {
            if (rb->write(output, &value, 1) != 1)
                goto playlist_delete_failed;
            position++;
            continue;
        }
        if (got != 1 || value == '\n') {
            char parse[160];
            char *first;
            char *second;
            bool remove = false;

            line[length] = '\0';
            rb->strlcpy(parse, line, sizeof(parse));
            first = rb->strchr(parse, '\t');
            second = first ? rb->strchr(first + 1, '\t') : NULL;
            if (first && second) {
                *first = *second = '\0';
                remove = rb->strtoul(parse, NULL, 10) == track_id &&
                         rb->strtoul(first + 1, NULL, 10) == playlist_id;
            }
            if (!remove) {
                if ((length > 0 &&
                     rb->write(output, line, length) != length) ||
                    rb->write(output, "\n", 1) != 1)
                    goto playlist_delete_failed;
            }
            length = 0;
            if (got != 1)
                break;
        } else if (value != '\r' &&
                   length + 1 < (int)sizeof(line)) {
            line[length++] = value;
        }
        if (got == 1)
            position++;
    }
    rb->close(input);
    rb->close(output);
    rb->remove(previous);
    if (rb->rename(RBPREP_PLAYLIST_JOURNAL, previous) < 0)
        return false;
    if (rb->rename(temporary, RBPREP_PLAYLIST_JOURNAL) < 0) {
        rb->rename(previous, RBPREP_PLAYLIST_JOURNAL);
        return false;
    }
    rb->remove(previous);
    return true;

playlist_delete_failed:
    rb->close(input);
    rb->close(output);
    rb->remove(temporary);
    return false;
}

#include "rbprep_burn.h"

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
    grid_source_bpm_x100 = grid_bpm_x100;
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
    /* Some exports contain an occasional missing or malformed beat number.
       Preserve the first known phase, then advance by the number of elapsed
       beat periods so the HUD cannot jump directly from 2 to 4. */
    for (i = 1; i < beat_count; i++) {
        int source_period = grid_source_bpm_x100 > 0
                          ? MAX(1, 6000000 / grid_source_bpm_x100) : 500;
        int delta = MAX(1, beat_times[i] - beat_times[i - 1]);
        int steps = MAX(1, MIN(8,
            (delta + source_period / 2) / source_period));
        beat_numbers[i] = ((beat_numbers[i - 1] - 1 + steps) & 3) + 1;
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
        track_row_count = search_active ? search_result_count
                                        : (int)library_track_count;
    } else if (read_node_record(playlist_node, &node)) {
        track_row_count = node.member_count;
    } else {
        track_row_count = 0;
    }
    mode = MODE_TRACKS;
    force_full_redraw = true;
}

static bool play_track_index(int index, int row,
                             enum rbprep_mode return_mode)
{
    struct rbprep_track_record track;
    struct mp3entry *id3;
    char path[MAX_PATH];
    const char *extension;
    bool already_loaded;

    if (!read_track_record(index, &track) ||
        !read_index_string(track.path_offset, path, sizeof(path)))
        return false;
    if (index != selected_track_index && !flush_deferred_edit()) {
        rb->splash(HZ * 2, "Could not save previous edits");
        restore_black_canvas();
        return false;
    }
    if (!rb->file_exists(path)) {
        rb->splashf(HZ * 2, "Missing: %s", path);
        restore_black_canvas();
        return false;
    }

    id3 = rb->audio_current_track();
    already_loaded = id3 && id3->path && !rb->strcmp(id3->path, path);
    if (index == selected_track_index && id3 && id3->path &&
        !rb->strcmp(id3->path, path)) {
        playhead = clamp_playhead(id3->elapsed);
        playing_track_row = row;
        deck_return_mode = return_mode;
        mode = MODE_DECK;
        reset_play_clock(playhead, *rb->current_tick);
        force_full_redraw = true;
        return true;
    }

    if (host_rpm_index != 0 || played_rpm_index != 0 ||
        pitch_bend_x100 != 0 || tempo_x100 != PITCH_SPEED_100) {
        host_rpm_index = played_rpm_index = 0;
        pitch_bend_x100 = 0;
        tempo_x100 = PITCH_SPEED_100;
        apply_playback_rate();
    }
    if (!already_loaded) {
        stop_editor_audio();
        if (rb->playlist_create(NULL, NULL) < 0 ||
            rb->playlist_insert_track(NULL, path, PLAYLIST_INSERT_LAST,
                                      false, true) < 0) {
            rb->splash(HZ * 2, "Could not load track");
            restore_black_canvas();
            return false;
        }
    }

    selected_track_index = index;
    selected_track_id = track.id;
    title_scroll_px = metadata_scroll_px = 0;
    extension = rb->strrchr(path, '.');
    rb->strlcpy(selected_extension, extension ? extension : "",
                sizeof(selected_extension));
    read_index_string(track.title_offset, selected_title,
                      sizeof(selected_title));
    read_index_string(track.artist_offset, selected_artist,
                      sizeof(selected_artist));
    read_index_string(track.genre_offset, selected_genre,
                      sizeof(selected_genre));
    if (track.key_offset)
        read_index_string(track.key_offset, selected_key,
                          sizeof(selected_key));
    else
        selected_key[0] = '\0';
    grid_bpm_x100 = track.bpm_x100;
    grid_source_bpm_x100 = grid_bpm_x100;
    rating = track.rating;
    color_index = track.color & 7;
    track_year = track.year;
    load_waveform(track.id);
    load_latest_edit(track.id);
    track_edit_dirty = false;
    if (already_loaded && id3) {
        if (id3->length > 0)
            track_length = id3->length;
        playhead = clamp_playhead(id3->elapsed);
    } else {
        playhead = 0;
        rb->playlist_start(0, 0, 0);
    }
    reset_play_clock(playhead, *rb->current_tick);
    playing_track_row = row;
    audio_was_running = true;
    deck_return_mode = return_mode;
    mode = MODE_DECK;
    force_full_redraw = true;
    return true;
}

static bool open_loaded_track(void)
{
    struct mp3entry *id3 = rb->audio_current_track();
    struct rbprep_track_record track;
    char path[MAX_PATH];
    unsigned int index;

    if (!id3 || !id3->path || !id3->path[0]) {
        rb->splash(HZ * 2, "No track is loaded");
        restore_black_canvas();
        return false;
    }
    for (index = 0; index < library_track_count; index++) {
        if (read_track_record(index, &track) &&
            read_index_string(track.path_offset, path, sizeof(path)) &&
            !rb->strcmp(path, id3->path))
            return play_track_index(index, -1, MODE_LIBRARY);
    }
    rb->splash(HZ * 2, "Loaded track is not in RBPrep index");
    restore_black_canvas();
    return false;
}

static bool play_track_row(int row)
{
    return play_track_index(track_index_at_row(row), row, MODE_TRACKS);
}

static void draw_record_badge(int cx, int cy, int radius, int color,
                              bool selected)
{
    rb->lcd_set_foreground(selected ? color : LCD_RGBPACK(24, 35, 28));
    xlcd_fillcircle(cx, cy, radius);
    rb->lcd_set_foreground(LCD_BLACK);
    xlcd_fillcircle(cx, cy, MAX(1, radius - 2));
    rb->lcd_set_foreground(selected ? LCD_WHITE
                                    : LCD_RGBPACK(100, 120, 107));
    xlcd_fillcircle(cx, cy, 1);
}

static void draw_blade_shell(const char *title, int accent)
{
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(0, 0, LCD_WIDTH, LCD_HEIGHT);
    rb->lcd_set_foreground(LCD_RGBPACK(4, 13, 8));
    rb->lcd_fillrect(0, 0, LCD_WIDTH, 22);
    rb->lcd_set_foreground(accent);
    rb->lcd_fillrect(0, 0, 5, LCD_HEIGHT);
    xlcd_drawcircle(LCD_WIDTH - 10, 9, 27);
    xlcd_drawcircle(LCD_WIDTH - 10, 9, 20);
    xlcd_fillcircle(LCD_WIDTH - 10, 9, 3);
    text(10, 4, title, LCD_WHITE);
}

static void draw_blade_selection(int y, int height, int color)
{
    const int left = 7;
    const int right = LCD_WIDTH - 7;
    int radius = height / 2;
    int center_y = y + radius;
    (void)color;

    rb->lcd_set_foreground(LCD_RGBPACK(12, 49, 29));
    xlcd_fillcircle(left + radius, center_y, radius);
    xlcd_fillcircle(right - radius, center_y, radius);
    rb->lcd_fillrect(left + radius, y,
                     right - left - radius * 2 + 1, height + 1);
    rb->lcd_set_foreground(RBPREP_GREEN);
    rb->lcd_hline(left + radius, right - radius, y + 1);
    rb->lcd_drawpixel(left + 2, center_y);
    rb->lcd_drawpixel(right - 2, center_y);
}

static void draw_playlist_node_glyph(int cx, int cy, bool folder,
                                     bool selected)
{
    int color = selected ? LCD_WHITE : RBPREP_GREEN;

    rb->lcd_set_foreground(color);
    if (folder) {
        rb->lcd_fillrect(cx - 6, cy - 3, 13, 8);
        rb->lcd_fillrect(cx - 4, cy - 5, 6, 3);
        rb->lcd_set_foreground(selected ? LCD_RGBPACK(12, 49, 29)
                                        : LCD_BLACK);
        rb->lcd_hline(cx - 4, cx + 5, cy + 2);
    } else {
        rb->lcd_hline(cx - 6, cx - 1, cy - 4);
        rb->lcd_hline(cx - 6, cx - 2, cy - 1);
        rb->lcd_hline(cx - 6, cx - 3, cy + 2);
        rb->lcd_vline(cx + 4, cy - 5, cy + 3);
        rb->lcd_hline(cx + 1, cx + 6, cy - 5);
        xlcd_fillcircle(cx + 1, cy + 4, 2);
    }
}

static void draw_main_glyph(int cx, int cy, int item, bool selected)
{
    int color = selected ? LCD_WHITE : RBPREP_GREEN;

    rb->lcd_set_foreground(color);
    xlcd_drawcircle(cx, cy, 7);
    if (item == 0) {
        xlcd_drawcircle(cx, cy, 4);
        xlcd_fillcircle(cx, cy, 1);
    } else if (item == 1) {
        draw_playlist_node_glyph(cx, cy, true, selected);
    } else if (item == 2) {
        xlcd_drawcircle(cx - 1, cy, 4);
        xlcd_fillcircle(cx - 1, cy, 1);
        rb->lcd_drawline(cx + 5, cy - 5, cx + 2, cy + 3);
    } else if (item == 3) {
        rb->lcd_vline(cx, cy - 5, cy + 4);
        rb->lcd_drawline(cx, cy - 3, cx - 4, cy - 1);
        rb->lcd_drawline(cx, cy, cx + 4, cy - 2);
        rb->lcd_fillrect(cx - 5, cy - 2, 2, 2);
        xlcd_fillcircle(cx + 4, cy - 2, 1);
        xlcd_fillcircle(cx, cy + 5, 1);
    } else if (item == 4) {
        rb->lcd_vline(cx - 4, cy - 5, cy + 5);
        rb->lcd_vline(cx, cy - 5, cy + 5);
        rb->lcd_vline(cx + 4, cy - 5, cy + 5);
        rb->lcd_fillrect(cx - 5, cy - 2, 3, 3);
        rb->lcd_fillrect(cx - 1, cy + 1, 3, 3);
        rb->lcd_fillrect(cx + 3, cy - 4, 3, 3);
    } else if (item == 5) {
        rb->lcd_drawline(cx - 5, cy, cx - 1, cy + 4);
        rb->lcd_drawline(cx - 1, cy + 4, cx + 5, cy - 5);
    } else if (item == 6) {
        rb->lcd_hline(cx - 4, cx + 4, cy - 4);
        rb->lcd_hline(cx - 4, cx + 4, cy);
        rb->lcd_hline(cx - 4, cx + 4, cy + 4);
        rb->lcd_drawpixel(cx - 6, cy - 4);
        rb->lcd_drawpixel(cx - 6, cy);
        rb->lcd_drawpixel(cx - 6, cy + 4);
    } else {
        rb->lcd_vline(cx - 4, cy - 5, cy + 5);
        rb->lcd_hline(cx - 4, cx + 1, cy - 5);
        rb->lcd_hline(cx - 4, cx + 1, cy + 5);
        rb->lcd_drawline(cx - 1, cy, cx + 5, cy);
        rb->lcd_drawline(cx + 5, cy, cx + 2, cy - 3);
        rb->lcd_drawline(cx + 5, cy, cx + 2, cy + 3);
    }
}

static void draw_playlist_browser(void)
{
    int row;
    char name[80];
    char line[96];

    draw_blade_shell(playlist_add_mode ? "ADD TO PLAYLIST" : "PLAYLISTS",
                     RBPREP_GREEN);
    if (tree_parent == RBPREP_ROOT_NODE) {
        text(112, 3, "/", LCD_LIGHTGRAY);
    } else {
        struct rbprep_node_record parent;
        if (read_node_record(tree_parent, &parent) &&
            read_index_string(parent.name_offset, name, sizeof(name)))
            text(112, 4, name, LCD_LIGHTGRAY);
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
            draw_blade_selection(y - 2, 19,
                                 node.kind == 0
                                 ? LCD_RGBPACK(34, 105, 73)
                                 : LCD_RGBPACK(27, 78, 128));
        }
        draw_playlist_node_glyph(16, y + 7, node.kind == 0,
                                 ordinal == tree_selection);
        rb->snprintf(line, sizeof(line), "%.38s", name);
        text(30, y + 2, line, LCD_WHITE);
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
    text(7, 226, playlist_add_mode
         ? "SELECT: CHOOSE DESTINATION     MENU: CANCEL"
         : "SELECT: OPEN     MENU: BACK", LCD_WHITE);
}

static void draw_track_browser(void)
{
    int row;
    char title_buffer[96];
    char artist_buffer[72];
    char name[80];
    char line[96];

    draw_blade_shell(active_playlist_node < 0 ? "COLLECTION" : "PLAYLIST",
                     RBPREP_GREEN);
    if (active_playlist_node >= 0) {
        struct rbprep_node_record node;
        if (read_node_record(active_playlist_node, &node) &&
            read_index_string(node.name_offset, name, sizeof(name)))
            text(75, 4, name, LCD_LIGHTGRAY);
    } else {
        rb->snprintf(line, sizeof(line), "%s %s%s",
                     track_sort_names[track_sort_key],
                     track_sort_descending ? "v" : "^",
                     search_active ? "  FILTERED" : "");
        text(184, 4, line, search_active
             ? LCD_RGBPACK(255, 145, 40) : LCD_RGBPACK(105, 125, 112));
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
        if (track_sort_key == TRACK_SORT_COMMENTS)
            read_index_string(track.comments_offset, artist_buffer,
                              sizeof(artist_buffer));
        else if (track_sort_key == TRACK_SORT_TAGS)
            read_index_string(track.tags_offset, artist_buffer,
                              sizeof(artist_buffer));
        if (ordinal == track_selection) {
            draw_blade_selection(y - 1, 20,
                                 LCD_RGBPACK(25, 91, 148));
        }
        draw_record_badge(12, y + 8, 5,
                          cue_palette[track.color & 7],
                          ordinal == track_selection);
        rb->snprintf(line, sizeof(line), "%.38s", title_buffer);
        text(22, y, line, LCD_WHITE);
        rb->snprintf(line, sizeof(line), "%.38s", artist_buffer);
        text(28, y + 10, line, LCD_RGBPACK(145, 165, 151));
        rb->snprintf(line, sizeof(line), "%d.%02d",
                     track.bpm_x100 / 100, track.bpm_x100 % 100);
        if (track_sort_key == TRACK_SORT_YEAR)
            rb->snprintf(line, sizeof(line), "%04d", track.year);
        else if (track_sort_key == TRACK_SORT_KEY)
            read_index_string(track.key_offset, line, sizeof(line));
        else if (track_sort_key == TRACK_SORT_COMMENTS)
            rb->snprintf(line, sizeof(line), "CMT");
        else if (track_sort_key == TRACK_SORT_TAGS)
            rb->snprintf(line, sizeof(line), "TAG");
        text(276, y + 10, line, RBPREP_GREEN);
    }
    rb->lcd_set_foreground(LCD_RGBPACK(14, 24, 18));
    rb->lcd_fillrect(0, 210, LCD_WIDTH, 30);
    rb->snprintf(line, sizeof(line), "%d/%d", track_selection + 1,
                 track_row_count);
    text(7, 213, line, LCD_RGBPACK(70, 235, 125));
    text(78, 213, "WHEEL: BROWSE", LCD_LIGHTGRAY);
    if (search_active) {
        rb->snprintf(line, sizeof(line), "SEARCH: %.31s", track_search);
        text(7, 226, line, LCD_RGBPACK(255, 145, 40));
    } else {
        text(7, 226, "SELECT: LOAD   PLAY: SEARCH/SORT   MENU: BACK",
             LCD_WHITE);
    }
}

static void draw_collection_filter(void)
{
    static const char *items[] = {
        "SEARCH...", "CLEAR SEARCH", "SORT TITLE", "SORT BPM",
        "SORT YEAR", "SORT KEY", "SORT COMMENTS", "SORT TAGS"
    };
    char line[80];
    int row;

    draw_blade_shell("COLLECTION SEARCH + SORT",
                     RBPREP_GREEN);
    rb->snprintf(line, sizeof(line), "QUERY  %s",
                 track_search[0] ? track_search : "<ALL TRACKS>");
    text(7, 20, line, track_search[0]
         ? RBPREP_GREEN : LCD_RGBPACK(105, 125, 112));
    for (row = 0; row < (int)ARRAYLEN(items); row++) {
        int y = 43 + row * 21;
        bool selected = row == filter_selection;
        bool active = row >= 2 && row - 2 == track_sort_key;
        if (selected) {
            draw_blade_selection(y - 3, 20,
                                 active ? LCD_RGBPACK(31, 108, 72)
                                        : LCD_RGBPACK(18, 78, 128));
        }
        draw_record_badge(12, y + 5, 5,
                          RBPREP_GREEN, selected);
        text(24, y, items[row], selected ? LCD_WHITE
             : active ? LCD_RGBPACK(70, 235, 125)
                      : LCD_RGBPACK(178, 192, 183));
        if (active)
            text(286, y, track_sort_descending ? "v" : "^",
                 LCD_RGBPACK(70, 235, 125));
    }
    text(7, 226, "WHEEL: CHOOSE   SELECT: APPLY   MENU: TRACKS", LCD_WHITE);
}

static enum rbprep_tool active_tool(void)
{
    if (mode == MODE_DECK)
        return TOOL_SEEK + deck_tool;
    if (mode == MODE_GRID)
        return TOOL_GRID_NUDGE + grid_tool;
    if (mode == MODE_CUES)
        return TOOL_CUE_SLOT + cue_tool;
    if (mode == MODE_LOOP)
        return TOOL_LOOP_LENGTH + loop_tool;
    return TOOL_META_RATING + metadata_tool;
}

static int tool_count(void)
{
    if (mode == MODE_DECK)
        return 11;
    if (mode == MODE_GRID || mode == MODE_CUES || mode == MODE_LOOP)
        return 4;
    return 4;
}

static int *tool_selection(void)
{
    if (mode == MODE_DECK)
        return &deck_tool;
    if (mode == MODE_GRID)
        return &grid_tool;
    if (mode == MODE_CUES)
        return &cue_tool;
    if (mode == MODE_LOOP)
        return &loop_tool;
    return &metadata_tool;
}

static void draw_tool_icon(int cx, int cy, enum rbprep_tool tool, int color)
{
    int x = cx - 4;
    int y = cy - 4;

    rb->lcd_set_foreground(color);
    if (tool == TOOL_SEEK) {
        rb->lcd_drawline(x, cy, x + 3, y + 1);
        rb->lcd_drawline(x, cy, x + 3, y + 7);
        rb->lcd_drawline(x + 4, cy, x + 7, y + 1);
        rb->lcd_drawline(x + 4, cy, x + 7, y + 7);
    } else if (tool == TOOL_SCRUB_STEP) {
        rb->lcd_hline(x, x + 8, cy + 2);
        rb->lcd_vline(x + 1, cy - 2, cy + 2);
        rb->lcd_vline(x + 4, cy, cy + 2);
        rb->lcd_vline(x + 7, cy - 2, cy + 2);
    } else if (tool == TOOL_ZOOM) {
        xlcd_drawcircle(cx - 1, cy - 1, 3);
        rb->lcd_drawline(cx + 2, cy + 2, cx + 5, cy + 5);
        rb->lcd_hline(cx - 3, cx + 1, cy - 1);
        rb->lcd_vline(cx - 1, cy - 3, cy + 1);
    } else if (tool == TOOL_GAIN) {
        rb->lcd_vline(x + 1, y + 1, y + 7);
        rb->lcd_vline(x + 4, y + 1, y + 7);
        rb->lcd_vline(x + 7, y + 1, y + 7);
        rb->lcd_fillrect(x, y + 2, 3, 2);
        rb->lcd_fillrect(x + 3, y + 5, 3, 2);
        rb->lcd_fillrect(x + 6, y + 3, 3, 2);
    } else if (tool == TOOL_HOST_RPM || tool == TOOL_PLAY_RPM) {
        xlcd_drawcircle(cx, cy, 4);
        xlcd_fillcircle(cx, cy, 1);
        if (tool == TOOL_HOST_RPM)
            rb->lcd_drawline(cx - 4, cy, cx - 2, cy - 2);
        else
            rb->lcd_drawline(cx, cy - 4, cx + 2, cy - 2);
    } else if (tool == TOOL_PITCH_BEND) {
        rb->lcd_vline(cx, y, y + 8);
        rb->lcd_hline(x, x + 8, cy);
        rb->lcd_fillrect(cx - 1, cy - 2, 3, 5);
    } else if (tool == TOOL_TEMPO) {
        rb->lcd_drawline(x, y + 7, x + 2, y + 2);
        rb->lcd_drawline(x + 2, y + 2, x + 5, y + 5);
        rb->lcd_drawline(x + 5, y + 5, x + 8, y);
        rb->lcd_hline(x, x + 8, y + 8);
    } else if (tool == TOOL_PLAYLIST_MODE) {
        rb->lcd_hline(x, x + 5, y + 1);
        rb->lcd_hline(x, x + 5, y + 4);
        rb->lcd_hline(x, x + 5, y + 7);
        rb->lcd_drawline(x + 5, y + 2, x + 8, y + 4);
        rb->lcd_drawline(x + 8, y + 4, x + 5, y + 6);
    } else if (tool == TOOL_ADD_PLAYLIST) {
        rb->lcd_hline(x, x + 4, y + 1);
        rb->lcd_hline(x, x + 4, y + 4);
        rb->lcd_hline(x, x + 4, y + 7);
        rb->lcd_hline(x + 5, x + 9, y + 5);
        rb->lcd_vline(x + 7, y + 3, y + 7);
    } else if (tool == TOOL_WAVEFORM_STYLE) {
        rb->lcd_vline(x, cy - 1, cy + 1);
        rb->lcd_vline(x + 2, cy - 3, cy + 3);
        rb->lcd_vline(x + 4, cy - 4, cy + 4);
        rb->lcd_vline(x + 6, cy - 2, cy + 2);
        rb->lcd_vline(x + 8, cy - 1, cy + 1);
    } else if (tool == TOOL_GRID_NUDGE) {
        rb->lcd_vline(cx, y, y + 8);
        rb->lcd_drawline(x, cy, x + 3, cy - 3);
        rb->lcd_drawline(x, cy, x + 3, cy + 3);
        rb->lcd_drawline(x + 8, cy, x + 5, cy - 3);
        rb->lcd_drawline(x + 8, cy, x + 5, cy + 3);
    } else if (tool == TOOL_GRID_BPM) {
        rb->lcd_drawline(cx - 3, cy + 4, cx, cy - 4);
        rb->lcd_drawline(cx, cy - 4, cx + 3, cy + 4);
        rb->lcd_hline(cx - 3, cx + 3, cy + 4);
        rb->lcd_drawline(cx, cy - 1, cx + 3, cy - 3);
    } else if (tool == TOOL_GRID_ORIGIN) {
        rb->lcd_vline(cx, y, y + 8);
        rb->lcd_drawline(cx, y, cx - 3, y + 3);
        rb->lcd_drawline(cx, y, cx + 3, y + 3);
        rb->lcd_hline(cx - 3, cx + 3, y + 3);
    } else if (tool == TOOL_GRID_QUANTIZE) {
        xlcd_drawcircle(cx, cy, 4);
        rb->lcd_hline(cx - 2, cx + 2, cy);
        rb->lcd_vline(cx, cy - 2, cy + 2);
    } else if (tool == TOOL_CUE_SLOT) {
        rb->lcd_vline(x + 1, y, y + 8);
        rb->lcd_fillrect(x + 2, y + 1, 6, 4);
        rb->lcd_drawline(x + 7, y + 5, x + 2, y + 5);
    } else if (tool == TOOL_CUE_MOVE) {
        rb->lcd_vline(cx, y, y + 8);
        rb->lcd_hline(x, x + 8, cy);
        rb->lcd_drawline(x, cy, x + 2, cy - 2);
        rb->lcd_drawline(x + 8, cy, x + 6, cy + 2);
    } else if (tool == TOOL_CUE_COLOR) {
        xlcd_fillcircle(cx, cy, 3);
        rb->lcd_drawpixel(cx + 4, cy - 3);
        rb->lcd_drawpixel(cx + 4, cy + 3);
    } else if (tool == TOOL_CUE_DELETE) {
        rb->lcd_drawline(x + 1, y + 1, x + 7, y + 7);
        rb->lcd_drawline(x + 7, y + 1, x + 1, y + 7);
    } else if (tool == TOOL_LOOP_LENGTH || tool == TOOL_LOOP_ACTIVE) {
        rb->lcd_hline(x + 2, x + 7, y + 1);
        rb->lcd_hline(x + 1, x + 6, y + 7);
        rb->lcd_drawline(x + 7, y + 1, x + 9, y + 3);
        rb->lcd_drawline(x + 1, y + 7, x - 1, y + 5);
        if (tool == TOOL_LOOP_LENGTH)
            rb->lcd_vline(cx, cy - 1, cy + 2);
    } else if (tool == TOOL_LOOP_IN) {
        rb->lcd_vline(x + 1, y, y + 8);
        rb->lcd_hline(x + 1, x + 7, y);
        rb->lcd_drawline(x + 7, y, x + 4, cy);
        rb->lcd_drawline(x + 4, cy, x + 7, y + 8);
    } else if (tool == TOOL_LOOP_OUT) {
        rb->lcd_vline(x + 7, y, y + 8);
        rb->lcd_hline(x + 1, x + 7, y);
        rb->lcd_drawline(x + 1, y, x + 4, cy);
        rb->lcd_drawline(x + 4, cy, x + 1, y + 8);
    } else if (tool == TOOL_META_RATING) {
        rb->lcd_drawpixel(cx, y);
        rb->lcd_hline(cx - 1, cx + 1, y + 2);
        rb->lcd_hline(x, x + 8, y + 3);
        rb->lcd_drawline(x, y + 3, cx - 2, y + 5);
        rb->lcd_drawline(cx + 4, y + 3, cx + 2, y + 5);
        rb->lcd_hline(cx - 2, cx + 2, y + 5);
    } else if (tool == TOOL_META_COLOR) {
        xlcd_fillcircle(cx, cy, 4);
        rb->lcd_set_foreground(LCD_BLACK);
        rb->lcd_drawpixel(cx + 2, cy - 2);
    } else if (tool == TOOL_META_YEAR) {
        rb->lcd_drawrect(x, y + 1, 9, 7);
        rb->lcd_hline(x, x + 8, y + 3);
        rb->lcd_vline(x + 2, y, y + 2);
        rb->lcd_vline(x + 6, y, y + 2);
    } else {
        rb->lcd_drawline(x, y + 2, x + 5, y + 7);
        rb->lcd_drawline(x, y + 2, x + 6, y + 2);
        rb->lcd_drawline(x + 5, y + 7, x + 8, y + 4);
        rb->lcd_fillrect(x + 2, y + 3, 2, 2);
    }
}

static void draw_beat_phase(void)
{
    int beat = 1;
    int index = current_beat_index(playhead);
    int i;

    if (index >= 0) {
        beat = adjusted_beat_number(index);
    } else if (beat_count <= 0) {
        int ordinal = (playhead - grid_phase_ms - grid_offset) /
                      beat_period_ms();
        if (ordinal >= 0) {
            beat = (ordinal & 3) + 1;
        }
    }

    for (i = 0; i < 4; i++) {
        int x = 238 + i * 8;
        rb->lcd_set_foreground(i + 1 == beat
                              ? LCD_RGBPACK(70, 235, 125)
                              : LCD_RGBPACK(35, 53, 42));
        rb->lcd_fillrect(x, 71, 6, 7);
    }
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

static void fit_text(char *dest, size_t size, const char *source,
                     int max_width)
{
    int width;
    size_t length;

    rb->strlcpy(dest, source ? source : "", size);
    rb->lcd_getstringsize(dest, &width, NULL);
    length = rb->strlen(dest);
    while (length > 0 && width > max_width) {
        dest[--length] = '\0';
        rb->lcd_getstringsize(dest, &width, NULL);
    }
}

static bool cue_audio_active(void)
{
    return seek_state == SEEK_PREVIEW || seek_state == SEEK_CUE_HOLD;
}

static void advance_hud_scroll(int *position, int content_width,
                               int viewport_width)
{
    if (content_width <= viewport_width) {
        *position = 0;
        return;
    }
    *position += 2;
    if (*position > content_width + 30)
        *position = 0;
}

static void draw_track_header(bool advance_scroll)
{
    char title[192];
    char fixed[64];
    char tail[96];
    char fitted[64];
    int width;
    bool title_overflow;
    bool metadata_overflow;

    rb->lcd_set_foreground(LCD_RGBPACK(13, 13, 13));
    rb->lcd_fillrect(0, 0, LCD_WIDTH, 16);
    rb->lcd_set_foreground(LCD_RGBPACK(8, 8, 8));
    rb->lcd_fillrect(0, 16, LCD_WIDTH, 16);

    rb->snprintf(title, sizeof(title), "%s - %s%s",
                 selected_artist[0] ? selected_artist : "RBPREP",
                 selected_title[0] ? selected_title : mode_names[mode],
                 selected_extension);
    rb->lcd_getstringsize(title, &width, NULL);
    title_overflow = width > LCD_WIDTH - 4;
    if (!title_overflow)
        title_scroll_px = 0;
    else if (advance_scroll)
        advance_hud_scroll(&title_scroll_px, width, LCD_WIDTH - 4);
    text(2 - title_scroll_px, 3, title, LCD_WHITE);

    rb->snprintf(tail, sizeof(tail), "%s  /  %s",
                 color_labels[color_index & 7],
                 selected_genre[0] ? selected_genre : "--");
    rb->lcd_getstringsize(tail, &width, NULL);
    metadata_overflow = width > LCD_WIDTH - 132;
    if (!metadata_overflow)
        metadata_scroll_px = 0;
    else if (advance_scroll)
        advance_hud_scroll(&metadata_scroll_px, width, LCD_WIDTH - 132);
    text(132 - metadata_scroll_px, 19, tail,
         cue_palette[color_index & 7]);

    /* The left metadata block also acts as a clip for the scrolling tail. */
    rb->lcd_set_foreground(LCD_RGBPACK(8, 8, 8));
    rb->lcd_fillrect(0, 16, 132, 16);
    rb->snprintf(fixed, sizeof(fixed), "%d.%02d  %s",
                 grid_bpm_x100 / 100, grid_bpm_x100 % 100,
                 selected_key[0] ? selected_key : "--");
    fit_text(fitted, sizeof(fitted), fixed, 72);
    text(2, 19, fitted, LCD_RGBPACK(85, 220, 255));
    draw_rating_stars(76, 21);
    rb->lcd_set_foreground(cue_palette[color_index & 7]);
    rb->lcd_fillrect(120, 20, 7, 7);
    hud_scroll_active = title_overflow || metadata_overflow;
}

static void draw_tool_orbs(void)
{
    int count = tool_count();
    int selected = *tool_selection();
    int i;

    for (i = 0; i < count; i++) {
        enum rbprep_tool tool = active_tool() - selected + i;
        int cx = count > 7 ? 7 + i * 12
                           : count > 4 ? 38 + i * 17 : 50 + i * 26;
        int color = i == selected
                                  ? (tool_menu_active
                                     ? LCD_WHITE
                                     : RBPREP_GREEN)
                                  : LCD_RGBPACK(48, 70, 56);
        rb->lcd_set_foreground(color);
        xlcd_fillcircle(cx, 74, count > 7
                               ? (i == selected ? 6 : 5)
                               : (i == selected ? 7 : 6));
        draw_tool_icon(cx, 74, tool,
                       i == selected ? LCD_BLACK
                                     : LCD_RGBPACK(195, 215, 201));
    }
}

static void draw_tool_status(void)
{
    char line[96];
    char fitted[96];
    enum rbprep_tool tool = active_tool();
    int color = tool_menu_active ? LCD_WHITE : RBPREP_GREEN;
    int status_x = tool_count() > 7 ? 132 : 150;

    if (tool == TOOL_SEEK)
        rb->snprintf(line, sizeof(line), "SEEK  %dms/tick", scrub_step_ms());
    else if (tool == TOOL_SCRUB_STEP)
        rb->snprintf(line, sizeof(line), "SCRUB  %dms/tick", scrub_step_ms());
    else if (tool == TOOL_ZOOM)
        rb->snprintf(line, sizeof(line), "ZOOM  %dx", zoom);
    else if (tool == TOOL_GAIN)
        rb->snprintf(line, sizeof(line), "GAIN  %d %s",
                     rb->sound_val2phys(SOUND_VOLUME,
                                        rb->global_status->volume),
                     rb->sound_unit(SOUND_VOLUME));
    else if (tool == TOOL_HOST_RPM)
        rb->snprintf(line, sizeof(line), "HOST RPM  %s  PLAY %s",
                     rpm_styles[host_rpm_index],
                     rpm_styles[played_rpm_index]);
    else if (tool == TOOL_PLAY_RPM)
        rb->snprintf(line, sizeof(line), "PLAY %s  HOST %s",
                     rpm_styles[played_rpm_index], rpm_styles[host_rpm_index]);
    else if (tool == TOOL_PITCH_BEND) {
        char delta[16];
        format_percent_delta(delta, sizeof(delta), pitch_bend_x100);
        rb->snprintf(line, sizeof(line), "BEND %s%%", delta);
    }
    else if (tool == TOOL_TEMPO)
        rb->snprintf(line, sizeof(line), "TEMPO  %d.%02d%%  RATE %d.%02d%%",
                     tempo_x100 / 100, tempo_x100 % 100,
                     deck_speed_x100() / 100, deck_speed_x100() % 100);
    else if (tool == TOOL_PLAYLIST_MODE)
        rb->snprintf(line, sizeof(line), "AUTO-NEXT  %s",
                     playlist_playback ? "ON" : "OFF");
    else if (tool == TOOL_ADD_PLAYLIST)
        rb->snprintf(line, sizeof(line), "ADD TO PLAYLIST");
    else if (tool == TOOL_WAVEFORM_STYLE)
        rb->snprintf(line, sizeof(line), "VISUALIZER  %s",
                     visualizer_styles[visualizer_mode]);
    else if (tool == TOOL_GRID_NUDGE)
        rb->snprintf(line, sizeof(line), "GRID  %+dms", grid_offset);
    else if (tool == TOOL_GRID_BPM)
        rb->snprintf(line, sizeof(line), "BPM  %d.%02d",
                     grid_bpm_x100 / 100, grid_bpm_x100 % 100);
    else if (tool == TOOL_GRID_ORIGIN)
        rb->snprintf(line, sizeof(line), "SET DOWNBEAT");
    else if (tool == TOOL_GRID_QUANTIZE)
        rb->snprintf(line, sizeof(line), "QUANTIZE  %s",
                     quantize ? "ON" : "OFF");
    else if (tool == TOOL_CUE_SLOT) {
        rb->snprintf(line, sizeof(line), "CUE %02d  %s", cue_slot + 1,
                     hotcues[cue_slot] >= 0 ? "SET" : "EMPTY");
    } else if (tool == TOOL_CUE_MOVE)
        rb->snprintf(line, sizeof(line), "MOVE CUE %02d", cue_slot + 1);
    else if (tool == TOOL_CUE_COLOR)
        rb->snprintf(line, sizeof(line), "CUE COLOR  %s",
                     cue_color_names[hotcue_colors[cue_slot] & 7]);
    else if (tool == TOOL_CUE_DELETE) {
        rb->snprintf(line, sizeof(line), "DELETE CUE %02d", cue_slot + 1);
        color = LCD_RGBPACK(255, 90, 70);
    } else if (tool == TOOL_LOOP_LENGTH)
        rb->snprintf(line, sizeof(line), "LOOP  %s BEAT%s",
                     loop_length_names[loop_length_index],
                     loop_length_index == 5 ? "" : "S");
    else if (tool == TOOL_LOOP_IN) {
        if (loop_in >= 0)
            rb->snprintf(line, sizeof(line), "IN  %dms", loop_in);
        else
            rb->snprintf(line, sizeof(line), "IN  --");
    } else if (tool == TOOL_LOOP_OUT) {
        if (loop_out >= 0)
            rb->snprintf(line, sizeof(line), "OUT  %dms", loop_out);
        else
            rb->snprintf(line, sizeof(line), "OUT  --");
    } else if (tool == TOOL_LOOP_ACTIVE)
        rb->snprintf(line, sizeof(line), "LOOP  %s",
                     loop_active ? "ACTIVE" : "EXIT");
    else if (tool == TOOL_META_RATING)
        rb->snprintf(line, sizeof(line), "RATING  %d/5", rating);
    else if (tool == TOOL_META_COLOR)
        rb->snprintf(line, sizeof(line), "COLOR  %s", color_labels[color_index]);
    else if (tool == TOOL_META_YEAR)
        rb->snprintf(line, sizeof(line), "YEAR  %04d", track_year);
    else
        rb->snprintf(line, sizeof(line), "GENRE  %.30s", selected_genre);
    fit_text(fitted, sizeof(fitted), line, 234 - status_x);
    text(status_x, 69, fitted, color);
}

static void draw_tool_indicators(void)
{
    int color = cue_audio_active() ? LCD_RGBPACK(255, 135, 35)
                                   : LCD_RGBPACK(82, 87, 84);

    draw_beat_phase();
    text(272, 69, "Q",
         quantize ? RBPREP_GREEN
                  : LCD_RGBPACK(105, 115, 108));
    text(288, 69, "CUE", color);
}

static void draw_tool_row(void)
{
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(0, RBPREP_TOOL_TOP, LCD_WIDTH, RBPREP_TOOL_HEIGHT);
    draw_tool_orbs();
    draw_tool_status();
    draw_tool_indicators();
}

static void draw_top_hud(void)
{
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(0, 0, LCD_WIDTH, RBPREP_WAVE_TOP);
    draw_track_header(false);
    draw_overview_waveform();
    draw_tool_row();
}

static void draw_settings(void)
{
    char line[64];
    int row;
    const char *names[] = {
        "VISUALIZER", "WAVEFORM SHAPE", "SAVE EDITS"
    };
    const char *values[] = {
        visualizer_styles[visualizer_mode],
        waveform_half ? "HALF" : "FULL",
        save_on_track_load ? "ON NEXT TRACK" : "IMMEDIATELY"
    };

    text(7, 3, "RBPREP SETTINGS", RBPREP_GREEN);
    text(7, 20, "DECK DISPLAY", LCD_RGBPACK(105, 125, 112));
    for (row = 0; row < 3; row++) {
        int y = 43 + row * 31;
        if (row == settings_selection)
            draw_blade_selection(y - 3, 27, RBPREP_GREEN);
        text(10, y + 3, names[row], LCD_WHITE);
        rb->snprintf(line, sizeof(line), "%s", values[row]);
        text(row == 0 ? 184 : row == 1 ? 260 : 215, y + 3, line,
             RBPREP_GREEN);
    }
    text(10, 145, settings_selection == 0
         ? "Boombox + EQ analyze live PCM; turntable follows deck rate"
         : settings_selection == 1
         ? (waveform_half ? "One-sided RGB waveform" : "Mirrored RGB waveform")
         : (save_on_track_load
            ? "One compact snapshot is written when the next track loads"
            : "Confirmed edits are journaled immediately"),
         LCD_RGBPACK(145, 165, 151));
    text(10, 216, "WHEEL: CHOOSE   SELECT: CHANGE   MENU: BACK", LCD_WHITE);
}

static void apply_usb_choice(int choice)
{
    usb_selection = !!choice;
#ifdef USB_ENABLE_HID
    rb->global_settings->usb_hid = false;
    rb->usb_set_hid(false);
#endif
#ifdef USB_ENABLE_AUDIO
    rb->global_settings->usb_audio = usb_selection ? 1 : 0;
    rb->usb_set_audio(rb->global_settings->usb_audio);
#endif
#if !defined(SIMULATOR) && !defined(USB_NONE) && \
    (defined(HAVE_USB_ADB) || defined(HAVE_USB_POWER))
    rb->global_settings->usb_mode = usb_selection
                                  ? USB_MODE_CHARGE
                                  : USB_MODE_MASS_STORAGE;
    rb->usb_set_mode(rb->global_settings->usb_mode);
#endif
    rb->settings_save();
}

static void draw_usb_mode(void)
{
    int row;
    const char *names[] = { "DATA TRANSFER", "USB DAC" };
    const char *details[] = {
        "Rekordbox FAT volume over USB; audio class disabled",
        "Mac audio to iPod headphone output; storage hidden"
    };

    text(7, 3, "USB MODE", RBPREP_GREEN);
    text(7, 20, "TAKES EFFECT ON NEXT USB CONNECTION",
         LCD_RGBPACK(105, 125, 112));
    for (row = 0; row < 2; row++) {
        int y = 55 + row * 55;
        if (row == usb_selection)
            draw_blade_selection(y - 5, 45, RBPREP_GREEN);
        text(12, y, names[row], LCD_WHITE);
        text(12, y + 18, details[row], LCD_RGBPACK(135, 155, 141));
    }
    text(7, 183, "USB HID IS KEPT OFF IN BOTH MODES", RBPREP_GREEN);
    text(7, 226, "WHEEL: CHOOSE   SELECT: SAVE   MENU: BACK", LCD_WHITE);
}

static void draw_genre_picker(void)
{
    int row;
    char name[32];
    char line[48];

    text(7, 3, "GENRE ROLLUP", RBPREP_GREEN);
    rb->snprintf(line, sizeof(line), "%d LIBRARY GENRES", genre_count);
    text(205, 3, line, LCD_RGBPACK(105, 125, 112));
    for (row = 0; row < RBPREP_LIST_ROWS; row++) {
        int ordinal = genre_top + row;
        int y = 27 + row * 21;
        if (ordinal > genre_count)
            break;
        if (ordinal == genre_selection)
            draw_blade_selection(y - 3, 20, RBPREP_GREEN);
        if (ordinal == 0) {
            text(10, y, "+ ADD GENRE...", RBPREP_GREEN);
        } else if (genre_name_at(ordinal - 1, name, sizeof(name))) {
            rb->snprintf(line, sizeof(line), "%s%s",
                         !rb->strcasecmp(name, selected_genre) ? "> " : "  ",
                         name);
            text(10, y, line, LCD_WHITE);
        }
    }
    text(7, 226, "WHEEL: BROWSE   SELECT: ASSIGN   MENU: BACK", LCD_WHITE);
}

static void draw_main_menu(void)
{
    static const char *items[] = {
        "COLLECTION", "PLAYLISTS", "PREP DECK", "USB MODE",
        "DECK SETTINGS", "PENDING EDITS", "INDEX STATUS",
        "EXIT TO ROCKBOX"
    };
    char line[80];
    int i;

    draw_blade_shell("RBPREP  /  PERFORMANCE LIBRARY",
                     LCD_RGBPACK(70, 235, 125));
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(5, 22, LCD_WIDTH - 5, 16);
    if (library_fd >= 0)
        rb->snprintf(line, sizeof(line), "%lu TRACKS  /  DEVICE INDEX ONLINE",
                     (unsigned long)library_track_count);
    else
        rb->snprintf(line, sizeof(line), "DEVICE INDEX OFFLINE");
    text(7, 21, line, library_fd >= 0
         ? LCD_RGBPACK(105, 178, 139) : LCD_RGBPACK(255, 90, 70));

    for (i = 0; i < (int)ARRAYLEN(items); i++) {
        int y = 42 + i * 23;
        bool selected = i == selection;
        if (selected) {
            draw_blade_selection(y - 2, 20, RBPREP_GREEN);
        }
        draw_main_glyph(20, y + 7, i, selected);
        text(42, y + 3, items[i], selected ? LCD_WHITE : RBPREP_MENU_TEXT);
        if (i == 3)
            text(257, y + 3, usb_selection ? "DAC" : "DATA",
                 usb_selection ? LCD_WHITE : RBPREP_GREEN);
        else if (i == 5 &&
                 (pending_snapshot_count || pending_playlist_count)) {
            rb->snprintf(line, sizeof(line), "%d",
                         pending_snapshot_count + pending_playlist_count);
            text(286, y + 3, line, LCD_WHITE);
        }
    }
    text(7, 229, "WHEEL  NAVIGATE       SELECT  OPEN",
         LCD_RGBPACK(105, 125, 112));
}

static void draw_pending_edits(void)
{
    char line[96];
    int row;

    text(7, 3, "PENDING EDITS", RBPREP_GREEN);
    rb->snprintf(line, sizeof(line), "%d SAVED STATES   %d PLAYLIST ADDS",
                 pending_snapshot_count, pending_playlist_count);
    text(7, 20, line, LCD_RGBPACK(145, 165, 151));
    rb->lcd_set_foreground(LCD_RGBPACK(25, 31, 27));
    rb->lcd_hline(7, LCD_WIDTH - 8, 36);
    for (row = 0; row < RBPREP_PENDING_ROWS; row++) {
        int visible = pending_top + row;
        int y = 45 + row * 20;

        if (visible >= pending_snapshot_count + pending_playlist_count)
            break;
        if (visible == pending_selection)
            draw_blade_selection(y - 2, 19, RBPREP_GREEN);
        if (visible < pending_snapshot_count) {
            int ordinal = pending_snapshot_count - 1 - visible;
            struct rbprep_pending_entry *entry = &pending_entries[ordinal];

            rb->lcd_set_foreground(cue_palette[entry->color]);
            rb->lcd_fillrect(7, y + 2, 6, 6);
            rb->snprintf(line, sizeof(line), "%.29s", entry->title[0]
                         ? entry->title : "Untitled track");
            text(19, y, line, LCD_WHITE);
            rb->snprintf(line, sizeof(line),
                         "EDIT  ID %lu   %d.%02d BPM   %d STAR",
                         (unsigned long)entry->track_id,
                         entry->bpm_x100 / 100, entry->bpm_x100 % 100,
                         entry->rating);
            text(19, y + 10, line, LCD_RGBPACK(105, 125, 112));
        } else {
            int ordinal = visible - pending_snapshot_count;
            struct rbprep_pending_playlist *entry =
                &pending_playlists[ordinal];

            rb->lcd_set_foreground(RBPREP_GREEN);
            xlcd_fillcircle(10, y + 5, 4);
            rb->lcd_set_foreground(LCD_BLACK);
            xlcd_fillcircle(10, y + 5, 1);
            rb->snprintf(line, sizeof(line), "ADD TO  %.27s", entry->name);
            text(19, y, line, LCD_WHITE);
            rb->snprintf(line, sizeof(line), "TRACK %lu   PLAYLIST %lu",
                         (unsigned long)entry->track_id,
                         (unsigned long)entry->playlist_id);
            text(19, y + 10, line, RBPREP_GREEN);
        }
    }
    if (pending_visible_count == 0 && pending_playlist_count == 0)
        text(7, 62, "NO CONFIRMED DEVICE EDITS YET", LCD_DARKGRAY);
    text(7, 210, "SELECT: OPEN   HOLD: DELETE   PLAY: BURN", LCD_WHITE);
    text(7, 226, "WHEEL: BROWSE                         MENU: BACK",
         LCD_LIGHTGRAY);
}

static void draw_index_status(void)
{
    char line[96];

    text(7, 3, "RBPREP INDEX STATUS", LCD_RGBPACK(70, 235, 125));
    text(7, 27, library_fd >= 0 ? "ONLINE" : "NOT FOUND",
         library_fd >= 0 ? LCD_RGBPACK(70, 235, 125)
                         : LCD_RGBPACK(255, 90, 70));
    rb->snprintf(line, sizeof(line), "%lu TRACKS",
                 (unsigned long)library_track_count);
    text(7, 52, line, LCD_WHITE);
    rb->snprintf(line, sizeof(line), "%lu PLAYLIST/FOLDER NODES",
                 (unsigned long)library_node_count);
    text(7, 72, line, LCD_WHITE);
    rb->snprintf(line, sizeof(line), "%lu PLAYLIST MEMBERS",
                 (unsigned long)library_member_count);
    text(7, 92, line, LCD_WHITE);
    rb->snprintf(line, sizeof(line), "%d EDIT SNAPSHOTS", pending_snapshot_count);
    text(7, 122, line, LCD_RGBPACK(255, 145, 40));
    rb->snprintf(line, sizeof(line), "%d PLAYLIST ADD REQUESTS",
                 pending_playlist_count);
    text(7, 142, line, LCD_RGBPACK(255, 145, 40));
    text(7, 180, "LOCAL BURN AVAILABLE IN PENDING EDITS",
         LCD_RGBPACK(145, 165, 151));
    text(7, 226, "MENU: BACK", LCD_LIGHTGRAY);
}

static void discard_staged_edit(void)
{
    if (staged_tool == TOOL_CUE_COLOR) {
        hotcue_colors[cue_slot] = staged_original;
        overview_dirty = true;
    } else if (staged_tool == TOOL_GRID_NUDGE) {
        grid_offset = staged_original;
    } else if (staged_tool == TOOL_GRID_BPM) {
        grid_bpm_x100 = staged_original;
    } else if (staged_tool == TOOL_GRID_QUANTIZE) {
        quantize = staged_original;
    } else if (staged_tool == TOOL_META_RATING) {
        rating = staged_original;
    } else if (staged_tool == TOOL_META_COLOR) {
        color_index = staged_original;
    } else if (staged_tool == TOOL_META_YEAR) {
        track_year = staged_original;
    }
    staged_tool = -1;
}

static void stage_active_edit(void)
{
    enum rbprep_tool tool = active_tool();

    if (staged_tool == tool)
        return;
    discard_staged_edit();
    staged_tool = tool;
    if (tool == TOOL_CUE_COLOR)
        staged_original = hotcue_colors[cue_slot];
    else if (tool == TOOL_GRID_NUDGE)
        staged_original = grid_offset;
    else if (tool == TOOL_GRID_BPM)
        staged_original = grid_bpm_x100;
    else if (tool == TOOL_GRID_QUANTIZE)
        staged_original = quantize;
    else if (tool == TOOL_META_RATING)
        staged_original = rating;
    else if (tool == TOOL_META_COLOR)
        staged_original = color_index;
    else if (tool == TOOL_META_YEAR)
        staged_original = track_year;
    else
        staged_tool = -1;
}

static void begin_confirmation(enum rbprep_confirm_action action)
{
    confirm_action = action;
    confirm_active = true;
    confirm_ok = true;
    confirm_wait_release = !!(rb->button_status() & BUTTON_SELECT);
    force_full_redraw = true;
}

static bool append_playlist_journal(int node_index)
{
    struct rbprep_node_record node;
    char name[80];
    char line[160];
    int fd;
    int length;
    int i;
    uint32_t playlist_id;

    if (!read_node_record(node_index, &node) ||
        !read_index_string(node.name_offset, name, sizeof(name)))
        return false;
    playlist_id = node.source_id ? node.source_id : (uint32_t)node_index;
    for (i = 0; i < pending_playlist_count; i++) {
        if (pending_playlists[i].track_id == (uint32_t)selected_track_id &&
            pending_playlists[i].playlist_id == playlist_id)
            return true;
    }
    fd = rb->open(RBPREP_PLAYLIST_JOURNAL,
                  O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return false;
    length = rb->snprintf(line, sizeof(line), "%d\t%lu\t%s\n",
                          selected_track_id,
                          (unsigned long)playlist_id,
                          name);
    if (rb->write(fd, line, length) != length) {
        rb->close(fd);
        return false;
    }
    rb->close(fd);
    return true;
}

static void apply_grid_origin(int origin)
{
    int index = nearest_beat_index(origin);

    if (index >= 0) {
        grid_offset += origin - adjusted_beat_time(index);
        grid_beat_shift = (1 - beat_numbers[index]) & 3;
    } else {
        grid_phase_ms = origin;
        grid_offset = 0;
    }
}

static void finish_confirmation(bool apply)
{
    enum rbprep_confirm_action action = confirm_action;
    bool save_snapshot = false;

    confirm_active = false;
    confirm_action = CONFIRM_NONE;
    if (!apply) {
        if (action == CONFIRM_KEEP_EDIT)
            discard_staged_edit();
        restore_black_canvas();
        return;
    }

    if (action == CONFIRM_EXIT) {
        if (flush_deferred_edit()) {
            exit_requested = true;
        } else {
            rb->splash(HZ * 2, "Could not save deferred edits");
            restore_black_canvas();
        }
    } else if (action == CONFIRM_DECK_CUE) {
        deck_cue = confirm_time;
        playhead = deck_cue;
        save_snapshot = true;
    } else if (action == CONFIRM_CUE_SET) {
        hotcues[confirm_slot] = confirm_time;
        hotcue_colors[confirm_slot] = confirm_color;
        cue_slot = confirm_slot;
        playhead = confirm_time;
        overview_dirty = true;
        save_snapshot = true;
    } else if (action == CONFIRM_CUE_DELETE) {
        hotcues[confirm_slot] = -1;
        cue_slot = confirm_slot;
        overview_dirty = true;
        save_snapshot = true;
    } else if (action == CONFIRM_GRID_ORIGIN) {
        apply_grid_origin(confirm_time);
        save_snapshot = true;
    } else if (action == CONFIRM_KEEP_EDIT) {
        staged_tool = -1;
        save_snapshot = true;
    } else if (action == CONFIRM_ADD_PLAYLIST) {
        if (append_playlist_journal(confirm_playlist_node)) {
            playlist_add_mode = false;
            mode = MODE_DECK;
            refresh_pending_summary();
            rb->splash(HZ, "Playlist add queued");
        } else {
            rb->splash(HZ * 2, "Could not queue playlist add");
        }
    } else if (action == CONFIRM_GENRE) {
        rb->strlcpy(selected_genre, confirm_genre,
                    sizeof(selected_genre));
        mode = MODE_METADATA;
        save_snapshot = true;
    } else if (action == CONFIRM_BURN) {
        if (rbprep_burn_all())
            rb->splash(HZ * 2, "Rekordbox edits burned locally");
        refresh_pending_summary();
    } else if (action == CONFIRM_DELETE_PENDING) {
        uint32_t track_id = pending_entries[confirm_slot].track_id;
        if (!delete_pending_edit(track_id)) {
            rb->splash(HZ * 2, "Could not delete pending edit");
            restore_black_canvas();
        }
        refresh_pending_summary();
    } else if (action == CONFIRM_DELETE_PLAYLIST) {
        struct rbprep_pending_playlist *entry =
            &pending_playlists[confirm_slot];
        if (!delete_pending_playlist(entry->track_id,
                                     entry->playlist_id)) {
            rb->splash(HZ * 2, "Could not delete playlist add");
            restore_black_canvas();
        }
        refresh_pending_summary();
    }
    if (save_snapshot && !record_edit_change())
        rb->splash(HZ * 2, "Edit applied, journal write failed");
    restore_black_canvas();
}

static void draw_confirmation(void)
{
    const int x = 25;
    const int y = 83;
    const int width = LCD_WIDTH - 50;
    const int height = 76;

    rb->lcd_set_foreground(LCD_RGBPACK(4, 4, 4));
    rb->lcd_fillrect(x, y, width, height);
    rb->lcd_set_foreground(LCD_RGBPACK(105, 115, 108));
    rb->lcd_drawrect(x, y, width, height);
    text(x + 10, y + 9, "CONFIRM", RBPREP_GREEN);
    text(x + 10, y + 29, confirm_message, LCD_WHITE);

    rb->lcd_set_foreground(confirm_ok ? LCD_RGBPACK(20, 26, 22)
                                      : LCD_RGBPACK(12, 49, 29));
    rb->lcd_fillrect(x + 12, y + 51, 72, 18);
    text(x + 29, y + 55, "CANCEL", LCD_WHITE);
    rb->lcd_set_foreground(confirm_ok ? RBPREP_GREEN
                                      : LCD_RGBPACK(20, 26, 22));
    rb->lcd_fillrect(x + width - 66, y + 51, 54, 18);
    text(x + width - 48, y + 55, "OK", confirm_ok ? LCD_BLACK : LCD_WHITE);
}

static void draw_screen(void)
{
    rb->lcd_set_background(LCD_BLACK);
    rb->lcd_set_drawmode(DRMODE_SOLID);
    if (mode == MODE_PLAYLISTS || mode == MODE_TRACKS || mode == MODE_FILTER ||
        mode == MODE_LIBRARY || mode == MODE_USB || mode == MODE_SETTINGS ||
        mode == MODE_PENDING || mode == MODE_INDEX || mode == MODE_GENRES) {
        rb->lcd_clear_display();
        if (mode == MODE_PLAYLISTS)
            draw_playlist_browser();
        else if (mode == MODE_TRACKS)
            draw_track_browser();
        else if (mode == MODE_FILTER)
            draw_collection_filter();
        else if (mode == MODE_USB)
            draw_usb_mode();
        else if (mode == MODE_SETTINGS)
            draw_settings();
        else if (mode == MODE_PENDING)
            draw_pending_edits();
        else if (mode == MODE_INDEX)
            draw_index_status();
        else if (mode == MODE_GENRES)
            draw_genre_picker();
        else
            draw_main_menu();
        if (confirm_active)
            draw_confirmation();
        rb->lcd_update();
        force_full_redraw = false;
        return;
    }

    update_vu_levels();
    if (force_full_redraw) {
        rb->lcd_clear_display();
        draw_top_hud();
    } else {
        if (hud_scroll_active &&
            !TIME_BEFORE(*rb->current_tick, hud_scroll_deadline)) {
            draw_track_header(true);
            rb->lcd_update_rect(0, 0, LCD_WIDTH, 32);
            hud_scroll_deadline = *rb->current_tick +
                                  RBPREP_HUD_SCROLL_TICKS;
        }
        if (overview_dirty ||
            !TIME_BEFORE(*rb->current_tick, overview_deadline)) {
            if (!TIME_BEFORE(*rb->current_tick, overview_deadline))
                overview_playhead_white = !overview_playhead_white;
            draw_overview_waveform();
            draw_tool_row();
            overview_dirty = false;
            overview_deadline = *rb->current_tick + RBPREP_OVERVIEW_TICKS;
            rb->lcd_update_rect(0, RBPREP_OVERVIEW_Y, LCD_WIDTH,
                                RBPREP_WAVE_TOP - RBPREP_OVERVIEW_Y);
        }
    }

    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(RBPREP_DECK_X, RBPREP_WAVE_TOP,
                     RBPREP_DECK_WIDTH,
                     RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP + 1);
    draw_waveform();
    draw_vu_meter();
    if (confirm_active)
        draw_confirmation();

    if (force_full_redraw) {
        rb->lcd_update();
        force_full_redraw = false;
        overview_dirty = false;
        overview_deadline = *rb->current_tick + RBPREP_OVERVIEW_TICKS;
        hud_scroll_deadline = *rb->current_tick +
                              RBPREP_HUD_SCROLL_TICKS;
    } else {
        rb->lcd_set_foreground(LCD_BLACK);
        rb->lcd_fillrect(236, RBPREP_TOOL_TOP,
                         LCD_WIDTH - 236, RBPREP_TOOL_HEIGHT);
        draw_tool_indicators();
        rb->lcd_update_rect(0, RBPREP_WAVE_TOP, LCD_WIDTH,
                            RBPREP_WAVE_BOTTOM - RBPREP_WAVE_TOP + 1);
        rb->lcd_update_rect(236, RBPREP_TOOL_TOP,
                            LCD_WIDTH - 236, RBPREP_TOOL_HEIGHT);
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
    position = clamp_playhead(play_clock_anchor +
        (long long)(now - play_clock_tick) * 1000 * deck_speed_x100() /
        (HZ * PITCH_SPEED_100));
    id3 = rb->audio_current_track();
    if (id3 && !TIME_BEFORE(now, audio_sync_after)) {
        int observed = clamp_playhead(id3->elapsed);
        if (observed != reported_audio_elapsed) {
            int error = observed - position;

            /* Codec elapsed can arrive several seconds late at a refill
               boundary. A backwards snap looks like a frozen waveform until
               the display clock catches up, so reject stale observations and
               phase-lock only with bounded, monotonic corrections. */
            reported_audio_elapsed = observed;
            if (error > 0)
                position += MIN(error, 100);
            else if (error >= -250)
                position += MAX(error / 8, -8);
            position = MAX(playhead, clamp_playhead(position));
            play_clock_anchor = position;
            play_clock_tick = now;
        }
    }
    position = MAX(playhead, position);
    if (position == playhead)
        return false;
    playhead = position;
    return true;
}

static void stop_editor_audio(void)
{
    bool resume;

    if (seek_state == SEEK_IDLE)
        return;
    resume = !seek_was_paused && !cue_audition_active;
    rb->audio_pause();
    rb->audio_ff_rewind(playhead);
    rb->pcmbuf_fade(false, true);
    seek_state = SEEK_IDLE;
    cue_audition_active = false;
    cue_audition_latched = false;
    overview_dirty = true;
    if (resume)
        rb->audio_resume();
    reset_play_clock(playhead, *rb->current_tick);
}

static void jump_to_time(int target)
{
    int status = rb->audio_status();
    bool was_running = (status & AUDIO_STATUS_PLAY) &&
                       !(status & AUDIO_STATUS_PAUSE);

    target = clamp_playhead(target);
    if (seek_state != SEEK_IDLE)
        stop_editor_audio();
    if (status & AUDIO_STATUS_PLAY) {
        rb->audio_pre_ff_rewind();
        rb->audio_ff_rewind(target);
        if (was_running)
            rb->audio_resume();
    }
    playhead = target;
    seek_state = SEEK_IDLE;
    cue_audition_active = false;
    cue_audition_latched = false;
    overview_dirty = true;
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

    if (seek_state == SEEK_CUE_HOLD) {
        int position = clamp_playhead(play_clock_anchor +
            (long long)(now - play_clock_tick) * 1000 * deck_speed_x100() /
            (HZ * PITCH_SPEED_100));
        if (position != playhead) {
            playhead = position;
            return true;
        }
        return false;
    }

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
        if (seek_preview) {
            rb->pcmbuf_fade(true, true);
            rb->audio_resume();
            seek_applied_tick = now;
            reset_play_clock(seek_applied_target, now);
            if (cue_audition_active) {
                if (cue_audition_latched) {
                    cue_audition_active = false;
                    cue_audition_latched = false;
                    seek_state = SEEK_IDLE;
                } else {
                    seek_state = SEEK_CUE_HOLD;
                }
            } else {
                seek_state = SEEK_PREVIEW;
                seek_deadline = now + RBPREP_PREVIEW_TICKS;
            }
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
        rb->pcmbuf_fade(true, false);
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
            if (!seek_was_paused) {
                rb->pcmbuf_fade(true, true);
                rb->audio_resume();
            } else {
                rb->pcmbuf_fade(false, true);
            }
            reset_play_clock(playhead, now);
        }
    }
    return true;
}

static void audition_playhead(void)
{
    request_audio_seek(true);
}

static void start_cue_audition(void)
{
    long now;

    if (hotcues[cue_slot] < 0 ||
        !(rb->audio_status() & AUDIO_STATUS_PLAY))
        return;
    stop_editor_audio();
    now = *rb->current_tick;
    cue_audition_position = hotcues[cue_slot];
    cue_audition_active = true;
    cue_audition_latched = false;
    overview_dirty = true;
    playhead = cue_audition_position;
    seek_target = cue_audition_position;
    seek_applied_target = cue_audition_position;
    seek_preview = true;
    seek_was_paused = true;
    rb->audio_pre_ff_rewind();
    rb->audio_ff_rewind(cue_audition_position);
    seek_applied_tick = now;
    seek_state = SEEK_SETTLE;
    seek_deadline = now + RBPREP_CUE_SETTLE;
    reset_play_clock(cue_audition_position, now);
}

static void finish_cue_audition(void)
{
    if (!cue_audition_active)
        return;
    if (cue_audition_latched) {
        rb->pcmbuf_fade(true, true);
        rb->audio_resume();
        if (seek_state != SEEK_CUE_HOLD)
            playhead = cue_audition_position;
        cue_audition_active = false;
        cue_audition_latched = false;
        overview_dirty = true;
        seek_state = SEEK_IDLE;
        reset_play_clock(playhead, *rb->current_tick);
        return;
    }
    rb->pcmbuf_fade(true, false);
    rb->audio_pause();
    rb->audio_pre_ff_rewind();
    rb->audio_ff_rewind(cue_audition_position);
    rb->pcmbuf_fade(false, true);
    playhead = cue_audition_position;
    seek_state = SEEK_IDLE;
    cue_audition_active = false;
    overview_dirty = true;
    reset_play_clock(playhead, *rb->current_tick);
}

static void latch_cue_audition(void)
{
    if (!cue_audition_active)
        return;
    cue_audition_latched = true;
    finish_cue_audition();
}

static void seek_by(int delta, bool audition)
{
    playhead = clamp_playhead(playhead + delta);
    if (audition)
        request_audio_seek(true);
}

static void beat_jump(int direction)
{
    int index = current_beat_index(playhead);
    int target;

    if (beat_count > 0 && index >= 0) {
        index = MAX(0, MIN(beat_count - 1, index + direction));
        target = adjusted_beat_time(index);
    } else {
        int period = beat_period_ms();
        int snapped = quantized_time(playhead);
        target = snapped + direction * period;
    }
    seek_by(clamp_playhead(target) - playhead, true);
}

static void toggle_playback(void)
{
    int status = rb->audio_status();

    if (cue_audition_active) {
        latch_cue_audition();
        return;
    }
    if (seek_state == SEEK_PREVIEW) {
        playhead = clamp_playhead(seek_applied_target +
            (long long)(*rb->current_tick - seek_applied_tick) * 1000 *
            deck_speed_x100() / (HZ * PITCH_SPEED_100));
        seek_state = SEEK_IDLE;
        rb->pcmbuf_fade(false, true);
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
        restore_black_canvas();
    }
}

static void choose_genre(const char *name)
{
    rb->strlcpy(confirm_genre, name, sizeof(confirm_genre));
    rb->snprintf(confirm_message, sizeof(confirm_message),
                 "SET GENRE %.26s?", confirm_genre);
    begin_confirmation(CONFIRM_GENRE);
}

static void add_genre_with_keyboard(void)
{
    char name[32] = "";
    int start;
    int end;

    if (rb->kbd_input(name, sizeof(name), NULL) < 0) {
        rb->lcd_setfont(FONT_SYSFIXED);
        restore_black_canvas();
        return;
    }
    rb->lcd_setfont(FONT_SYSFIXED);
    start = 0;
    while (name[start] == ' ' || name[start] == '\t')
        start++;
    if (start > 0)
        rb->memmove(name, name + start, rb->strlen(name + start) + 1);
    end = rb->strlen(name);
    while (end > 0 && (name[end - 1] == ' ' || name[end - 1] == '\t'))
        name[--end] = '\0';
    if (!name[0]) {
        rb->splash(HZ, "Genre name is empty");
    } else if (!append_custom_genre(name)) {
        rb->splash(HZ * 2, "Could not save genre");
    } else {
        int i;
        char candidate[32];
        genre_selection = 0;
        for (i = 0; i < genre_count; i++) {
            if (genre_name_at(i, candidate, sizeof(candidate)) &&
                !rb->strcasecmp(candidate, name)) {
                genre_selection = i + 1;
                break;
            }
        }
        genre_top = MAX(0, genre_selection - RBPREP_LIST_ROWS + 1);
        choose_genre(name);
    }
    restore_black_canvas();
}

static void edit_collection_search(void)
{
    char query[sizeof(track_search)];
    int start = 0;
    int end;

    rb->strlcpy(query, track_search, sizeof(query));
    if (rb->kbd_input(query, sizeof(query), NULL) < 0) {
        rb->lcd_setfont(FONT_SYSFIXED);
        restore_black_canvas();
        return;
    }
    rb->lcd_setfont(FONT_SYSFIXED);
    while (query[start] == ' ' || query[start] == '\t')
        start++;
    if (start)
        rb->memmove(query, query + start, rb->strlen(query + start) + 1);
    end = rb->strlen(query);
    while (end > 0 && (query[end - 1] == ' ' || query[end - 1] == '\t'))
        query[--end] = '\0';
    rb->strlcpy(track_search, query, sizeof(track_search));
    rebuild_search_results();
    mode = MODE_TRACKS;
    restore_black_canvas();
}

static void choose_collection_sort(int sort_key)
{
    if (sort_key == track_sort_key)
        track_sort_descending = !track_sort_descending;
    else {
        track_sort_key = sort_key;
        track_sort_descending = false;
    }
    if (search_active)
        rebuild_search_results();
    else {
        track_row_count = library_track_count;
        track_selection = track_top = 0;
    }
    mode = MODE_TRACKS;
    force_full_redraw = true;
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
        } else if (selection == 2) {
            if (library_fd >= 0)
                open_loaded_track();
            else {
                rb->splash(HZ * 2, "RBPrep index is offline");
                restore_black_canvas();
            }
        } else if (selection == 3) {
            usb_selection = rb->global_settings->usb_audio != 0;
            mode = MODE_USB;
            force_full_redraw = true;
        } else if (selection == 4) {
            settings_selection = 0;
            mode = MODE_SETTINGS;
            force_full_redraw = true;
        } else if (selection == 5) {
            if (!flush_deferred_edit()) {
                rb->splash(HZ * 2, "Could not save deferred edits");
                restore_black_canvas();
                return;
            }
            refresh_pending_summary();
            mode = MODE_PENDING;
            force_full_redraw = true;
        } else if (selection == 6) {
            refresh_pending_summary();
            mode = MODE_INDEX;
            force_full_redraw = true;
        } else if (selection == 7) {
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "EXIT TO ROCKBOX?");
            begin_confirmation(CONFIRM_EXIT);
        }
    } else if (mode == MODE_PLAYLISTS) {
        struct rbprep_node_record node;
        int index = child_node_at(tree_parent, tree_selection, &node);
        if (index >= 0 && node.kind == 0) {
            tree_selection = tree_top = 0;
            refresh_tree_children(index);
            force_full_redraw = true;
        } else if (index >= 0 && playlist_add_mode) {
            char name[40];
            read_index_string(node.name_offset, name, sizeof(name));
            confirm_playlist_node = index;
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "ADD TO %.28s?", name);
            begin_confirmation(CONFIRM_ADD_PLAYLIST);
        } else if (index >= 0) {
            open_track_browser(index);
        }
    } else if (mode == MODE_TRACKS) {
        if (track_row_count > 0)
            play_track_row(track_selection);
    } else if (mode == MODE_FILTER) {
        if (filter_selection == 0) {
            edit_collection_search();
        } else if (filter_selection == 1) {
            track_search[0] = '\0';
            rebuild_search_results();
            mode = MODE_TRACKS;
        } else {
            choose_collection_sort(filter_selection - 2);
        }
    } else if (mode == MODE_USB) {
        apply_usb_choice(usb_selection);
        rb->splash(HZ, usb_selection ? "USB DAC selected"
                                     : "Data transfer selected");
        restore_black_canvas();
    } else if (mode == MODE_SETTINGS) {
        if (settings_selection == 0)
            visualizer_mode = (visualizer_mode + 1) %
                              ARRAYLEN(visualizer_styles);
        else if (settings_selection == 1)
            waveform_half = !waveform_half;
        else {
            save_on_track_load = !save_on_track_load;
            if (!save_on_track_load && !flush_deferred_edit()) {
                rb->splash(HZ * 2, "Could not save deferred edits");
                restore_black_canvas();
            }
        }
        rebuild_waveform_height_lut();
        configfile_save(RBPREP_CONFIG_FILE, rbprep_config,
                        ARRAYLEN(rbprep_config), RBPREP_CONFIG_VERSION);
        force_full_redraw = true;
    } else if (mode == MODE_PENDING) {
        int index = -1;
        if (pending_selection < pending_snapshot_count) {
            int ordinal = pending_snapshot_count - 1 - pending_selection;
            index = find_track_index_by_id(pending_entries[ordinal].track_id);
        } else if (pending_selection < pending_snapshot_count +
                                            pending_playlist_count) {
            int ordinal = pending_selection - pending_snapshot_count;
            index = find_track_index_by_id(
                pending_playlists[ordinal].track_id);
        }
        if (index >= 0) {
            playlist_playback = false;
            play_track_index(index, -1, MODE_PENDING);
        } else if (pending_snapshot_count + pending_playlist_count > 0) {
            rb->splash(HZ * 2, "Track is missing from device index");
            restore_black_canvas();
        }
    } else if (mode == MODE_GENRES) {
        char name[32];
        if (genre_selection == 0) {
            add_genre_with_keyboard();
        } else if (genre_name_at(genre_selection - 1, name, sizeof(name))) {
            choose_genre(name);
        }
    } else if (mode == MODE_DECK) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_SEEK) {
            if (deck_cue >= 0)
                playhead = deck_cue;
            audition_playhead();
        } else if (tool == TOOL_SCRUB_STEP) {
            configfile_save(RBPREP_CONFIG_FILE, rbprep_config,
                            ARRAYLEN(rbprep_config), RBPREP_CONFIG_VERSION);
        } else if (tool == TOOL_ZOOM) {
            zoom = 1;
        } else if (tool == TOOL_HOST_RPM) {
            host_rpm_index = 0;
            apply_playback_rate();
        } else if (tool == TOOL_PLAY_RPM) {
            played_rpm_index = 0;
            apply_playback_rate();
        } else if (tool == TOOL_PITCH_BEND) {
            pitch_bend_x100 = 0;
            apply_playback_rate();
        } else if (tool == TOOL_TEMPO) {
            tempo_x100 = PITCH_SPEED_100;
            apply_playback_rate();
            configfile_save(RBPREP_CONFIG_FILE, rbprep_config,
                            ARRAYLEN(rbprep_config), RBPREP_CONFIG_VERSION);
        } else if (tool == TOOL_PLAYLIST_MODE) {
            playlist_playback = !playlist_playback;
        } else if (tool == TOOL_ADD_PLAYLIST) {
            if (selected_track_id >= 0) {
                playlist_add_mode = true;
                tree_selection = tree_top = 0;
                refresh_tree_children(RBPREP_ROOT_NODE);
                mode = MODE_PLAYLISTS;
                force_full_redraw = true;
            }
        } else if (tool == TOOL_WAVEFORM_STYLE) {
            visualizer_mode = (visualizer_mode + 1) %
                              ARRAYLEN(visualizer_styles);
            configfile_save(RBPREP_CONFIG_FILE, rbprep_config,
                            ARRAYLEN(rbprep_config), RBPREP_CONFIG_VERSION);
            force_full_redraw = true;
        }
    } else if (mode == MODE_CUES) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_CUE_MOVE && hotcues[cue_slot] >= 0) {
            playhead = hotcues[cue_slot];
            audition_playhead();
        } else if (tool == TOOL_CUE_COLOR && staged_tool == tool) {
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "KEEP CUE COLOR?");
            begin_confirmation(CONFIRM_KEEP_EDIT);
        }
    } else if (mode == MODE_GRID) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_GRID_QUANTIZE && staged_tool != tool) {
            stage_active_edit();
            quantize = !quantize;
        }
        if (staged_tool == tool) {
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "KEEP GRID CHANGE?");
            begin_confirmation(CONFIRM_KEEP_EDIT);
        }
    } else if (mode == MODE_LOOP) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_LOOP_LENGTH) {
            make_auto_loop();
        } else if (tool == TOOL_LOOP_IN) {
            set_loop_in();
        } else if (tool == TOOL_LOOP_OUT) {
            set_loop_out();
        } else if (tool == TOOL_LOOP_ACTIVE) {
            if (loop_in < 0 || loop_out <= loop_in) {
                make_auto_loop();
            } else {
                loop_active = !loop_active;
                if (loop_active)
                    jump_to_time(loop_in);
                overview_dirty = true;
            }
        }
    } else if (mode == MODE_METADATA) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_META_GENRE) {
            int i;
            char name[32];
            genre_selection = 0;
            genre_top = 0;
            for (i = 0; i < genre_count; i++) {
                if (genre_name_at(i, name, sizeof(name)) &&
                    !rb->strcasecmp(name, selected_genre)) {
                    genre_selection = i + 1;
                    genre_top = MAX(0, genre_selection - 4);
                    break;
                }
            }
            mode = MODE_GENRES;
            force_full_redraw = true;
        } else if (staged_tool == tool) {
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "KEEP METADATA CHANGE?");
            begin_confirmation(CONFIRM_KEEP_EDIT);
        }
    }
}

static void long_select(void)
{
    if (mode == MODE_PENDING &&
        pending_snapshot_count + pending_playlist_count > 0) {
        if (pending_selection < pending_snapshot_count) {
            int ordinal = pending_snapshot_count - 1 - pending_selection;
            confirm_slot = ordinal;
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "DELETE EDIT %.24s?",
                         pending_entries[ordinal].title);
            begin_confirmation(CONFIRM_DELETE_PENDING);
        } else {
            int ordinal = pending_selection - pending_snapshot_count;
            confirm_slot = ordinal;
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "DELETE ADD TO %.22s?",
                         pending_playlists[ordinal].name);
            begin_confirmation(CONFIRM_DELETE_PLAYLIST);
        }
    } else if (mode == MODE_DECK) {
        if (active_tool() == TOOL_SEEK) {
            confirm_time = quantized_time(playhead);
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "SET MAIN CUE @ %dms?", confirm_time);
            begin_confirmation(CONFIRM_DECK_CUE);
        }
    } else if (mode == MODE_CUES) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_CUE_DELETE) {
            if (hotcues[cue_slot] >= 0) {
                confirm_slot = cue_slot;
                rb->snprintf(confirm_message, sizeof(confirm_message),
                             "DELETE CUE %02d?", cue_slot + 1);
                begin_confirmation(CONFIRM_CUE_DELETE);
            }
        } else if (tool == TOOL_CUE_SLOT || tool == TOOL_CUE_MOVE) {
            confirm_slot = cue_slot;
            confirm_time = quantized_time(playhead);
            confirm_color = color_index;
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "%s CUE %02d @ %dms?",
                         hotcues[cue_slot] >= 0 ? "REPLACE" : "CREATE",
                         cue_slot + 1, confirm_time);
            begin_confirmation(CONFIRM_CUE_SET);
        }
    } else if (mode == MODE_GRID) {
        if (active_tool() == TOOL_GRID_ORIGIN) {
            confirm_time = playhead;
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "SET DOWNBEAT @ %dms?", playhead);
            begin_confirmation(CONFIRM_GRID_ORIGIN);
        }
    } else if (mode == MODE_LOOP) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_LOOP_LENGTH)
            make_auto_loop();
        else if (tool == TOOL_LOOP_IN)
            set_loop_in();
        else if (tool == TOOL_LOOP_OUT)
            set_loop_out();
        else if (tool == TOOL_LOOP_ACTIVE) {
            loop_in = loop_out = -1;
            loop_active = false;
            overview_dirty = true;
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

static void change_volume(int direction)
{
    int volume = rb->global_status->volume + direction;

    volume = MAX(rb->sound_min(SOUND_VOLUME),
                 MIN(rb->sound_max(SOUND_VOLUME), volume));
    if (volume != rb->global_status->volume)
        rb->sound_set(SOUND_VOLUME, volume);
}

static void adjust_loop_marker(enum rbprep_tool tool, int delta)
{
    if (loop_in < 0 || loop_out <= loop_in)
        make_auto_loop();
    if (tool == TOOL_LOOP_IN)
        loop_in = MAX(0, MIN(loop_out - 1, loop_in + delta));
    else
        loop_out = MAX(loop_in + 1,
                       MIN(track_length, loop_out + delta));
    loop_active = loop_out > loop_in;
    overview_dirty = true;
}

static void adjust_active_tool(int direction)
{
    enum rbprep_tool tool = active_tool();

    if (tool == TOOL_SEEK || tool == TOOL_CUE_MOVE ||
        tool == TOOL_GRID_ORIGIN)
        seek_by(direction * scrub_step_ms(), true);
    else if (tool == TOOL_SCRUB_STEP) {
        scrub_step_index = (scrub_step_index +
            (direction > 0 ? 1 : ARRAYLEN(scrub_steps) - 1)) %
            ARRAYLEN(scrub_steps);
        configfile_save(RBPREP_CONFIG_FILE, rbprep_config,
                        ARRAYLEN(rbprep_config), RBPREP_CONFIG_VERSION);
    }
    else if (tool == TOOL_ZOOM)
        change_zoom(direction > 0);
    else if (tool == TOOL_GAIN)
        change_volume(direction);
    else if (tool == TOOL_HOST_RPM) {
        host_rpm_index = (host_rpm_index +
            (direction > 0 ? 1 : ARRAYLEN(rpm_styles) - 1)) %
            ARRAYLEN(rpm_styles);
        apply_playback_rate();
        force_full_redraw = true;
    }
    else if (tool == TOOL_PLAY_RPM) {
        played_rpm_index = (played_rpm_index +
            (direction > 0 ? 1 : ARRAYLEN(rpm_styles) - 1)) %
            ARRAYLEN(rpm_styles);
        apply_playback_rate();
        force_full_redraw = true;
    }
    else if (tool == TOOL_PITCH_BEND) {
        pitch_bend_x100 = MAX(-1600, MIN(1600,
                                pitch_bend_x100 + direction * 10));
        apply_playback_rate();
        force_full_redraw = true;
    }
    else if (tool == TOOL_TEMPO) {
        tempo_x100 = MAX(5000, MIN(20000,
                                   tempo_x100 + direction * 10));
        apply_playback_rate();
        force_full_redraw = true;
    }
    else if (tool == TOOL_PLAYLIST_MODE)
        playlist_playback = direction > 0;
    else if (tool == TOOL_WAVEFORM_STYLE) {
        visualizer_mode = (visualizer_mode +
            (direction > 0 ? 1 : ARRAYLEN(visualizer_styles) - 1)) %
            ARRAYLEN(visualizer_styles);
        configfile_save(RBPREP_CONFIG_FILE, rbprep_config,
                        ARRAYLEN(rbprep_config), RBPREP_CONFIG_VERSION);
        force_full_redraw = true;
    }
    else if (tool == TOOL_LOOP_LENGTH) {
        loop_length_index = (loop_length_index +
            (direction > 0 ? 1 : ARRAYLEN(loop_beats_x32) - 1)) %
            ARRAYLEN(loop_beats_x32);
        if (loop_in >= 0) {
            loop_out = clamp_playhead(loop_in + loop_duration_ms());
            loop_active = loop_out > loop_in;
            overview_dirty = true;
        }
    }
    else if (tool == TOOL_LOOP_IN || tool == TOOL_LOOP_OUT)
        adjust_loop_marker(tool, direction * scrub_step_ms());
    else if (tool == TOOL_LOOP_ACTIVE) {
        loop_active = direction > 0 && loop_in >= 0 && loop_out > loop_in;
        overview_dirty = true;
    }
    else if (tool == TOOL_CUE_SLOT || tool == TOOL_CUE_DELETE)
        cue_slot = (cue_slot + (direction > 0 ? 1 : 15)) & 15;
    else if (tool == TOOL_CUE_COLOR) {
        stage_active_edit();
        hotcue_colors[cue_slot] =
            (hotcue_colors[cue_slot] + (direction > 0 ? 1 : 7)) & 7;
        overview_dirty = true;
    } else if (tool == TOOL_GRID_NUDGE) {
        stage_active_edit();
        grid_offset += direction;
    } else if (tool == TOOL_GRID_BPM) {
        stage_active_edit();
        grid_bpm_x100 = MAX(3000, MIN(30000,
                                     grid_bpm_x100 + direction));
    } else if (tool == TOOL_GRID_QUANTIZE) {
        stage_active_edit();
        quantize = direction > 0;
    } else if (tool == TOOL_META_RATING) {
        stage_active_edit();
        rating = MAX(0, MIN(5, rating + direction));
        force_full_redraw = true;
    } else if (tool == TOOL_META_COLOR) {
        stage_active_edit();
        color_index = (color_index + (direction > 0 ? 1 : 7)) & 7;
        force_full_redraw = true;
    } else if (tool == TOOL_META_YEAR) {
        stage_active_edit();
        track_year = MAX(0, MIN(9999, track_year + direction));
        force_full_redraw = true;
    }
}

static void adjust_active_tool_coarse(int direction)
{
    enum rbprep_tool tool = active_tool();

    if (tool == TOOL_SEEK && quantize)
        beat_jump(direction);
    else if (tool == TOOL_SEEK || tool == TOOL_CUE_MOVE ||
             tool == TOOL_GRID_ORIGIN)
        seek_by(direction * 1000, true);
    else if (tool == TOOL_GAIN) {
        int i;
        for (i = 0; i < 5; i++)
            change_volume(direction);
    } else if (tool == TOOL_HOST_RPM) {
        host_rpm_index = (host_rpm_index +
            (direction > 0 ? 1 : ARRAYLEN(rpm_styles) - 1)) %
            ARRAYLEN(rpm_styles);
        apply_playback_rate();
        force_full_redraw = true;
    } else if (tool == TOOL_PLAY_RPM) {
        played_rpm_index = (played_rpm_index +
            (direction > 0 ? 1 : ARRAYLEN(rpm_styles) - 1)) %
            ARRAYLEN(rpm_styles);
        apply_playback_rate();
        force_full_redraw = true;
    } else if (tool == TOOL_PITCH_BEND) {
        pitch_bend_x100 = MAX(-1600, MIN(1600,
                                pitch_bend_x100 + direction * 100));
        apply_playback_rate();
        force_full_redraw = true;
    } else if (tool == TOOL_TEMPO) {
        tempo_x100 = MAX(5000, MIN(20000,
                                   tempo_x100 + direction * 100));
        apply_playback_rate();
        force_full_redraw = true;
    } else if (tool == TOOL_GRID_NUDGE) {
        stage_active_edit();
        grid_offset += direction * 10;
    } else if (tool == TOOL_GRID_BPM) {
        stage_active_edit();
        grid_bpm_x100 = MAX(3000, MIN(30000,
                                     grid_bpm_x100 + direction * 100));
    } else if (tool == TOOL_LOOP_IN || tool == TOOL_LOOP_OUT) {
        adjust_loop_marker(tool, direction * 1000);
    } else if (tool == TOOL_META_YEAR) {
        stage_active_edit();
        track_year = MAX(0, MIN(9999, track_year + direction * 10));
    }
    else
        adjust_active_tool(direction);
}

static bool service_loop_playback(void)
{
    int status = rb->audio_status();

    if (!loop_active || loop_in < 0 || loop_out <= loop_in ||
        seek_state != SEEK_IDLE || !(status & AUDIO_STATUS_PLAY) ||
        (status & AUDIO_STATUS_PAUSE) || playhead < loop_out)
        return false;
    jump_to_time(loop_in);
    return true;
}

static bool service_playlist_playback(void)
{
    bool loaded = !!(rb->audio_status() & AUDIO_STATUS_PLAY);
    bool finished = playlist_playback && audio_was_running && !loaded &&
                    seek_state == SEEK_IDLE && playing_track_row >= 0 &&
                    playhead >= MAX(0, track_length - 3000);

    audio_was_running = loaded;
    if (!finished)
        return false;
    if (playing_track_row + 1 >= track_row_count) {
        rb->splash(HZ, "End of playlist");
        restore_black_canvas();
        return true;
    }

    track_selection = playing_track_row + 1;
    if (track_selection >= track_top + RBPREP_LIST_ROWS)
        track_top = track_selection - RBPREP_LIST_ROWS + 1;
    return play_track_row(track_selection);
}

static void select_tool(enum rbprep_tool tool)
{
    if (tool < TOOL_GRID_NUDGE) {
        mode = MODE_DECK;
        deck_tool = tool - TOOL_SEEK;
    } else if (tool < TOOL_CUE_SLOT) {
        mode = MODE_GRID;
        grid_tool = tool - TOOL_GRID_NUDGE;
    } else if (tool < TOOL_LOOP_LENGTH) {
        mode = MODE_CUES;
        cue_tool = tool - TOOL_CUE_SLOT;
    } else if (tool < TOOL_META_RATING) {
        mode = MODE_LOOP;
        loop_tool = tool - TOOL_LOOP_LENGTH;
    } else {
        mode = MODE_METADATA;
        metadata_tool = tool - TOOL_META_RATING;
    }
}

static void browse_tool_menu(int direction)
{
    int tool = active_tool();

    tool = (tool + (direction > 0 ? 1 : TOOL_COUNT - 1)) % TOOL_COUNT;
    discard_staged_edit();
    finish_cue_audition();
    select_tool(tool);
    force_full_redraw = true;
}

static void handle_escape_once(void)
{
    if (tool_menu_active) {
        select_tool(tool_menu_original);
        tool_menu_active = false;
        force_full_redraw = true;
        return;
    }

    discard_staged_edit();
    stop_editor_audio();
    if (mode == MODE_LIBRARY) {
        force_full_redraw = true;
    } else if (mode == MODE_FILTER) {
        mode = MODE_TRACKS;
    } else if (mode == MODE_USB || mode == MODE_SETTINGS ||
               mode == MODE_PENDING || mode == MODE_INDEX) {
        mode = MODE_LIBRARY;
    } else if (mode == MODE_GENRES) {
        mode = MODE_METADATA;
    } else if (mode == MODE_PLAYLISTS &&
               tree_parent != RBPREP_ROOT_NODE) {
        struct rbprep_node_record parent;
        if (read_node_record(tree_parent, &parent))
            refresh_tree_children(parent.parent);
        else
            refresh_tree_children(RBPREP_ROOT_NODE);
        tree_selection = tree_top = 0;
    } else if (mode == MODE_PLAYLISTS && playlist_add_mode) {
        playlist_add_mode = false;
        mode = MODE_DECK;
    } else if (mode == MODE_TRACKS && active_playlist_node >= 0) {
        mode = MODE_PLAYLISTS;
    } else if (mode >= MODE_DECK &&
               (deck_return_mode == MODE_TRACKS ||
                deck_return_mode == MODE_PENDING)) {
        mode = deck_return_mode;
        if (mode == MODE_PENDING)
            refresh_pending_summary();
    } else {
        mode = MODE_LIBRARY;
    }
    force_full_redraw = true;
}

static void draw_rekordpod_boot_splash(void)
{
    static const int8_t dot_x[12] = {
        18, 16, 9, 0, -9, -16, -18, -16, -9, 0, 9, 16
    };
    static const int8_t dot_y[12] = {
        0, 9, 16, 18, 16, 9, 0, -9, -16, -18, -16, -9
    };
    const char *left_word = "rekord";
    const char *right_word = "pod";
    int font_id = rb->font_load("/.rockbox/fonts/15-Adobe-Helvetica.fnt");
    int left_width;
    int right_width;
    int word_x;
    int frame;

    rb->lcd_set_background(LCD_BLACK);
    rb->lcd_setfont(font_id >= 0 ? font_id : FONT_UI);
    rb->lcd_getstringsize(left_word, &left_width, NULL);
    rb->lcd_getstringsize(right_word, &right_width, NULL);
    word_x = (LCD_WIDTH - left_width - right_width) / 2;

    for (frame = 0; frame < 12; frame++) {
        int progress_right = 82 + frame * 156 / 11;

        rb->lcd_clear_display();
        rb->lcd_set_foreground(LCD_RGBPACK(20, 25, 22));
        xlcd_fillcircle(LCD_WIDTH / 2, 71, 22);
        rb->lcd_set_foreground(LCD_RGBPACK(115, 125, 119));
        xlcd_drawcircle(LCD_WIDTH / 2, 71, 22);
        xlcd_drawcircle(LCD_WIDTH / 2, 71, 17);
        xlcd_drawcircle(LCD_WIDTH / 2, 71, 12);
        rb->lcd_set_foreground(LCD_WHITE);
        xlcd_fillcircle(LCD_WIDTH / 2, 71, 7);
        rb->lcd_set_foreground(LCD_RGBPACK(205, 215, 209));
        xlcd_fillcircle(LCD_WIDTH / 2, 71, 2);
        rb->lcd_set_foreground(RBPREP_GREEN);
        xlcd_fillcircle(LCD_WIDTH / 2 + dot_x[frame],
                        71 + dot_y[frame], 2);

        text(word_x, 108, left_word, LCD_WHITE);
        text(word_x + left_width, 108, right_word, RBPREP_GREEN);
        centered_text(0, LCD_WIDTH, 132,
                      "DEVICE LIBRARY / PREP DECK",
                      LCD_RGBPACK(145, 160, 150));

        rb->lcd_set_foreground(LCD_RGBPACK(16, 38, 25));
        rb->lcd_fillrect(82, 155, 156, 4);
        rb->lcd_set_foreground(RBPREP_GREEN);
        rb->lcd_fillrect(82, 155, MAX(1, progress_right - 82), 4);
        xlcd_fillcircle(82, 156, 2);
        xlcd_fillcircle(progress_right, 156, 2);
        rb->lcd_update();
        rb->sleep(MAX(1, HZ / 20));
    }
    rb->lcd_setfont(FONT_SYSFIXED);
    if (font_id >= 0)
        rb->font_unload(font_id);
    rb->lcd_clear_display();
    rb->lcd_update();
}

enum plugin_status plugin_start(const void *parameter)
{
    int button;
    int pressed = BUTTON_NONE;
    bool select_hold_fired = false;
    bool redraw = true;
    long frame_deadline;

    atexit(rbprep_cleanup);
#ifdef HAS_BUTTON_HOLD
    display_locked = rb->button_hold();
#else
    display_locked = false;
#endif
    set_storage_performance_mode(!display_locked);
    if (display_locked)
        backlight_use_settings();
    else
        backlight_ignore_timeout();
#ifdef HAVE_BACKLIGHT
    if (!display_locked)
        rb->backlight_on();
#endif
    rb->lcd_setfont(FONT_SYSFIXED);
    if (parameter && !rb->strcmp((const char *)parameter, "autoboot"))
        draw_rekordpod_boot_splash();
    mode = MODE_LIBRARY;
    selection = 0;
    playhead = 0;
    zoom = 1;
    grid_offset = 0;
    grid_beat_shift = 0;
    cue_slot = 0;
    deck_tool = grid_tool = cue_tool = loop_tool = metadata_tool = 0;
    rating = 0;
    color_index = 0;
    track_year = 0;
    quantize = true;
    playlist_playback = false;
    playlist_add_mode = false;
    playing_track_row = -1;
    audio_was_running = !!(rb->audio_status() & AUDIO_STATUS_PLAY);
    confirm_active = false;
    exit_requested = false;
    staged_tool = -1;
    cue_audition_active = false;
    cue_audition_latched = false;
    overview_playhead_white = true;
    seek_state = SEEK_IDLE;
    suppress_menu = suppress_play = false;
    suppress_left = suppress_right = false;
    tool_menu_active = false;
    menu_button_down = false;
    menu_hold_fired = false;
    force_full_redraw = true;
    selected_title[0] = selected_artist[0] = selected_genre[0] = '\0';
    selected_key[0] = selected_extension[0] = '\0';
    selected_track_index = selected_track_id = -1;
    settings_selection = genre_selection = genre_top = 0;
    filter_selection = 0;
    track_sort_key = TRACK_SORT_TITLE;
    track_sort_descending = false;
    track_search[0] = '\0';
    search_active = false;
    search_result_count = 0;
    pending_selection = pending_top = 0;
    track_edit_dirty = false;
    usb_selection = rb->global_settings->usb_audio != 0;
    host_rpm_index = played_rpm_index = 0;
    pitch_bend_x100 = 0;
    tempo_x100 = PITCH_SPEED_100;
    original_pitch = rb->sound_get_pitch();
    original_stretch = rb->dsp_get_timestretch();
    original_timestretch_enabled =
        rb->global_settings->timestretch_enabled;
    playback_rate_changed = false;
    configfile_load(RBPREP_CONFIG_FILE, rbprep_config,
                    ARRAYLEN(rbprep_config), RBPREP_CONFIG_VERSION);
    waveform_half = !!waveform_half;
    visualizer_mode = MAX(0, MIN((int)ARRAYLEN(visualizer_styles) - 1,
                                 visualizer_mode));
    save_on_track_load = !!save_on_track_load;
    scrub_step_index = MAX(0, MIN((int)ARRAYLEN(scrub_steps) - 1,
                                  scrub_step_index));
    host_rpm_index = MAX(0, MIN((int)ARRAYLEN(rpm_styles) - 1,
                                host_rpm_index));
    played_rpm_index = MAX(0, MIN((int)ARRAYLEN(rpm_styles) - 1,
                                  played_rpm_index));
    tempo_x100 = MAX(5000, MIN(20000, tempo_x100));
    apply_playback_rate();
    start_spectrum_capture();
    rebuild_waveform_height_lut();
    clear_analysis();
    open_library_index();
    load_genre_rollup();
    refresh_pending_summary();
    reset_play_clock(playhead, *rb->current_tick);
    overview_deadline = *rb->current_tick;
    hud_scroll_deadline = *rb->current_tick;
    frame_deadline = *rb->current_tick;

    /* Discard the release of SELECT used to launch the plugin. */
    rb->button_clear_queue();
    while (true) {
        struct mp3entry *id3 = rb->audio_current_track();
        if (service_display_lock())
            redraw = true;
        if (selected_track_id >= 0 && id3 && id3->length > 0) {
            track_length = id3->length;
            if (track_year == 0 && id3->year > 0) {
                track_year = id3->year;
                force_full_redraw = true;
            }
        }
        if (service_audio_seek())
            redraw = true;
        if (update_play_clock())
            redraw = true;
        if (service_loop_playback())
            redraw = true;
        if (service_playlist_playback())
            redraw = true;
        if (exit_requested) {
            stop_editor_audio();
            if (library_fd >= 0)
                rb->close(library_fd);
            if (genre_fd >= 0)
                rb->close(genre_fd);
            return PLUGIN_OK;
        }

        if (mode >= MODE_DECK && !display_locked && !redraw) {
            if (hud_scroll_active &&
                !TIME_BEFORE(*rb->current_tick, hud_scroll_deadline)) {
                draw_track_header(true);
                rb->lcd_update_rect(0, 0, LCD_WIDTH, 32);
                hud_scroll_deadline = *rb->current_tick +
                                      RBPREP_HUD_SCROLL_TICKS;
            }
            if (!TIME_BEFORE(*rb->current_tick, overview_deadline)) {
                overview_playhead_white = !overview_playhead_white;
                draw_overview_waveform();
                draw_tool_row();
                rb->lcd_update_rect(0, RBPREP_OVERVIEW_Y, LCD_WIDTH,
                                    RBPREP_WAVE_TOP - RBPREP_OVERVIEW_Y);
                overview_deadline = *rb->current_tick +
                                    RBPREP_OVERVIEW_TICKS;
            }
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
        if (!confirm_active && menu_button_down && !menu_hold_fired &&
            (rb->button_status() & BUTTON_MENU) &&
            !TIME_BEFORE(*rb->current_tick,
                         menu_pressed_tick + MAX(1, HZ / 4))) {
            menu_hold_fired = true;
            pressed = BUTTON_NONE;
            handle_escape_once();
            redraw = true;
        }
        if (confirm_active) {
            if (confirm_wait_release) {
                if (!(rb->button_status() & BUTTON_SELECT))
                    confirm_wait_release = false;
                continue;
            }
            if (button == BUTTON_LEFT || button == BUTTON_SCROLL_BACK ||
                button == (BUTTON_SCROLL_BACK | BUTTON_REPEAT)) {
                confirm_ok = false;
                force_full_redraw = true;
            } else if (button == BUTTON_RIGHT ||
                       button == BUTTON_SCROLL_FWD ||
                       button == (BUTTON_SCROLL_FWD | BUTTON_REPEAT)) {
                confirm_ok = true;
                force_full_redraw = true;
            } else if (button == (BUTTON_MENU | BUTTON_REL)) {
                finish_confirmation(false);
            } else if (button == (BUTTON_SELECT | BUTTON_REL)) {
                finish_confirmation(confirm_ok);
            } else if (rb->default_event_handler(button) ==
                       SYS_USB_CONNECTED) {
                stop_editor_audio();
                if (library_fd >= 0)
                    rb->close(library_fd);
                if (genre_fd >= 0)
                    rb->close(genre_fd);
                return PLUGIN_USB_CONNECTED;
            }
            continue;
        }
        switch (button) {
        case BUTTON_MENU:
            menu_button_down = true;
            menu_hold_fired = false;
            menu_pressed_tick = *rb->current_tick;
            pressed = button;
            break;
        case BUTTON_MENU | BUTTON_REPEAT:
            /* The timer above owns the single long-MENU escape. */
            break;
        case BUTTON_PLAY:
            if (cue_audition_active &&
                (rb->button_status() & BUTTON_SELECT)) {
                select_hold_fired = true;
                suppress_play = true;
                latch_cue_audition();
            } else if (!suppress_play) {
                pressed = button;
            }
            break;
        case BUTTON_SELECT:
            select_hold_fired = false;
            if (tool_menu_active) {
                pressed = BUTTON_SELECT;
            } else if (mode == MODE_CUES &&
                       active_tool() == TOOL_CUE_SLOT &&
                       hotcues[cue_slot] >= 0) {
                pressed = BUTTON_SELECT;
                start_cue_audition();
            } else {
                pressed = BUTTON_SELECT;
            }
            break;
        case BUTTON_MENU | BUTTON_REL:
            if (!menu_button_down)
                break;
            menu_button_down = false;
            pressed = BUTTON_NONE;
            if (menu_hold_fired) {
                menu_hold_fired = false;
            } else if (mode >= MODE_DECK) {
                if (tool_menu_active) {
                    browse_tool_menu(-1);
                } else {
                    tool_menu_original = active_tool();
                    tool_menu_active = true;
                }
                force_full_redraw = true;
            } else {
                handle_escape_once();
            }
            break;
        case BUTTON_PLAY | BUTTON_REL:
            if (suppress_play) {
                suppress_play = false;
                pressed = BUTTON_NONE;
                if (rb->button_status() & BUTTON_SELECT)
                    select_hold_fired = false;
                break;
            }
            if (tool_menu_active && mode >= MODE_DECK) {
                pressed = BUTTON_NONE;
                browse_tool_menu(1);
                break;
            }
            if (pressed != BUTTON_PLAY)
                break;
            pressed = BUTTON_NONE;
            if (mode == MODE_TRACKS && active_playlist_node < 0) {
                filter_selection = 0;
                mode = MODE_FILTER;
                force_full_redraw = true;
            } else if (mode == MODE_PENDING) {
                int changes = pending_snapshot_count + pending_playlist_count;
                if (changes > 0) {
                    rb->snprintf(confirm_message, sizeof(confirm_message),
                                 "BURN %d DEVICE CHANGE%s?", changes,
                                 changes == 1 ? "" : "S");
                    begin_confirmation(CONFIRM_BURN);
                }
            } else {
                toggle_playback();
            }
            break;
        case BUTTON_SELECT | BUTTON_REL:
            if (pressed != BUTTON_SELECT)
                break;
            pressed = BUTTON_NONE;
            if (tool_menu_active) {
                tool_menu_active = false;
                force_full_redraw = true;
            } else if (cue_audition_active) {
                finish_cue_audition();
            } else if (!select_hold_fired) {
                short_select();
            }
            select_hold_fired = false;
            break;
        case BUTTON_SELECT | BUTTON_REPEAT:
            if (pressed == BUTTON_SELECT && !select_hold_fired &&
                !tool_menu_active) {
                select_hold_fired = true;
                if (cue_audition_active)
                    latch_cue_audition();
                else
                    long_select();
            }
            break;
        case BUTTON_SELECT | BUTTON_LEFT:
        case BUTTON_SELECT | BUTTON_LEFT | BUTTON_REPEAT:
        case BUTTON_SELECT | BUTTON_RIGHT:
        case BUTTON_SELECT | BUTTON_RIGHT | BUTTON_REPEAT:
        case BUTTON_SELECT | BUTTON_MENU:
        case BUTTON_SELECT | BUTTON_MENU | BUTTON_REPEAT:
        case BUTTON_SELECT | BUTTON_PLAY:
        case BUTTON_SELECT | BUTTON_PLAY | BUTTON_REPEAT:
            /* Tool and page chords are intentionally retired. */
            break;
        case BUTTON_SELECT | BUTTON_LEFT | BUTTON_REL:
        case BUTTON_SELECT | BUTTON_RIGHT | BUTTON_REL:
        case BUTTON_SELECT | BUTTON_MENU | BUTTON_REL:
        case BUTTON_SELECT | BUTTON_PLAY | BUTTON_REL:
            pressed = BUTTON_NONE;
            break;
        case BUTTON_SCROLL_FWD:
        case BUTTON_SCROLL_FWD | BUTTON_REPEAT:
            if (tool_menu_active && mode >= MODE_DECK)
                browse_tool_menu(1);
            else if (mode == MODE_LIBRARY)
                selection = MIN(7, selection + 1);
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
            } else if (mode == MODE_FILTER) {
                filter_selection = MIN(7, filter_selection + 1);
            } else if (mode == MODE_PENDING &&
                       pending_snapshot_count + pending_playlist_count > 0) {
                pending_selection = MIN(pending_snapshot_count +
                                        pending_playlist_count - 1,
                                        pending_selection + 1);
                if (pending_selection >= pending_top + RBPREP_PENDING_ROWS)
                    pending_top = pending_selection - RBPREP_PENDING_ROWS + 1;
            }
            else if (mode == MODE_USB)
                usb_selection = 1;
            else if (mode == MODE_SETTINGS)
                settings_selection = MIN(2, settings_selection + 1);
            else if (mode == MODE_GENRES) {
                genre_selection = MIN(genre_count, genre_selection + 1);
                if (genre_selection >= genre_top + RBPREP_LIST_ROWS)
                    genre_top = genre_selection - RBPREP_LIST_ROWS + 1;
            } else if (mode != MODE_PENDING && mode != MODE_INDEX)
                adjust_active_tool(1);
            break;
        case BUTTON_SCROLL_BACK:
        case BUTTON_SCROLL_BACK | BUTTON_REPEAT:
            if (tool_menu_active && mode >= MODE_DECK)
                browse_tool_menu(-1);
            else if (mode == MODE_LIBRARY)
                selection = MAX(0, selection - 1);
            else if (mode == MODE_PLAYLISTS) {
                tree_selection = MAX(0, tree_selection - 1);
                if (tree_selection < tree_top)
                    tree_top = tree_selection;
            } else if (mode == MODE_TRACKS) {
                track_selection = MAX(0, track_selection - 1);
                if (track_selection < track_top)
                    track_top = track_selection;
            } else if (mode == MODE_FILTER) {
                filter_selection = MAX(0, filter_selection - 1);
            } else if (mode == MODE_PENDING) {
                pending_selection = MAX(0, pending_selection - 1);
                if (pending_selection < pending_top)
                    pending_top = pending_selection;
            } else if (mode == MODE_USB)
                usb_selection = 0;
            else if (mode == MODE_SETTINGS)
                settings_selection = MAX(0, settings_selection - 1);
            else if (mode == MODE_GENRES) {
                genre_selection = MAX(0, genre_selection - 1);
                if (genre_selection < genre_top)
                    genre_top = genre_selection;
            } else if (mode != MODE_PENDING && mode != MODE_INDEX)
                adjust_active_tool(-1);
            break;
        case BUTTON_LEFT:
            if (suppress_left)
                break;
            if (tool_menu_active && mode >= MODE_DECK) {
                browse_tool_menu(-1);
                pressed = BUTTON_NONE;
            } else if (mode >= MODE_DECK)
                pressed = BUTTON_LEFT;
            break;
        case BUTTON_LEFT | BUTTON_REPEAT:
            if (tool_menu_active && mode >= MODE_DECK)
                browse_tool_menu(-1);
            else if (!suppress_left && mode >= MODE_DECK)
                adjust_active_tool_coarse(-1);
            break;
        case BUTTON_LEFT | BUTTON_REL:
            if (suppress_left) {
                suppress_left = false;
                pressed = BUTTON_NONE;
                if (rb->button_status() & BUTTON_SELECT)
                    select_hold_fired = false;
            } else if (pressed == BUTTON_LEFT && mode >= MODE_DECK &&
                       !tool_menu_active) {
                pressed = BUTTON_NONE;
                adjust_active_tool_coarse(-1);
            }
            break;
        case BUTTON_RIGHT:
            if (suppress_right)
                break;
            if (tool_menu_active && mode >= MODE_DECK) {
                browse_tool_menu(1);
                pressed = BUTTON_NONE;
            } else if (mode >= MODE_DECK)
                pressed = BUTTON_RIGHT;
            break;
        case BUTTON_RIGHT | BUTTON_REPEAT:
            if (tool_menu_active && mode >= MODE_DECK)
                browse_tool_menu(1);
            else if (!suppress_right && mode >= MODE_DECK)
                adjust_active_tool_coarse(1);
            break;
        case BUTTON_RIGHT | BUTTON_REL:
            if (suppress_right) {
                suppress_right = false;
                pressed = BUTTON_NONE;
                if (rb->button_status() & BUTTON_SELECT)
                    select_hold_fired = false;
            } else if (pressed == BUTTON_RIGHT && mode >= MODE_DECK &&
                       !tool_menu_active) {
                pressed = BUTTON_NONE;
                adjust_active_tool_coarse(1);
            }
            break;
        default:
            if (rb->default_event_handler(button) == SYS_USB_CONNECTED) {
                stop_editor_audio();
                if (library_fd >= 0)
                    rb->close(library_fd);
                if (genre_fd >= 0)
                    rb->close(genre_fd);
                return PLUGIN_USB_CONNECTED;
            }
            break;
        }
    }
}
