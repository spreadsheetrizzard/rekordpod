#include "plugin.h"
#include "lib/configfile.h"
#include "lib/helper.h"
#include "lib/pluginlib_exit.h"
#include "lib/xlcd.h"
#include "rbprep_caps.h"
#include "rbprep_grid.h"
#include "rbprep_store.h"
#include "rbprep_wave.h"
#include "rbprep_wave_index.h"

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
#define RBPREP_STATUS_HEIGHT 8
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
#define RBPREP_MENU_FRAME_TICKS MAX(1, HZ / 25)
#define RBPREP_HUD_SCROLL_TICKS MAX(1, HZ / 10)
#define RBPREP_STATUS_TICKS MAX(1, HZ)
#define RBPREP_CONFIG_VERSION 4
#define RBPREP_CONFIG_FILE "/.rockbox/rbprep/rbprep.cfg"
#define RBPREP_DEVICE_NAME_FILE "/.rockbox/rbprep/device-name.txt"
#define RBPREP_USB_STATUS "/.rockbox/rbprep/usb-status.rbs"
#define RBPREP_AUTOBOOT_OFF "/.rockbox/rbprep/autoboot.off"
#define RBPREP_MACRO_FILE_A "/.rockbox/rbprep/state/macros.a"
#define RBPREP_MACRO_FILE_B "/.rockbox/rbprep/state/macros.b"
#define RBPREP_MACRO_LEGACY_FILE "/.rockbox/rbprep/tool-macros.rbm"
#define RBPREP_MACRO_LEGACY_TMP "/.rockbox/rbprep/tool-macros.rbm.tmp"
#define RBPREP_MACRO_LEGACY_PREV "/.rockbox/rbprep/tool-macros.rbm.prev"
#define RBPREP_MACRO_LEGACY_ROOT "/.rekordpod-macros.rbm"
#define RBPREP_MACRO_LEGACY_ROOT_TMP "/.rekordpod-macros.rbm.tmp"
#define RBPREP_MACRO_LEGACY_ROOT_PREV "/.rekordpod-macros.rbm.prev"
#define RBPREP_MACRO_LINK_FILE "/.rekordpod-workflows.rbl"
#define RBPREP_MACRO_LINK_TMP "/.rekordpod-workflows.rbl.tmp"
#define RBPREP_MACRO_LINK_LEGACY \
    "/.rockbox/rbprep/playlist-workflows.rbl"
#define RBPREP_SMART_QUERY_FILE "/.rockbox/rbprep/smart-playlists.rbq"
#define RBPREP_PLAYLIST_JOURNAL "/.rockbox/rbprep/playlist-adds.rba"
#define RBPREP_EDIT_JOURNAL "/.rockbox/rbprep/edits.rbe"
#define RBPREP_EDIT_JOURNAL_REPAIR RBPREP_EDIT_JOURNAL ".rbprep-repair"
#define RBPREP_EDIT_JOURNAL_BAD RBPREP_EDIT_JOURNAL ".rbprep-corrupt"
#define RBPREP_PLAYLIST_JOURNAL_REPAIR \
    RBPREP_PLAYLIST_JOURNAL ".rbprep-repair"
#define RBPREP_PLAYLIST_JOURNAL_BAD \
    RBPREP_PLAYLIST_JOURNAL ".rbprep-corrupt"
#define RBPREP_BURN_STATE "/.rockbox/rbprep/local-burn.rbs"
#define RBPREP_SEARCH_RESULTS "/.rockbox/rbprep/state/search.results"
/* Fixed-size, little-endian RBE1 snapshots make interrupted appends harmless
   and keep the eventual macOS importer independent of compiler struct layout. */
#define RBPREP_EDIT_RECORD_SIZE 216
#define RBPREP_PENDING_ROWS 8
#define RBPREP_PENDING_MAX 64
#define RBPREP_SPECTRUM_BANDS 20
#define RBPREP_CHROMA_BANDS 12
#define RBPREP_SPECTRUM_HISTORY 16
#define RBPREP_PCM_FRAMES 1024
#define RBPREP_GREEN theme_accent
#define RBPREP_GREEN_DIM theme_accent_dim
#define RBPREP_MENU_TEXT LCD_RGBPACK(188, 205, 193)
#define RBPREP_TOOL_PAGE_MAX 5
#define RBPREP_SELECT_DOUBLE_TICKS MAX(1, HZ * 3 / 10)
#define RBPREP_SETTINGS_LAST 13
#define RBPREP_MAIN_DISSOLVE_STEPS 16
#define RBPREP_MAIN_TRANSITION_FRAME_TICKS MAX(1, HZ / 30)
#define RBPREP_MAIN_SCREEN_X 101
#define RBPREP_MAIN_SCREEN_Y 26
#define RBPREP_MAIN_SCREEN_W 118
#define RBPREP_MAIN_SCREEN_H 55
#define RBPREP_RETURN_THUMB_W 160
#define RBPREP_RETURN_THUMB_H 120
#define RBPREP_HIGH_RES_WINDOW_WIDTH 112
#define RBPREP_WHEEL_ARM_UNITS 3
#define RBPREP_MACRO_COUNT 2
#define RBPREP_MACRO_STEPS 48
#define RBPREP_MACRO_LEGACY_STEPS 24
#define RBPREP_MACRO_NAME 24
#define RBPREP_MACRO_CHOOSE (-2147483647 - 1)
#define RBPREP_MACRO_LOCK_WHEEL 0x01
#define RBPREP_MACRO_LINKS 128
#define RBPREP_SMART_QUERIES 128
#define RBPREP_DAC_INITIAL_GAIN_DB (-50)
#define RBPREP_MACRO_STEP_SIZE 8
#define RBPREP_MACRO_V5_HEADER 24
#define RBPREP_MACRO_V5_PAYLOAD \
    (4 + RBPREP_MACRO_COUNT * \
     (RBPREP_MACRO_NAME + 1 + \
      RBPREP_MACRO_STEPS * RBPREP_MACRO_STEP_SIZE))
#define RBPREP_MACRO_V5_SIZE \
    (RBPREP_MACRO_V5_HEADER + RBPREP_MACRO_V5_PAYLOAD)
#define RBPREP_TRACK_COLOR_NONE 8
#define RBPREP_TRACK_COLOR_COUNT 9

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
    MODE_PLAYLIST_ACTIONS,
    MODE_ACCENT,
    MODE_MACRO_ACTIONS,
    MODE_MACRO_EDITOR,
    MODE_MACRO_PICKER,
    MODE_MACRO_VALUE,
    MODE_DECK,
    MODE_TEMPO,
    MODE_GRID,
    MODE_CUES,
    MODE_LOOP,
    MODE_METADATA,
    MODE_LIST,
    MODE_PNAV,
    MODE_VISUALIZER,
    MODE_MACRO,
    MODE_VISUALIZER_TWO,
    MODE_PITCH
};

enum rbprep_color_target {
    COLOR_TARGET_ACCENT,
    COLOR_TARGET_BODY,
    COLOR_TARGET_WHEEL
};

enum rbprep_key_display {
    KEY_DISPLAY_CHROMATIC,
    KEY_DISPLAY_CAMELOT,
    KEY_DISPLAY_ORIGINAL
};

enum rbprep_tool {
    /* These values are persisted in tool-macros.rbm. Never renumber an
       existing tool: append new values immediately before TOOL_COUNT. */
    TOOL_SEEK = 0,
    TOOL_SCRUB_STEP = 1,
    TOOL_ZOOM = 2,
    TOOL_GAIN = 3,
    TOOL_HOST_RPM = 4,
    TOOL_PLAY_RPM = 5,
    TOOL_PITCH_BEND = 6,
    TOOL_TEMPO = 7,
    TOOL_PLAYLIST_MODE = 8,
    TOOL_ADD_PLAYLIST = 9,
    TOOL_WAVEFORM_STYLE = 10,
    TOOL_KEYLOCK = 11,
    TOOL_VIS_BOOMBOX = 12,
    TOOL_VIS_EQ = 13,
    TOOL_VIS_TURNTABLE = 14,
    TOOL_MACRO_ONE = 15,
    TOOL_MACRO_TWO = 16,
    TOOL_GRID_NUDGE = 17,
    TOOL_GRID_BPM = 18,
    TOOL_GRID_ORIGIN = 19,
    TOOL_GRID_QUANTIZE = 20,
    TOOL_CUE_SLOT = 21,
    TOOL_CUE_MOVE = 22,
    TOOL_CUE_COLOR = 23,
    TOOL_CUE_DELETE = 24,
    TOOL_LOOP_LENGTH = 25,
    TOOL_LOOP_IN = 26,
    TOOL_LOOP_OUT = 27,
    TOOL_LOOP_ACTIVE = 28,
    TOOL_META_RATING = 29,
    TOOL_META_COLOR = 30,
    TOOL_META_YEAR = 31,
    TOOL_META_GENRE = 32,
    TOOL_BURN_SONG = 33,
    TOOL_BURN_ALL = 34,
    TOOL_PLAYLIST_PREVIOUS = 35,
    TOOL_PLAYLIST_NEXT = 36,
    TOOL_RESTART_PLAYBACK = 37,
    TOOL_RELOAD_TRACK = 38,
    TOOL_VIS_CANYON = 39,
    TOOL_VIS_ORBIT = 40,
    TOOL_VIS_PHRASE = 41,
    TOOL_VIS_HARMONIC = 42,
    TOOL_META_KEY = 43,
    TOOL_KEY_NOTATION = 44,
    TOOL_FAVORITE_ONE = 45,
    TOOL_FAVORITE_TWO = 46,
    TOOL_IPOD_SEEK = 47,
    TOOL_COUNT = 48
};

enum rbprep_confirm_action {
    CONFIRM_NONE,
    CONFIRM_EXIT,
    CONFIRM_DECK_CUE,
    CONFIRM_CUE_DELETE,
    CONFIRM_GRID_ORIGIN,
    CONFIRM_KEEP_EDIT,
    CONFIRM_ADD_PLAYLIST,
    CONFIRM_GENRE,
    CONFIRM_BURN,
    CONFIRM_DELETE_PENDING,
    CONFIRM_DELETE_PLAYLIST,
    CONFIRM_PLAYLIST_CREATE,
    CONFIRM_PLAYLIST_RENAME,
    CONFIRM_PLAYLIST_MOVE,
    CONFIRM_PLAYLIST_NODE_DELETE,
    CONFIRM_MACRO_CLEAR,
    CONFIRM_MACRO_DELETE_STEP,
    CONFIRM_BURN_ALL_NOW,
    CONFIRM_TRACK_LOAD,
    CONFIRM_PLAYLIST_SEED
};

enum rbprep_burn_request {
    BURN_REQUEST_NONE,
    BURN_REQUEST_ALL
};

enum rbprep_playlist_operation {
    PLAYLIST_OP_ADD = 'A',
    PLAYLIST_OP_CREATE = 'C',
    PLAYLIST_OP_RENAME = 'R',
    PLAYLIST_OP_MOVE = 'M',
    PLAYLIST_OP_DELETE = 'D'
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
    unsigned char operation;
    unsigned char kind;
    uint32_t track_id;
    uint32_t playlist_id;
    uint32_t parent_id;
    char name[64];
};

static enum rbprep_mode mode;
static struct rbprep_caps capabilities;
static struct rbprep_wave_reader wave_reader;
static struct rbprep_grid_reader grid_reader;
static struct rbprep_wave_index wave_index;
static int selection;
static int playhead;
static unsigned char overview_waveform[RBPREP_OVERVIEW_WIDTH][4];
static struct rbprep_wave_column waveform_columns[RBPREP_DECK_WIDTH];
static unsigned char waveform_height_lut[256];
static int waveform_column_first;
static int waveform_column_span;
static bool waveform_columns_valid;
static bool waveform_columns_exact;
static int waveform_points;
static int beat_count;
static int beat_search_hint;
static bool imported_grid_cached;
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
static long status_deadline;
static int title_scroll_px;
static int metadata_scroll_px;
static bool hud_scroll_active;
static int deck_tool;
static int tempo_tool;
static int grid_tool;
static int cue_tool;
static int loop_tool;
static int metadata_tool;
static int pitch_tool;
static int visualizer_tool;
static int visualizer_two_tool;
static int list_tool;
static int pnav_tool;
static int macro_tool;
static int macro_tool_override = -1;
static bool select_click_pending;
static bool select_double_consumed;
static long select_click_deadline;
static bool seek_portal_active;
static enum rbprep_mode seek_return_mode;
static int seek_return_tool;
static int seek_return_macro_active;
static int seek_return_macro_position;
static bool seek_return_macro_lock;
static int seek_return_macro_override;
static int waveform_half;
static int visualizer_mode;
static int save_on_track_load;
static int auto_burn;
static int click_sound;
static int platter_wheel_mode = 1;
static int ipod_seek_first;
static int keylock_enabled = 1;
static int key_notation;
static int autoplay_enabled = 1;
static int autoboot_enabled = 1;
static int favorite_playlist_ids[2];
static int total_plays;
static int accent_hue = 145;
static int accent_saturation = 70;
static int accent_brightness = 92;
static int accent_component;
static int color_picker_target;
static int body_hue = 210;
static int body_saturation = 5;
static int body_brightness = 88;
static int wheel_hue;
static int wheel_saturation;
static int wheel_brightness = 14;
static int turntable_arm_style = 1;
static int turntable_headshell_style;
static int theme_accent = LCD_RGBPACK(70, 235, 125);
static int theme_accent_dim = LCD_RGBPACK(28, 75, 48);
static int theme_body = LCD_RGBPACK(220, 224, 221);
static int theme_body_shadow = LCD_RGBPACK(70, 76, 72);
static int theme_wheel = LCD_RGBPACK(25, 28, 26);
static int theme_wheel_outline = LCD_RGBPACK(145, 153, 148);
static int main_wheel_phase_fp;
static int main_wheel_velocity_fp;
static int main_wheel_touch_position;
static long main_wheel_motion_tick;
static long main_name_scroll_deadline;
static bool main_transition_thumbnail_valid;
static fb_data main_transition_thumbnail[RBPREP_RETURN_THUMB_W *
                                         RBPREP_RETURN_THUMB_H];
static unsigned short main_transition_x_lut[LCD_WIDTH];
static unsigned short main_transition_y_lut[LCD_HEIGHT];
static struct viewport *main_viewport;
static bool transition_render_only;
static int activity_ticker_progress;
static long activity_ticker_until;
static long activity_ticker_last_update;
static unsigned char activity_ticker_burst;
static char device_usb_name[32] = "REKORDPOD";
static int scrub_step_index = 4;
static int settings_selection;
static int usb_selection;
static int dac_saved_volume;
static bool dac_gain_override;
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
static int pcm_snapshot_frames;
static volatile int pcm_capture_index;
static volatile int pcm_capture_frames[2];
static volatile unsigned int pcm_capture_generation;
static bool spectrum_capture_active;
static volatile unsigned long pcm_sample_rate = 44100;
static unsigned int spectrum_generation;
static long spectrum_deadline;
static unsigned char spectrum_levels[RBPREP_SPECTRUM_BANDS];
static unsigned char chroma_levels[RBPREP_CHROMA_BANDS];
static unsigned char spectrum_history[RBPREP_SPECTRUM_HISTORY]
                                     [RBPREP_SPECTRUM_BANDS];
static int spectrum_history_head = -1;
static unsigned int spectrum_history_generation;
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
static enum rbprep_burn_request burn_request;
static bool playlist_playback;
static bool playlist_add_mode;
static bool playlist_move_mode;
static int playlist_action_selection;
static int playlist_action_node = -1;
static int playlist_action_parent = -1;
static uint32_t playlist_action_id;
static uint32_t playlist_action_parent_id;
static char playlist_action_name[64];
static int playing_track_row = -1;
static bool force_track_reload;
static bool audio_was_running;
static bool confirm_active;
static bool confirm_ok;
static int confirm_choice;
static bool confirm_wait_release;
static bool exit_requested;
static enum rbprep_confirm_action confirm_action;
static char confirm_message[64];
static int deferred_track_index = -1;
static int deferred_track_row = -1;
static enum rbprep_mode deferred_track_return_mode;
static bool deferred_track_force_reload;
static int confirm_slot;
static int confirm_time;
static int confirm_playlist_node;
static int staged_tool = -1;
static int staged_original;
static int staged_cue_slot = -1;
static char staged_original_key[24];
static int pending_snapshot_count;
static int pending_playlist_count;
static bool pending_summary_overflow;
static bool pending_journal_invalid;
static int pending_visible_count;
static struct rbprep_pending_entry pending_entries[RBPREP_PENDING_MAX];
static struct rbprep_pending_playlist
    pending_playlists[RBPREP_PENDING_MAX];
static int pending_selection;
static int pending_top;
static int recent_tracks_30d;
static bool recent_tracks_ready;
static int uptime_tracks_burned;
static int play_stat_track_id = -1;
static long play_stat_last_tick;
static long play_stat_ticks;
static bool play_stat_counted;
static long storage_keepalive_deadline;
static bool config_dirty;
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
static int macro_chord_button;
static bool tool_menu_active;
static enum rbprep_tool tool_menu_original;
static bool menu_button_down;
static bool menu_hold_fired;
static long menu_pressed_tick;
static int select_pressed_time;
static int loop_length_index = 7;
static int loop_in = -1;
static int loop_out = -1;
static bool loop_active;

struct rbprep_macro_step {
    unsigned char tool;
    unsigned char flags;
    int value;
};

struct rbprep_tool_macro {
    char name[RBPREP_MACRO_NAME];
    unsigned char count;
    struct rbprep_macro_step steps[RBPREP_MACRO_STEPS];
};

static struct rbprep_tool_macro tool_macros[RBPREP_MACRO_COUNT];
static int macro_active = -1;
static int macro_position;
static uint32_t macro_generation;
static int macro_generation_slot = -1;
static bool macro_store_valid;
static bool macro_dirty;
static int macro_manage_slot;
static int macro_action_selection;
static int macro_edit_position;
static int macro_edit_operation;
static int macro_picker_page;
static int macro_picker_tool;
static int macro_value_draft;
static bool macro_value_choose;
static bool macro_value_new_step;
static enum rbprep_tool macro_value_tool;
static bool macro_value_lock;
static bool macro_wheel_locked;

struct rbprep_macro_link {
    uint32_t playlist_id;
    signed char slot;
};

static struct rbprep_macro_link macro_links[RBPREP_MACRO_LINKS];
static int macro_link_count;
static uint32_t smart_query_ids[RBPREP_SMART_QUERIES];
static int smart_query_count;

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
static bool seek_prepared;
static int seek_target;
static int seek_applied_target;
static long seek_deadline;
static long seek_applied_tick;
#ifdef HAVE_WHEEL_POSITION
static int seek_wheel_touch_position;
static int seek_wheel_velocity_fp;
static int seek_wheel_delta_fp;
static int seek_wheel_arm_delta;
static bool seek_wheel_gesture_tracked;
static bool seek_wheel_blocked_until_release;
static long seek_wheel_motion_tick;
#endif
static int play_clock_anchor;
static long play_clock_tick;
static int reported_audio_elapsed = -1;
static long audio_sync_after;
static bool overview_playhead_white;
static bool display_locked;
static bool deck_cpu_boosted;
static unsigned long maximum_frame_ticks;
static unsigned long late_frame_count;

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
    uint16_t import_date;
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
    TRACK_SORT_IMPORTED,
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

struct rbprep_playlist_cache_row {
    bool valid;
    uint32_t parent;
    int ordinal;
    int node_index;
    struct rbprep_node_record node;
    char name[80];
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
static uint32_t library_string_size;
static uint32_t library_sort_offsets[TRACK_SORT_COUNT];
static uint32_t tree_parent = RBPREP_ROOT_NODE;
static int tree_selection;
static int tree_top;
static int tree_child_count;
static int tree_children[RBPREP_TREE_NODES];
static int favorite_playlist_nodes[2] = { -1, -1 };
static struct rbprep_playlist_cache_row
    playlist_cache[RBPREP_LIST_ROWS];
static char tree_parent_name[80];
static int track_selection;
static int track_top;
static int track_row_count;
static int track_sort_key;
static bool track_sort_descending;
static int filter_selection;
static char track_search[40];
static int search_result_count;
static int search_result_fd = -1;
static uint32_t shuffle_multiplier = 1;
static uint32_t shuffle_offset;
static bool search_active;
static bool collection_shuffle_active;
static int active_playlist_node = -1;
static int selected_track_index = -1;
static int selected_track_id = -1;
static enum rbprep_mode deck_return_mode = MODE_LIBRARY;
static char selected_title[96];
static char selected_artist[72];
static char selected_genre[32];
static char selected_key[24];
static char selected_comments[96];
static char selected_extension[12];
static int usb_armed_selection;
static bool usb_event_registered;
static bool usb_extract_event_registered;
static uint32_t usb_active_playlist_source_id;
static uint32_t usb_tree_parent_source_id;
static bool usb_library_context_captured;
static bool usb_persistence_warning;
static bool usb_unsaved_edit_valid;
static unsigned char usb_unsaved_edit[RBPREP_EDIT_RECORD_SIZE];

static void stop_editor_audio(void);
static void reset_play_clock(int anchor, long tick);
static bool update_play_clock(void);
static void jump_to_time(int target);
static bool flush_deferred_edit(void);
static void start_spectrum_capture(void);
static void stop_spectrum_capture(void);
static void restore_playback_rate(void);
static void draw_tool_icon(int cx, int cy, enum rbprep_tool tool, int color);
static bool draw_macro_strip(void);
static void select_tool(enum rbprep_tool tool);
static void apply_macro_step(const struct rbprep_macro_step *step);
static void activate_macro(int slot);
static void rename_macro(int slot);
static void open_macro_editor(int slot);
static void apply_macro_picker_tool(void);
static void save_macro_value(void);
static void delete_macro_step(int slot, int position);
static void clear_macro(int slot);
static bool handle_usb_system_event(int button);
static void draw_screen(void);
static bool save_rbprep_config(void);
static bool save_tool_macros(void);
static bool persist_usb_arm_state(void);
static void begin_track_load_confirmation(int index, int row,
                                          enum rbprep_mode return_mode,
                                          bool force_reload);

static void set_output_gain(int volume)
{
    volume = MAX(rb->sound_min(SOUND_VOLUME),
                 MIN(rb->sound_max(SOUND_VOLUME), volume));
    rb->global_status->volume = volume;
    rb->sound_set(SOUND_VOLUME, volume);
}

static void restore_dac_gain(void)
{
    if (!dac_gain_override)
        return;

    set_output_gain(dac_saved_volume);
    dac_gain_override = false;
}

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

static void update_deck_cpu_boost(void)
{
#ifdef HAVE_ADJUSTABLE_CPU_FREQ
    bool wanted = !display_locked && mode >= MODE_DECK;

    if (wanted == deck_cpu_boosted)
        return;
    rb->cpu_boost(wanted);
    deck_cpu_boosted = wanted;
#endif
}

static void rbprep_usb_inserted(unsigned short id, void *event_data)
{
    (void)id;
#if !defined(SIMULATOR) && !defined(USB_NONE) && defined(HAVE_USB_POWER)
    int *requested_mode = event_data;
    bool dac_requested = capabilities.usb_audio &&
                         mode == MODE_USB && usb_armed_selection == 2;

    /* This event runs before USB descriptors are configured, so establish
       the quiet DAC baseline before macOS can open the audio stream. */
    if (dac_requested && !dac_gain_override) {
        dac_saved_volume = rb->global_status->volume;
        dac_gain_override = true;
        set_output_gain(RBPREP_DAC_INITIAL_GAIN_DB);
    }

    /* Decide at the physical insertion edge. Mass storage is permitted only
       while its dedicated page is visible and DATA was explicitly saved;
       every other Rekordpod screen remains live in charge-only mode. */
    if (requested_mode)
        *requested_mode = mode == MODE_USB && usb_armed_selection == 1
                        ? USB_MODE_MASS_STORAGE : USB_MODE_CHARGE;
#else
    (void)event_data;
#endif
}

static void rbprep_usb_extracted(unsigned short id, void *event_data)
{
    (void)id;
    (void)event_data;
    restore_dac_gain();
}

static void rbprep_cleanup(void)
{
    if (usb_event_registered) {
        rb->remove_event(SYS_EVENT_USB_INSERTED, rbprep_usb_inserted);
        usb_event_registered = false;
    }
    if (usb_extract_event_registered) {
        rb->remove_event(SYS_EVENT_USB_EXTRACTED, rbprep_usb_extracted);
        usb_extract_event_registered = false;
    }
    flush_deferred_edit();
    if (macro_dirty)
        save_tool_macros();
    save_rbprep_config();
    stop_spectrum_capture();
    restore_playback_rate();
    restore_dac_gain();
    rbprep_wave_close(&wave_reader);
    rbprep_grid_close(&grid_reader);
    rbprep_wave_index_close(&wave_index);
#ifdef HAVE_ADJUSTABLE_CPU_FREQ
    if (deck_cpu_boosted) {
        rb->cpu_boost(false);
        deck_cpu_boosted = false;
    }
#endif
    if (search_result_fd >= 0) {
        rb->close(search_result_fd);
        search_result_fd = -1;
    }
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
        stop_spectrum_capture();
        set_storage_performance_mode(false);
        backlight_use_settings();
    } else {
        start_spectrum_capture();
        set_storage_performance_mode(true);
        backlight_ignore_timeout();
#ifdef HAVE_BACKLIGHT
        rb->backlight_on();
#endif
    }
    update_deck_cpu_boost();
    force_full_redraw = true;
    return true;
#else
    return false;
#endif
}

static const char *color_labels[] = {
    "SAMPLE", "OPENER", "BUILDER", "PIVOTER",
    "MAINTAINER", "PEAK", "RESET", "TOOL", "NO COLOR"
};

static const char *cue_color_names[] = {
    "RED", "ORANGE", "YELLOW", "GREEN",
    "AQUA", "BLUE", "PURPLE", "PINK"
};

static const char *mode_names[] = {
    "LIBRARY", "PLAYLISTS", "TRACKS", "FILTER", "USB", "SETTINGS", "PENDING",
    "INDEX", "GENRES", "PLAYLIST MANAGER", "ACCENT", "WORKFLOW MANAGER",
    "WORKFLOW EDITOR", "TOOL PICKER", "DEFAULT VALUE", "PLAYBACK", "TEMPO",
    "BEATGRID", "HOT CUES", "LOOP", "METADATA", "LIST", "PLAYLIST NAV",
    "VISUALIZER", "WORKFLOWS", "VISUALIZER II", "PITCH"
};

static const char * const tool_names[TOOL_COUNT] = {
    "SEEK", "SCRUB", "ZOOM", "GAIN", "HOST RPM", "PLAY RPM",
    "PITCH BEND", "TEMPO", "AUTO-NEXT", "ADD TO PLAYLIST",
    "RGB WAVEFORM", "PITCH LOCK", "BOOMBOX", "20-BAND EQ",
    "OSCILLO-TURNTABLE", "M1", "M2", "GRID NUDGE",
    "GRID BPM", "DOWNBEAT", "QUANTIZE", "CUE SLOT", "CUE MOVE",
    "CUE COLOR", "CUE DELETE", "LOOP SIZE", "LOOP IN", "LOOP OUT",
    "LOOP ACTIVE", "RATING", "TRACK COLOR", "YEAR", "GENRE",
    "BURN TRACK", "BURN ALL", "PREV TRACK", "NEXT TRACK",
    "RESTART", "RELOAD", "SPECTRAL CANYON", "STEREO ORBIT",
    "PHRASE MAP", "HARMONIC CONSTELLATION", "KEY EDITOR",
    "KEY NOTATION", "FAVLIST1", "FAVLIST2", "IPOD SEEK"
};

/* On-disk workflow identities are deliberately independent of enum order.
   A new build may rearrange pages or append tools without reinterpreting a
   user's saved M1/M2 sequence. Never reuse one of these IDs. */
static const uint16_t macro_tool_storage_ids[TOOL_COUNT] = {
    [TOOL_SEEK]              = 0x0100,
    [TOOL_SCRUB_STEP]        = 0x0101,
    [TOOL_ZOOM]              = 0x0102,
    [TOOL_GAIN]              = 0x0103,
    [TOOL_HOST_RPM]          = 0x0104,
    [TOOL_PLAY_RPM]          = 0x0105,
    [TOOL_PITCH_BEND]        = 0x0106,
    [TOOL_TEMPO]             = 0x0107,
    [TOOL_PLAYLIST_MODE]     = 0x0108,
    [TOOL_ADD_PLAYLIST]      = 0x0109,
    [TOOL_WAVEFORM_STYLE]    = 0x010a,
    [TOOL_KEYLOCK]           = 0x010b,
    [TOOL_VIS_BOOMBOX]       = 0x010c,
    [TOOL_VIS_EQ]            = 0x010d,
    [TOOL_VIS_TURNTABLE]     = 0x010e,
    [TOOL_MACRO_ONE]         = 0x010f,
    [TOOL_MACRO_TWO]         = 0x0110,
    [TOOL_GRID_NUDGE]        = 0x0111,
    [TOOL_GRID_BPM]          = 0x0112,
    [TOOL_GRID_ORIGIN]       = 0x0113,
    [TOOL_GRID_QUANTIZE]     = 0x0114,
    [TOOL_CUE_SLOT]          = 0x0115,
    [TOOL_CUE_MOVE]          = 0x0116,
    [TOOL_CUE_COLOR]         = 0x0117,
    [TOOL_CUE_DELETE]        = 0x0118,
    [TOOL_LOOP_LENGTH]       = 0x0119,
    [TOOL_LOOP_IN]           = 0x011a,
    [TOOL_LOOP_OUT]          = 0x011b,
    [TOOL_LOOP_ACTIVE]       = 0x011c,
    [TOOL_META_RATING]       = 0x011d,
    [TOOL_META_COLOR]        = 0x011e,
    [TOOL_META_YEAR]         = 0x011f,
    [TOOL_META_GENRE]        = 0x0120,
    [TOOL_BURN_SONG]         = 0x0121,
    [TOOL_BURN_ALL]          = 0x0122,
    [TOOL_PLAYLIST_PREVIOUS] = 0x0123,
    [TOOL_PLAYLIST_NEXT]     = 0x0124,
    [TOOL_RESTART_PLAYBACK]  = 0x0125,
    [TOOL_RELOAD_TRACK]      = 0x0126,
    [TOOL_VIS_CANYON]        = 0x0127,
    [TOOL_VIS_ORBIT]         = 0x0128,
    [TOOL_VIS_PHRASE]        = 0x0129,
    [TOOL_VIS_HARMONIC]      = 0x012a,
    [TOOL_META_KEY]          = 0x012b,
    [TOOL_KEY_NOTATION]      = 0x012c,
    [TOOL_FAVORITE_ONE]      = 0x012d,
    [TOOL_FAVORITE_TWO]      = 0x012e,
    [TOOL_IPOD_SEEK]         = 0x012f,
};

static char *waveform_styles[] = { "full", "half" };
static char *visualizer_styles[] = {
    "rgb waveform", "boombox bass", "20-band EQ", "Oscillo-Turntable",
    "spectral canyon", "stereo orbit", "phrase map",
    "harmonic constellation"
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
static char *auto_burn_styles[] = { "off", "next track load" };
static char *off_on_styles[] = { "off", "on" };
static char *ipod_seek_position_styles[] = { "last", "first" };
static char *key_notation_styles[] = {
    "chromatic equivalent", "camelot equivalent", "original"
};
static const char * const chromatic_key_names[] = {
    "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
    "Cm", "Dbm", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "Abm", "Am",
    "Bbm", "Bm"
};
static const char * const camelot_key_names[] = {
    "8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B",
    "6B", "1B", "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A",
    "1A", "8A", "3A", "10A"
};
struct rbprep_key_alias {
    const char *name;
    unsigned char index;
};
static const struct rbprep_key_alias key_aliases[] = {
    { "B#", 0 }, { "C#", 1 }, { "D#", 3 }, { "E#", 5 },
    { "Gb", 6 }, { "G#", 8 }, { "A#", 10 }, { "Cb", 11 },
    { "Fb", 4 },
    { "B#m", 12 }, { "C#m", 13 }, { "D#m", 15 }, { "E#m", 17 },
    { "Gbm", 18 }, { "G#m", 20 }, { "A#m", 22 }, { "Cbm", 23 },
    { "Fbm", 16 }
};
static char *turntable_arm_styles[] = { "straight", "s-shaped" };
static char *turntable_headshell_styles[] = {
    "technics", "ortofon concorde", "shure m44", "ipod body", "phase"
};
static const char *track_sort_names[] = {
    "TITLE", "BPM", "YEAR", "KEY", "COMMENTS", "TAGS", "IMPORTED"
};
static const int scrub_steps[] = {
    1, 2, 5, 10, 20, 50, 100, 250, 500, 1000
};
static const int16_t wheel_cosine[64] = {
    256,255,251,245,237,226,213,198,181,162,142,121,98,74,50,25,
    0,-25,-50,-74,-98,-121,-142,-162,-181,-198,-213,-226,-237,
    -245,-251,-255,-256,-255,-251,-245,-237,-226,-213,-198,-181,
    -162,-142,-121,-98,-74,-50,-25,0,25,50,74,98,121,142,162,
    181,198,213,226,237,245,251,255
};
static const int16_t wheel_sine[64] = {
    0,25,50,74,98,121,142,162,181,198,213,226,237,245,251,255,
    256,255,251,245,237,226,213,198,181,162,142,121,98,74,50,25,
    0,-25,-50,-74,-98,-121,-142,-162,-181,-198,-213,-226,-237,
    -245,-251,-255,-256,-255,-251,-245,-237,-226,-213,-198,-181,
    -162,-142,-121,-98,-74,-50,-25
};

static const struct configdata rbprep_config[] = {
    { TYPE_ENUM, 0, 1, { .int_p = &waveform_half },
      "waveform style", waveform_styles },
    { TYPE_ENUM, 0, ARRAYLEN(visualizer_styles) - 1,
      { .int_p = &visualizer_mode },
      "visualizer", visualizer_styles },
    { TYPE_ENUM, 0, 1, { .int_p = &save_on_track_load },
      "save edits", save_on_load_styles },
    { TYPE_ENUM, 0, 1, { .int_p = &auto_burn },
      "auto burn", auto_burn_styles },
    { TYPE_INT, 0, ARRAYLEN(scrub_steps) - 1,
      { .int_p = &scrub_step_index }, "scrub step", NULL },
    { TYPE_ENUM, 0, 1, { .int_p = &click_sound },
      "wheel click", off_on_styles },
    { TYPE_ENUM, 0, 1, { .int_p = &platter_wheel_mode },
      "platter wheel mode", off_on_styles },
    { TYPE_ENUM, 0, 1, { .int_p = &ipod_seek_first },
      "ipod seek position", ipod_seek_position_styles },
    { TYPE_ENUM, 0, 1, { .int_p = &keylock_enabled },
      "keylock", off_on_styles },
    { TYPE_ENUM, 0, ARRAYLEN(key_notation_styles) - 1,
      { .int_p = &key_notation },
      "key notation", key_notation_styles },
    { TYPE_ENUM, 0, 1, { .int_p = &autoplay_enabled },
      "autoplay default", off_on_styles },
    { TYPE_ENUM, 0, 1, { .int_p = &autoboot_enabled },
      "autoboot rekordpod", off_on_styles },
    { TYPE_INT, 0, INT_MAX, { .int_p = &total_plays },
      "total plays", NULL },
    { TYPE_INT, 0, 359, { .int_p = &accent_hue },
      "accent hue", NULL },
    { TYPE_INT, 0, 100, { .int_p = &accent_saturation },
      "accent saturation", NULL },
    { TYPE_INT, 10, 100, { .int_p = &accent_brightness },
      "accent brightness", NULL },
    { TYPE_INT, 0, 359, { .int_p = &body_hue },
      "ipod body hue", NULL },
    { TYPE_INT, 0, 100, { .int_p = &body_saturation },
      "ipod body saturation", NULL },
    { TYPE_INT, 10, 100, { .int_p = &body_brightness },
      "ipod body brightness", NULL },
    { TYPE_INT, 0, 359, { .int_p = &wheel_hue },
      "wheel hue", NULL },
    { TYPE_INT, 0, 100, { .int_p = &wheel_saturation },
      "wheel saturation", NULL },
    { TYPE_INT, 5, 100, { .int_p = &wheel_brightness },
      "wheel brightness", NULL },
    { TYPE_ENUM, 0, 1, { .int_p = &turntable_arm_style },
      "turntable arm", turntable_arm_styles },
    { TYPE_ENUM, 0, ARRAYLEN(turntable_headshell_styles) - 1,
      { .int_p = &turntable_headshell_style },
      "turntable headshell", turntable_headshell_styles },
    { TYPE_INT, 0, INT_MAX, { .int_p = &favorite_playlist_ids[0] },
      "favorite playlist 1", NULL },
    { TYPE_INT, 0, INT_MAX, { .int_p = &favorite_playlist_ids[1] },
      "favorite playlist 2", NULL }
};

/* Never write configuration from the animation/playback hot path. On flash
   media a tiny config rewrite can still block for seconds while the device
   performs erase/program housekeeping. Keep the current values live in RAM
   and commit them only at a playback-safe boundary. */
static bool save_rbprep_config(void)
{
    int fd;

    if (!config_dirty)
        return true;
    if (configfile_save(RBPREP_CONFIG_FILE, rbprep_config,
                        ARRAYLEN(rbprep_config),
                        RBPREP_CONFIG_VERSION) < 0)
        return false;
    if (autoboot_enabled) {
        if (rb->file_exists(RBPREP_AUTOBOOT_OFF) &&
            rb->remove(RBPREP_AUTOBOOT_OFF) < 0)
            return false;
    } else if (!rb->file_exists(RBPREP_AUTOBOOT_OFF)) {
        fd = rb->creat(RBPREP_AUTOBOOT_OFF, 0666);
        if (fd < 0)
            return false;
        rb->close(fd);
    }
    config_dirty = false;
    return true;
}

static void mark_rbprep_config_dirty(void)
{
    config_dirty = true;
}

static void load_device_usb_name(void)
{
    char raw[sizeof(device_usb_name)];
    int fd;
    int count;
    int first = 0;
    int last;

    rb->strlcpy(device_usb_name, "REKORDPOD", sizeof(device_usb_name));
    fd = rb->open(RBPREP_DEVICE_NAME_FILE, O_RDONLY);
    if (fd < 0)
        return;
    count = rb->read(fd, raw, sizeof(raw) - 1);
    rb->close(fd);
    if (count <= 0)
        return;
    raw[count] = '\0';
    while (first < count && (raw[first] == ' ' || raw[first] == '\t' ||
                             raw[first] == '\r' || raw[first] == '\n'))
        first++;
    last = count;
    while (last > first && (raw[last - 1] == ' ' || raw[last - 1] == '\t' ||
                            raw[last - 1] == '\r' || raw[last - 1] == '\n'))
        last--;
    raw[last] = '\0';
    if (raw[first])
        rb->strlcpy(device_usb_name, raw + first, sizeof(device_usb_name));
}

static void service_config_persistence(void)
{
    int status;

    if (!config_dirty || seek_state != SEEK_IDLE)
        return;
    status = rb->audio_status();
    if ((status & AUDIO_STATUS_PLAY) && !(status & AUDIO_STATUS_PAUSE))
        return;
    save_rbprep_config();
}

static void service_storage_keepalive(void)
{
    long now = *rb->current_tick;
    int status;

    if (display_locked || TIME_BEFORE(now, storage_keepalive_deadline))
        return;
    status = rb->audio_status();
    if ((status & AUDIO_STATUS_PLAY) && !(status & AUDIO_STATUS_PAUSE)) {
        /* Decoder refills already keep media awake. An extra ATA keepalive
           here could collide with a refill and produced a visible hitch near
           the one-minute mark on large flash adapters. */
        storage_keepalive_deadline = now + HZ * 30;
        return;
    }
    /* Some large PATA-to-flash adapters are slow to recover from standby.
       Keep the already-awake device active while Rekordpod is in use, but
       immediately restore normal Rockbox power policy when HOLD is engaged,
       USB takes ownership, or the plugin exits. */
    rb->storage_spin();
    storage_keepalive_deadline = now + HZ * 30;
}

struct rbprep_tool_page {
    enum rbprep_mode mode;
    const char *label;
    unsigned char count;
    enum rbprep_tool tools[RBPREP_TOOL_PAGE_MAX];
};

static struct rbprep_tool_page tool_pages[] = {
    { MODE_DECK, "PLAYER", 5,
      { TOOL_SEEK, TOOL_SCRUB_STEP, TOOL_ZOOM, TOOL_GAIN,
        TOOL_IPOD_SEEK } },
    { MODE_PNAV, "PLNAV", 3,
      { TOOL_PLAYLIST_PREVIOUS, TOOL_PLAYLIST_NEXT,
        TOOL_RESTART_PLAYBACK } },
    { MODE_GRID, "BTGRID", 3,
      { TOOL_GRID_NUDGE, TOOL_GRID_ORIGIN, TOOL_GRID_BPM } },
    { MODE_LIST, "LISTS", 4,
      { TOOL_PLAYLIST_MODE, TOOL_ADD_PLAYLIST,
        TOOL_FAVORITE_ONE, TOOL_FAVORITE_TWO } },
    { MODE_CUES, "HOTCUE", 4,
      { TOOL_CUE_SLOT, TOOL_CUE_DELETE, TOOL_CUE_COLOR, TOOL_CUE_MOVE } },
    { MODE_METADATA, "DETAIL", 5,
      { TOOL_META_RATING, TOOL_META_COLOR, TOOL_META_YEAR, TOOL_META_GENRE,
        TOOL_META_KEY } },
    { MODE_VISUALIZER, "VIZ", 4,
      { TOOL_WAVEFORM_STYLE, TOOL_VIS_EQ, TOOL_VIS_PHRASE,
        TOOL_VIS_HARMONIC } },
    { MODE_VISUALIZER_TWO, "MORVIZ", 4,
      { TOOL_VIS_CANYON, TOOL_VIS_BOOMBOX, TOOL_VIS_ORBIT,
        TOOL_VIS_TURNTABLE } },
    { MODE_TEMPO, "TEMPO", 4,
      { TOOL_PITCH_BEND, TOOL_TEMPO, TOOL_HOST_RPM, TOOL_PLAY_RPM } },
    { MODE_LOOP, "LOOPS", 4,
      { TOOL_LOOP_LENGTH, TOOL_LOOP_IN, TOOL_LOOP_OUT, TOOL_LOOP_ACTIVE } },
    { MODE_MACRO, "LOCK", 4,
      { TOOL_KEYLOCK, TOOL_GRID_QUANTIZE, TOOL_MACRO_ONE, TOOL_MACRO_TWO } }
};

static void update_player_tool_order(void)
{
    struct rbprep_tool_page *page = &tool_pages[0];

    page->count = 5;
    if (ipod_seek_first) {
        page->tools[0] = TOOL_IPOD_SEEK;
        page->tools[1] = TOOL_SEEK;
        page->tools[2] = TOOL_SCRUB_STEP;
        page->tools[3] = TOOL_ZOOM;
        page->tools[4] = TOOL_GAIN;
    } else {
        page->tools[0] = TOOL_SEEK;
        page->tools[1] = TOOL_SCRUB_STEP;
        page->tools[2] = TOOL_ZOOM;
        page->tools[3] = TOOL_GAIN;
        page->tools[4] = TOOL_IPOD_SEEK;
    }
}

static int player_tool_index(enum rbprep_tool tool)
{
    int index;

    for (index = 0; index < tool_pages[0].count; index++)
        if (tool_pages[0].tools[index] == tool)
            return index;
    return 0;
}

static int hsb_rgb(int hue, int saturation, int brightness)
{
    int sector = (hue % 360) / 60;
    int remainder = (hue % 60) * 255 / 60;
    int value = brightness * 255 / 100;
    int minimum = value * (100 - saturation) / 100;
    int rising = minimum + (value - minimum) * remainder / 255;
    int falling = value - (value - minimum) * remainder / 255;
    int red = minimum;
    int green = minimum;
    int blue = minimum;

    if (sector == 0) { red = value; green = rising; }
    else if (sector == 1) { red = falling; green = value; }
    else if (sector == 2) { green = value; blue = rising; }
    else if (sector == 3) { green = falling; blue = value; }
    else if (sector == 4) { red = rising; blue = value; }
    else { red = value; blue = falling; }
    return LCD_RGBPACK(red, green, blue);
}

static void update_theme_colors(void)
{
    theme_accent = hsb_rgb(accent_hue, accent_saturation,
                           accent_brightness);
    theme_accent_dim = hsb_rgb(accent_hue,
                               MIN(100, accent_saturation + 8),
                               MAX(10, accent_brightness / 3));
    theme_body = hsb_rgb(body_hue, body_saturation, body_brightness);
    theme_body_shadow = hsb_rgb(body_hue,
                                MIN(100, body_saturation + 4),
                                MAX(10, body_brightness / 3));
    theme_wheel = hsb_rgb(wheel_hue, wheel_saturation, wheel_brightness);
    theme_wheel_outline = hsb_rgb(wheel_hue,
                                  MAX(0, wheel_saturation - 8),
                                  MIN(100, wheel_brightness + 38));
}

static int key_index_from_name(const char *name)
{
    int index;
    int pitch;
    int position = 1;
    bool minor = false;
    char letter;

    if (!name || !name[0])
        return -1;
    while (*name == ' ')
        name++;
    for (index = 0; index < (int)ARRAYLEN(chromatic_key_names); index++) {
        if (!rb->strcasecmp(name, chromatic_key_names[index]) ||
            !rb->strcasecmp(name, camelot_key_names[index]))
            return index;
    }
    for (index = 0; index < (int)ARRAYLEN(key_aliases); index++)
        if (!rb->strcasecmp(name, key_aliases[index].name))
            return key_aliases[index].index;

    /* Accept enharmonic spellings not used by the canonical rekordbox list. */
    letter = name[0];
    if (letter >= 'a' && letter <= 'g')
        letter -= 'a' - 'A';
    if (letter == 'C') pitch = 0;
    else if (letter == 'D') pitch = 2;
    else if (letter == 'E') pitch = 4;
    else if (letter == 'F') pitch = 5;
    else if (letter == 'G') pitch = 7;
    else if (letter == 'A') pitch = 9;
    else if (letter == 'B') pitch = 11;
    else return -1;
    if (name[position] == '#') {
        pitch++;
        position++;
    } else if (name[position] == 'b') {
        pitch--;
        position++;
    }
    while (name[position] == ' ')
        position++;
    minor = name[position] == 'm' ||
            (name[position] == 'M' &&
             (name[position + 1] == 'i' || name[position + 1] == 'I'));
    return (minor ? 12 : 0) + (pitch + 12) % 12;
}

static void format_key_name(const char *source, char *buffer, size_t size)
{
    int index;

    if (key_notation == KEY_DISPLAY_ORIGINAL) {
        rb->strlcpy(buffer, source && source[0] ? source : "--", size);
        return;
    }
    index = key_index_from_name(source);

    if (index < 0) {
        rb->strlcpy(buffer, source && source[0] ? source : "--", size);
        return;
    }
    rb->strlcpy(buffer, key_notation == KEY_DISPLAY_CAMELOT
                        ? camelot_key_names[index]
                        : chromatic_key_names[index], size);
}

static void set_selected_key_index(int index)
{
    index = (index + ARRAYLEN(chromatic_key_names)) %
            ARRAYLEN(chromatic_key_names);
    rb->strlcpy(selected_key, chromatic_key_names[index],
                sizeof(selected_key));
}

static void active_color_channels(int **hue, int **saturation,
                                  int **brightness)
{
    if (color_picker_target == COLOR_TARGET_BODY) {
        *hue = &body_hue;
        *saturation = &body_saturation;
        *brightness = &body_brightness;
    } else if (color_picker_target == COLOR_TARGET_WHEEL) {
        *hue = &wheel_hue;
        *saturation = &wheel_saturation;
        *brightness = &wheel_brightness;
    } else {
        *hue = &accent_hue;
        *saturation = &accent_saturation;
        *brightness = &accent_brightness;
    }
}

static const char *active_color_title(void)
{
    if (color_picker_target == COLOR_TARGET_BODY)
        return "IPOD BODY COLOR";
    if (color_picker_target == COLOR_TARGET_WHEEL)
        return "WHEEL / VINYL";
    return "ACCENT COLOR";
}

static void adjust_active_color(int direction)
{
    int *hue;
    int *saturation;
    int *brightness;
    int minimum_brightness = color_picker_target == COLOR_TARGET_WHEEL
                           ? 5 : 10;

    active_color_channels(&hue, &saturation, &brightness);
    if (accent_component == 0)
        *hue = (*hue + (direction > 0 ? 1 : 359)) % 360;
    else if (accent_component == 1)
        *saturation = MAX(0, MIN(100, *saturation + direction));
    else
        *brightness = MAX(minimum_brightness,
                          MIN(100, *brightness + direction));
    update_theme_colors();
    mark_rbprep_config_dirty();
    force_full_redraw = true;
}

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

    rb->dsp_timestretch_enable(!!keylock_enabled);
    if (keylock_enabled && rb->dsp_timestretch_available()) {
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
    /* The callback runs in Rockbox's mixer path.  The RGB waveform is already
       driven by imported analysis and must not spend audio-thread time copying
       PCM that it never consumes. */
    if (spectrum_capture_active || visualizer_mode == 0 || display_locked ||
        mode < MODE_DECK)
        return;
    pcm_capture_index = 0;
    pcm_capture_frames[0] = pcm_capture_frames[1] = 0;
    pcm_capture_generation = spectrum_generation = 0;
    spectrum_deadline = *rb->current_tick;
    pcm_sample_rate = rb->mixer_get_frequency();
    rb->memset(spectrum_levels, 0, sizeof(spectrum_levels));
    rb->memset(chroma_levels, 0, sizeof(chroma_levels));
    rb->memset(spectrum_history, 0, sizeof(spectrum_history));
    spectrum_history_head = -1;
    spectrum_history_generation = 0;
    spectrum_bass = spectrum_bass_hit = 0;
    bass_frequency_x10 = 0;
    bass_note_index = -1;
    bass_pitch_confidence = 0;
    bass_filter_state = 0;
    rb->mixer_channel_set_buffer_hook(PCM_MIXER_CHAN_PLAYBACK,
                                      &spectrum_buffer_cbs);
    spectrum_capture_active = true;
}

static void stop_spectrum_capture(void)
{
    if (!spectrum_capture_active)
        return;
    rb->mixer_channel_set_buffer_hook(PCM_MIXER_CHAN_PLAYBACK, NULL);
    spectrum_capture_active = false;
}

static void update_spectrum_capture_state(void)
{
    if (!display_locked && mode >= MODE_DECK && visualizer_mode != 0)
        start_spectrum_capture();
    else
        stop_spectrum_capture();
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

static int normalize_track_color(int color)
{
    return color >= 0 && color < RBPREP_TRACK_COLOR_COUNT
         ? color : RBPREP_TRACK_COLOR_NONE;
}

static int track_color_display(int color)
{
    color = normalize_track_color(color);
    return color == RBPREP_TRACK_COLOR_NONE ? LCD_LIGHTGRAY
                                            : cue_palette[color];
}

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

    /* A one-pixel dark keyline keeps the five-pixel colored flag and its
       number readable over white, yellow, or high-energy RGB waveform bins. */
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(center_x - 3, top - 1, 7, 7);
    rb->lcd_hline(center_x - 2, center_x + 2, top + 6);
    rb->lcd_hline(center_x - 1, center_x + 1, top + 7);
    rb->lcd_drawpixel(center_x, top + 8);
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

static void activity_ticker_ping(int progress)
{
    long now = *rb->current_tick;

    activity_ticker_progress = MAX(0, MIN(1000, progress));
    if (activity_ticker_last_update &&
        !TIME_BEFORE(activity_ticker_last_update + MAX(1, HZ / 5), now))
        activity_ticker_burst = MIN(12, activity_ticker_burst + 3);
    activity_ticker_last_update = now;
    activity_ticker_until = now + HZ;
}

static void draw_activity_ticker(void)
{
    int x;
    int color;

    if (TIME_BEFORE(activity_ticker_until, *rb->current_tick))
        return;
    x = activity_ticker_progress * (LCD_WIDTH - 13) / 1000;
    color = activity_ticker_burst && ((*rb->current_tick & 1) != 0)
          ? LCD_WHITE : theme_accent;
    rb->lcd_set_drawmode(DRMODE_SOLID);
    rb->lcd_set_foreground(color);
    rb->lcd_fillrect(MAX(0, x - 10), 5, 12, 2);
    rb->lcd_fillrect(x, 1, 3, 6);
    if (activity_ticker_burst)
        activity_ticker_burst--;
}

static void draw_status_bar(void)
{
    struct tm *now = rb->get_time();
    char clock[8];
    char timecode[28];
    int level = rb->battery_level();
    int width;
    int text_width;
    bool low;

    level = MAX(0, MIN(100, level));
    low = level <= 10;
    width = LCD_WIDTH * level / 100;
    rb->lcd_set_drawmode(DRMODE_SOLID);
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(0, 0, LCD_WIDTH, RBPREP_STATUS_HEIGHT);
    rb->lcd_set_foreground(low ? LCD_RGBPACK(225, 35, 35) : LCD_WHITE);
    if (width > 0)
        rb->lcd_fillrect(0, 0, width, RBPREP_STATUS_HEIGHT - 1);
    if (now) {
        int hour = now->tm_hour;
        if (rb->global_settings->timeformat) {
            hour %= 12;
            if (!hour)
                hour = 12;
        }
        rb->snprintf(clock, sizeof(clock), "%02d:%02d", hour, now->tm_min);
    } else {
        rb->strlcpy(clock, "--:--", sizeof(clock));
    }
    rb->lcd_getstringsize(clock, &text_width, NULL);
    rb->lcd_set_foreground(LCD_WHITE);
    if (!low)
        rb->lcd_set_drawmode(DRMODE_COMPLEMENT);
    rb->lcd_putsxy((LCD_WIDTH - text_width) / 2, 0, clock);
    if (mode >= MODE_DECK && selected_track_id >= 0) {
        rb->snprintf(timecode, sizeof(timecode), "%ds : %ds",
                     MAX(0, MIN(track_length, playhead)) / 1000,
                     MAX(0, track_length) / 1000);
        rb->lcd_getstringsize(timecode, &text_width, NULL);
        rb->lcd_putsxy(MAX(1, LCD_WIDTH - text_width - 2), 0, timecode);
    }
    rb->lcd_set_drawmode(DRMODE_SOLID);
    draw_activity_ticker();
    rb->lcd_set_foreground(low ? LCD_RGBPACK(125, 20, 20)
                               : LCD_RGBPACK(72, 72, 72));
    rb->lcd_hline(0, LCD_WIDTH - 1, RBPREP_STATUS_HEIGHT - 1);
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
#if LCD_DEPTH > 1
    /* Prep Deck owns every pixel; never let the selected Rockbox backdrop
       leak through after a splash, keyboard, error, or USB handoff. */
    rb->lcd_set_backdrop(NULL);
#endif
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

static bool read_exact(int fd, void *buffer, size_t size)
{
    unsigned char *cursor = buffer;

    while (size > 0) {
        ssize_t count = rb->read(fd, cursor, size);
        if (count <= 0)
            return false;
        cursor += count;
        size -= count;
    }
    return true;
}

static bool write_exact(int fd, const void *buffer, size_t size)
{
    const unsigned char *cursor = buffer;

    while (size > 0) {
        ssize_t count = rb->write(fd, cursor, size);
        if (count <= 0)
            return false;
        cursor += count;
        size -= count;
    }
    return true;
}

static uint32_t macro_checksum(const unsigned char *data, size_t size)
{
    uint32_t checksum = 2166136261u;
    size_t i;

    for (i = 0; i < size; i++) {
        checksum ^= data[i];
        checksum *= 16777619u;
    }
    return checksum;
}

static uint16_t macro_tool_storage_id(unsigned char tool)
{
    if (tool >= TOOL_COUNT)
        return 0;
    return macro_tool_storage_ids[tool];
}

static bool macro_tool_from_storage_id(uint16_t stored,
                                       unsigned char *tool)
{
    int candidate;

    for (candidate = 0; candidate < TOOL_COUNT; candidate++) {
        if (macro_tool_storage_ids[candidate] == stored) {
            *tool = candidate;
            return true;
        }
    }
    return false;
}

static void reset_tool_macros(void)
{
    int slot;
    int step;

    rb->memset(tool_macros, 0, sizeof(tool_macros));
    rb->strlcpy(tool_macros[0].name, "WORKFLOW A",
                sizeof(tool_macros[0].name));
    rb->strlcpy(tool_macros[1].name, "WORKFLOW B",
                sizeof(tool_macros[1].name));
    for (slot = 0; slot < RBPREP_MACRO_COUNT; slot++)
        for (step = 0; step < RBPREP_MACRO_STEPS; step++)
            tool_macros[slot].steps[step].value = RBPREP_MACRO_CHOOSE;
    macro_active = -1;
    macro_position = 0;
    macro_generation = 0;
    macro_generation_slot = -1;
    macro_store_valid = true;
    macro_dirty = false;
}

static bool macro_v5_validate(const unsigned char *data, int size,
                              uint32_t *generation)
{
    const unsigned char *cursor;
    int slot;
    int step;

    if (size != RBPREP_MACRO_V5_SIZE || rb->memcmp(data, "RBM5", 4) ||
        read_u16(data + 4) != 5 ||
        read_u16(data + 6) != RBPREP_MACRO_COUNT ||
        read_u16(data + 8) != RBPREP_MACRO_STEPS ||
        read_u16(data + 10) != RBPREP_MACRO_STEP_SIZE ||
        read_u32(data + 16) != RBPREP_MACRO_V5_PAYLOAD ||
        read_u32(data + 20) != macro_checksum(
            data + RBPREP_MACRO_V5_HEADER, RBPREP_MACRO_V5_PAYLOAD))
        return false;

    cursor = data + RBPREP_MACRO_V5_HEADER;
    if ((cursor[0] != 0xff && cursor[0] >= RBPREP_MACRO_COUNT) ||
        cursor[1] >= RBPREP_MACRO_STEPS)
        return false;
    cursor += 4;
    for (slot = 0; slot < RBPREP_MACRO_COUNT; slot++) {
        int count;

        cursor += RBPREP_MACRO_NAME;
        count = *cursor++;
        if (count > RBPREP_MACRO_STEPS)
            return false;
        for (step = 0; step < RBPREP_MACRO_STEPS; step++) {
            unsigned char tool;

            if (step < count &&
                !macro_tool_from_storage_id(read_u16(cursor), &tool))
                return false;
            cursor += RBPREP_MACRO_STEP_SIZE;
        }
    }
    if (cursor != data + size)
        return false;
    *generation = read_u32(data + 12);
    return true;
}

static bool macro_v5_read(const char *path, unsigned char *data,
                          uint32_t *generation)
{
    int size = 0;

    return rbprep_store_read(rb, path, data, RBPREP_MACRO_V5_SIZE, &size) &&
           macro_v5_validate(data, size, generation);
}

static bool macro_generation_is_newer(uint32_t candidate,
                                      uint32_t reference)
{
    return (int32_t)(candidate - reference) > 0;
}

static void macro_v5_pack(unsigned char *data, uint32_t generation)
{
    unsigned char *cursor = data + RBPREP_MACRO_V5_HEADER;
    int slot;
    int step;

    rb->memset(data, 0, RBPREP_MACRO_V5_SIZE);
    rb->memcpy(data, "RBM5", 4);
    write_u16(data + 4, 5);
    write_u16(data + 6, RBPREP_MACRO_COUNT);
    write_u16(data + 8, RBPREP_MACRO_STEPS);
    write_u16(data + 10, RBPREP_MACRO_STEP_SIZE);
    write_u32(data + 12, generation);
    write_u32(data + 16, RBPREP_MACRO_V5_PAYLOAD);

    cursor[0] = macro_active >= 0 && macro_active < RBPREP_MACRO_COUNT
                    ? macro_active : 0xff;
    cursor[1] = MAX(0, MIN(RBPREP_MACRO_STEPS - 1, macro_position));
    cursor += 4;
    for (slot = 0; slot < RBPREP_MACRO_COUNT; slot++) {
        rb->memcpy(cursor, tool_macros[slot].name, RBPREP_MACRO_NAME);
        cursor += RBPREP_MACRO_NAME;
        *cursor++ = MIN(tool_macros[slot].count, RBPREP_MACRO_STEPS);
        for (step = 0; step < RBPREP_MACRO_STEPS; step++) {
            write_u16(cursor, macro_tool_storage_id(
                                  tool_macros[slot].steps[step].tool));
            cursor[2] = tool_macros[slot].steps[step].flags;
            cursor[3] = 0;
            write_u32(cursor + 4, tool_macros[slot].steps[step].value);
            cursor += RBPREP_MACRO_STEP_SIZE;
        }
    }
    write_u32(data + 20, macro_checksum(
        data + RBPREP_MACRO_V5_HEADER, RBPREP_MACRO_V5_PAYLOAD));
}

static bool decode_tool_macros_v5(const unsigned char *data)
{
    const unsigned char *cursor = data + RBPREP_MACRO_V5_HEADER;
    int saved_active = cursor[0] == 0xff ? -1 : cursor[0];
    int saved_position = cursor[1];
    int slot;
    int step;

    reset_tool_macros();
    cursor += 4;
    for (slot = 0; slot < RBPREP_MACRO_COUNT; slot++) {
        int count;

        rb->memcpy(tool_macros[slot].name, cursor, RBPREP_MACRO_NAME);
        tool_macros[slot].name[RBPREP_MACRO_NAME - 1] = '\0';
        cursor += RBPREP_MACRO_NAME;
        count = *cursor++;
        tool_macros[slot].count = count;
        for (step = 0; step < RBPREP_MACRO_STEPS; step++) {
            unsigned char tool;

            if (macro_tool_from_storage_id(read_u16(cursor), &tool))
                tool_macros[slot].steps[step].tool = tool;
            else if (step < count)
                return false;
            else
                tool_macros[slot].steps[step].tool = TOOL_SEEK;
            tool_macros[slot].steps[step].flags = cursor[2] &
                                                 RBPREP_MACRO_LOCK_WHEEL;
            tool_macros[slot].steps[step].value =
                (int)read_u32(cursor + 4);
            cursor += RBPREP_MACRO_STEP_SIZE;
        }
    }
    macro_active = saved_active;
    if (macro_active >= 0 && tool_macros[macro_active].count > 0)
        macro_position = MIN(saved_position,
                             tool_macros[macro_active].count - 1);
    else {
        macro_active = -1;
        macro_position = 0;
    }
    macro_store_valid = true;
    macro_dirty = false;
    return true;
}

static bool save_tool_macros(void)
{
    unsigned char data[RBPREP_MACRO_V5_SIZE];
    unsigned char verify[RBPREP_MACRO_V5_SIZE];
    int target_slot = macro_generation_slot == 0 ? 1 : 0;
    const char *target = target_slot == 0 ? RBPREP_MACRO_FILE_A
                                          : RBPREP_MACRO_FILE_B;
    uint32_t next_generation = macro_generation + 1;
    uint32_t verify_generation;
    bool ok;

    if (next_generation == 0)
        next_generation = 1;
    macro_dirty = true;
    macro_v5_pack(data, next_generation);
    rbprep_store_ensure_state_dir(rb);
    ok = rbprep_store_write_verified(rb, target, data, sizeof(data),
                                     verify, sizeof(verify)) &&
         macro_v5_validate(verify, sizeof(verify), &verify_generation) &&
         verify_generation == next_generation;
    if (ok) {
        macro_generation = next_generation;
        macro_generation_slot = target_slot;
        macro_store_valid = true;
        macro_dirty = false;
    } else {
        macro_store_valid = false;
    }
    return ok;
}

static bool decode_tool_macros_legacy(const unsigned char *data, int size)
{
    const unsigned char *cursor;
    int legacy_size = 12 + RBPREP_MACRO_COUNT *
                      (RBPREP_MACRO_NAME + 1 +
                       RBPREP_MACRO_LEGACY_STEPS);
    int legacy_value_size = 12 + RBPREP_MACRO_COUNT *
                            (RBPREP_MACRO_NAME + 1 +
                             RBPREP_MACRO_LEGACY_STEPS * 6);
    int v4_size = 12 + RBPREP_MACRO_COUNT *
                  (RBPREP_MACRO_NAME + 1 +
                   RBPREP_MACRO_LEGACY_STEPS * RBPREP_MACRO_STEP_SIZE);
    bool legacy_v1;
    bool legacy_v2;
    bool legacy_v3;
    bool legacy_v4;
    int slot;
    int step;

    reset_tool_macros();
    legacy_v1 = size == legacy_size && !rb->memcmp(data, "RBM1", 4) &&
                read_u16(data + 4) == 1;
    legacy_v2 = size == legacy_value_size &&
                !rb->memcmp(data, "RBM2", 4) &&
                read_u16(data + 4) == 2;
    legacy_v3 = size == legacy_value_size &&
                !rb->memcmp(data, "RBM3", 4) &&
                read_u16(data + 4) == 3;
    legacy_v4 = size == v4_size && !rb->memcmp(data, "RBM4", 4) &&
                read_u16(data + 4) == 4;
    if ((!legacy_v1 && !legacy_v2 && !legacy_v3 && !legacy_v4) ||
        read_u16(data + 6) != RBPREP_MACRO_COUNT ||
        read_u32(data + 8) != macro_checksum(data + 12,
                                             size - 12)) {
        goto invalid;
    }
    cursor = data + 12;
    for (slot = 0; slot < RBPREP_MACRO_COUNT; slot++) {
        rb->memcpy(tool_macros[slot].name, cursor, RBPREP_MACRO_NAME);
        tool_macros[slot].name[RBPREP_MACRO_NAME - 1] = '\0';
        cursor += RBPREP_MACRO_NAME;
        tool_macros[slot].count = MIN(*cursor++,
                                      RBPREP_MACRO_LEGACY_STEPS);
        for (step = 0; step < RBPREP_MACRO_LEGACY_STEPS; step++) {
            if (legacy_v4) {
                unsigned char tool;

                if (macro_tool_from_storage_id(read_u16(cursor), &tool))
                    tool_macros[slot].steps[step].tool = tool;
                else if (step < tool_macros[slot].count)
                    goto invalid;
                else
                    tool_macros[slot].steps[step].tool = TOOL_SEEK;
                tool_macros[slot].steps[step].flags = cursor[2] &
                                                     RBPREP_MACRO_LOCK_WHEEL;
                tool_macros[slot].steps[step].value =
                    (int)read_u32(cursor + 4);
                cursor += RBPREP_MACRO_STEP_SIZE;
            } else {
                tool_macros[slot].steps[step].tool = *cursor++;
            }
            if (legacy_v1) {
                tool_macros[slot].steps[step].flags = 0;
                tool_macros[slot].steps[step].value = RBPREP_MACRO_CHOOSE;
            } else if (!legacy_v4) {
                tool_macros[slot].steps[step].flags = *cursor++ &
                                                     RBPREP_MACRO_LOCK_WHEEL;
                tool_macros[slot].steps[step].value = (int)read_u32(cursor);
                cursor += 4;
            }
        }
        for (step = 0; step < RBPREP_MACRO_LEGACY_STEPS; step++) {
            if (tool_macros[slot].steps[step].tool >= TOOL_COUNT &&
                step < tool_macros[slot].count) {
                goto invalid;
            }
            if (step >= tool_macros[slot].count &&
                tool_macros[slot].steps[step].tool >= TOOL_COUNT) {
                tool_macros[slot].steps[step].tool = TOOL_SEEK;
                tool_macros[slot].steps[step].flags = 0;
                tool_macros[slot].steps[step].value = RBPREP_MACRO_CHOOSE;
            }
        }
    }
    macro_store_valid = true;
    return true;

invalid:
    reset_tool_macros();
    macro_store_valid = false;
    return false;
}

static bool load_tool_macros(void)
{
    unsigned char a[RBPREP_MACRO_V5_SIZE];
    unsigned char b[RBPREP_MACRO_V5_SIZE];
    unsigned char legacy[RBPREP_MACRO_V5_SIZE];
    const char *legacy_stores[] = {
        RBPREP_MACRO_LEGACY_TMP,
        RBPREP_MACRO_LEGACY_FILE,
        RBPREP_MACRO_LEGACY_PREV,
        RBPREP_MACRO_LEGACY_ROOT_TMP,
        RBPREP_MACRO_LEGACY_ROOT,
        RBPREP_MACRO_LEGACY_ROOT_PREV
    };
    uint32_t generation_a = 0;
    uint32_t generation_b = 0;
    bool valid_a;
    bool valid_b;
    bool saw_store = false;
    int source;

    reset_tool_macros();
    valid_a = macro_v5_read(RBPREP_MACRO_FILE_A, a, &generation_a);
    valid_b = macro_v5_read(RBPREP_MACRO_FILE_B, b, &generation_b);
    saw_store = rb->file_exists(RBPREP_MACRO_FILE_A) ||
                rb->file_exists(RBPREP_MACRO_FILE_B);
    if (valid_a || valid_b) {
        bool choose_b = valid_b && (!valid_a ||
                         macro_generation_is_newer(generation_b,
                                                   generation_a));
        const unsigned char *chosen = choose_b ? b : a;

        if (!decode_tool_macros_v5(chosen))
            goto invalid;
        macro_generation = choose_b ? generation_b : generation_a;
        macro_generation_slot = choose_b ? 1 : 0;
        macro_store_valid = true;
        macro_dirty = false;
        return true;
    }

    for (source = 0; source < (int)ARRAYLEN(legacy_stores); source++) {
        int size = 0;

        if (!rb->file_exists(legacy_stores[source]))
            continue;
        saw_store = true;
        if (!rbprep_store_read(rb, legacy_stores[source], legacy,
                               sizeof(legacy), &size) ||
            !decode_tool_macros_legacy(legacy, size))
            continue;

        /* Commit the migrated state into one verified RBM5 slot. The old
           files remain untouched until two new generations exist. */
        macro_generation = 0;
        macro_generation_slot = -1;
        macro_dirty = true;
        if (!save_tool_macros())
            macro_store_valid = false;
        return macro_store_valid;
    }

invalid:
    reset_tool_macros();
    macro_store_valid = !saw_store;
    return !saw_store;
}

static void restore_saved_macro_selection(void)
{
    int slot;

    if (macro_active >= 0 && macro_active < RBPREP_MACRO_COUNT &&
        tool_macros[macro_active].count > 0)
        return;
    macro_active = -1;
    macro_position = 0;
    for (slot = 0; slot < RBPREP_MACRO_COUNT; slot++) {
        if (tool_macros[slot].count > 0) {
            macro_active = slot;
            break;
        }
    }
}

static int macro_link_for(uint32_t playlist_id)
{
    int i;
    for (i = 0; i < macro_link_count; i++)
        if (macro_links[i].playlist_id == playlist_id)
            return macro_links[i].slot;
    return -1;
}

static bool save_macro_links(void)
{
    unsigned char data[12 + RBPREP_MACRO_LINKS * 5];
    int size = 12 + macro_link_count * 5;
    int i;
    int fd;

    rb->memset(data, 0, sizeof(data));
    rb->memcpy(data, "RBML", 4);
    write_u16(data + 4, 1);
    write_u16(data + 6, macro_link_count);
    for (i = 0; i < macro_link_count; i++) {
        write_u32(data + 12 + i * 5, macro_links[i].playlist_id);
        data[16 + i * 5] = macro_links[i].slot;
    }
    write_u32(data + 8, macro_checksum(data + 12, size - 12));
    fd = rb->open(RBPREP_MACRO_LINK_TMP,
                  O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    if (rb->write(fd, data, size) != size) {
        rb->close(fd);
        rb->remove(RBPREP_MACRO_LINK_TMP);
        return false;
    }
    rb->close(fd);
    rb->remove(RBPREP_MACRO_LINK_FILE);
    if (rb->rename(RBPREP_MACRO_LINK_TMP, RBPREP_MACRO_LINK_FILE) < 0) {
        rb->remove(RBPREP_MACRO_LINK_TMP);
        return false;
    }
    return true;
}

static void load_macro_links(void)
{
    unsigned char data[12 + RBPREP_MACRO_LINKS * 5];
    int fd;
    int size;
    int count;
    int i;
    bool migrate = false;

    macro_link_count = 0;
    fd = rb->open(RBPREP_MACRO_LINK_FILE, O_RDONLY);
    if (fd < 0) {
        fd = rb->open(RBPREP_MACRO_LINK_LEGACY, O_RDONLY);
        migrate = fd >= 0;
    }
    if (fd < 0)
        return;
    size = rb->filesize(fd);
    if (size < 12 || size > (int)sizeof(data) ||
        rb->read(fd, data, size) != size) {
        rb->close(fd);
        return;
    }
    rb->close(fd);
    count = read_u16(data + 6);
    if (rb->memcmp(data, "RBML", 4) || read_u16(data + 4) != 1 ||
        count > RBPREP_MACRO_LINKS || size != 12 + count * 5 ||
        read_u32(data + 8) != macro_checksum(data + 12, size - 12))
        return;
    for (i = 0; i < count; i++) {
        int slot = data[16 + i * 5];
        uint32_t playlist_id = read_u32(data + 12 + i * 5);
        if (!playlist_id || slot >= RBPREP_MACRO_COUNT)
            continue;
        macro_links[macro_link_count].playlist_id = playlist_id;
        macro_links[macro_link_count].slot = slot;
        macro_link_count++;
    }
    if (migrate)
        save_macro_links();
}

static bool cycle_macro_link(uint32_t playlist_id)
{
    int i;

    for (i = 0; i < macro_link_count; i++) {
        if (macro_links[i].playlist_id != playlist_id)
            continue;
        if (macro_links[i].slot + 1 < RBPREP_MACRO_COUNT) {
            macro_links[i].slot++;
        } else {
            for (; i + 1 < macro_link_count; i++)
                macro_links[i] = macro_links[i + 1];
            macro_link_count--;
        }
        return save_macro_links();
    }
    if (macro_link_count >= RBPREP_MACRO_LINKS)
        return false;
    macro_links[macro_link_count].playlist_id = playlist_id;
    macro_links[macro_link_count].slot = 0;
    macro_link_count++;
    return save_macro_links();
}

static bool playlist_has_smart_query(uint32_t playlist_id)
{
    int i;
    for (i = 0; i < smart_query_count; i++)
        if (smart_query_ids[i] == playlist_id)
            return true;
    return false;
}

static void load_smart_query_flags(void)
{
    char line[192];
    int fd;

    smart_query_count = 0;
    fd = rb->open(RBPREP_SMART_QUERY_FILE, O_RDONLY);
    if (fd < 0)
        return;
    if (rb->read_line(fd, line, sizeof(line)) <= 0 ||
        rb->strcmp(line, "RBQ1")) {
        rb->close(fd);
        return;
    }
    while (smart_query_count < RBPREP_SMART_QUERIES &&
           rb->read_line(fd, line, sizeof(line)) > 0) {
        char *separator = rb->strchr(line, '\t');
        uint32_t playlist_id;
        if (!separator)
            continue;
        *separator = '\0';
        playlist_id = rb->strtoul(line, NULL, 10);
        if (playlist_id)
            smart_query_ids[smart_query_count++] = playlist_id;
    }
    rb->close(fd);
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
        track->import_date = read_u16(data + 30);
        track->comments_offset = read_u32(data + 32);
        track->tags_offset = read_u32(data + 36);
        track->search_offset = read_u32(data + 40);
    } else {
        track->year = 0;
        track->import_date = 0;
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
    if (node->kind != 0 && playlist_has_smart_query(node->source_id))
        node->kind = 2;
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

static int playlist_browser_count(void)
{
    int favorites = 0;
    int slot;

    if (tree_parent == RBPREP_ROOT_NODE)
        for (slot = 0; slot < 2; slot++)
            if (favorite_playlist_nodes[slot] >= 0)
                favorites++;
    return tree_child_count + favorites + (playlist_add_mode ? 1 : 0);
}

static int playlist_node_at_visible(int ordinal,
                                    struct rbprep_node_record *result,
                                    int *favorite_slot)
{
    int slot;

    if (favorite_slot)
        *favorite_slot = -1;
    if (playlist_add_mode) {
        if (ordinal == 0)
            return -1;
        ordinal--;
    }
    if (tree_parent == RBPREP_ROOT_NODE) {
        for (slot = 0; slot < 2; slot++) {
            int node_index = favorite_playlist_nodes[slot];

            if (node_index < 0)
                continue;
            if (ordinal-- == 0) {
                if (result && !read_node_record(node_index, result))
                    return -1;
                if (favorite_slot)
                    *favorite_slot = slot;
                return node_index;
            }
        }
    }
    return child_node_at(tree_parent, ordinal, result);
}

static void invalidate_playlist_cache(void)
{
    int row;

    for (row = 0; row < RBPREP_LIST_ROWS; row++)
        playlist_cache[row].valid = false;
}

static bool cached_playlist_node(int ordinal,
                                 struct rbprep_playlist_cache_row **result)
{
    int slot;
    struct rbprep_playlist_cache_row *cached;

    if (ordinal < 0 || ordinal >= tree_child_count)
        return false;
    slot = ordinal % RBPREP_LIST_ROWS;
    cached = &playlist_cache[slot];
    if (!cached->valid || cached->parent != tree_parent ||
        cached->ordinal != ordinal) {
        cached->valid = false;
        cached->parent = tree_parent;
        cached->ordinal = ordinal;
        cached->node_index = tree_children[ordinal];
        if (!read_node_record(cached->node_index, &cached->node) ||
            !read_index_string(cached->node.name_offset, cached->name,
                               sizeof(cached->name)))
            return false;
        cached->valid = true;
    }
    *result = cached;
    return true;
}

static void refresh_tree_children(uint32_t parent)
{
    int index;
    struct rbprep_node_record node;

    tree_parent = parent;
    tree_child_count = 0;
    tree_parent_name[0] = '\0';
    favorite_playlist_nodes[0] = favorite_playlist_nodes[1] = -1;
    invalidate_playlist_cache();
    for (index = 0; (uint32_t)index < library_node_count; index++) {
        if (!read_node_record(index, &node))
            break;
        if (favorite_playlist_ids[0] > 0 &&
            node.source_id == (uint32_t)favorite_playlist_ids[0])
            favorite_playlist_nodes[0] = index;
        if (favorite_playlist_ids[1] > 0 &&
            node.source_id == (uint32_t)favorite_playlist_ids[1])
            favorite_playlist_nodes[1] = index;
        if (node.parent == parent && tree_child_count < RBPREP_TREE_NODES)
            tree_children[tree_child_count++] = index;
    }
    if (parent != RBPREP_ROOT_NODE && read_node_record(parent, &node))
        read_index_string(node.name_offset, tree_parent_name,
                          sizeof(tree_parent_name));
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

static int find_track_row_in_collection(int track_index)
{
    unsigned char rows[64 * 4];
    uint32_t position;

    if (track_index < 0 || (uint32_t)track_index >= library_track_count)
        return -1;
    if (track_sort_key == TRACK_SORT_TITLE ||
        library_sort_offsets[track_sort_key] == 0)
        position = track_index;
    else {
        uint32_t base;

        position = library_track_count;
        for (base = 0; base < library_track_count; base += 64) {
            uint32_t count = MIN(64u, library_track_count - base);
            uint32_t item;

            if (!read_index_at(library_sort_offsets[track_sort_key] +
                               base * 4, rows, count * 4))
                return -1;
            for (item = 0; item < count; item++) {
                if ((int)read_u32(rows + item * 4) == track_index) {
                    position = base + item;
                    break;
                }
            }
            if (position < library_track_count)
                break;
        }
        if (position >= library_track_count)
            return -1;
    }
    if (track_sort_descending)
        position = library_track_count - 1 - position;
    return position <= INT_MAX ? (int)position : -1;
}

static int track_index_at_row(int row)
{
    unsigned char data[4];
    struct rbprep_node_record node;

    if (active_playlist_node < 0) {
        if (collection_shuffle_active)
            return row >= 0 && row < search_result_count
                 ? (int)(((uint64_t)shuffle_multiplier * row +
                           shuffle_offset) % search_result_count) : -1;
        if (search_active) {
            if (row < 0 || row >= search_result_count ||
                search_result_fd < 0 ||
                rb->lseek(search_result_fd, (off_t)row * 4, SEEK_SET) < 0 ||
                !read_exact(search_result_fd, data, sizeof(data)))
                return -1;
            return read_u32(data);
        }
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
    unsigned char output[256 * 4];
    int buffered = 0;
    bool write_ok = true;
    int row;

    if (search_result_fd >= 0) {
        rb->close(search_result_fd);
        search_result_fd = -1;
    }
    collection_shuffle_active = false;
    search_active = track_search[0] != '\0';
    search_result_count = 0;
    if (!search_active) {
        track_row_count = library_track_count;
        return;
    }
    rbprep_store_ensure_state_dir(rb);
    search_result_fd = rb->open(RBPREP_SEARCH_RESULTS,
                                O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (search_result_fd < 0) {
        search_active = false;
        track_row_count = library_track_count;
        rb->splash(HZ * 2, "Could not create search results");
        restore_black_canvas();
        return;
    }
    for (row = 0; (uint32_t)row < library_track_count; row++) {
        int index = collection_sorted_index_at(row);
        if ((row & 127) == 0)
            rb->splash_progress(row, library_track_count,
                                "Searching %.24s", track_search);
        if (index >= 0 && track_matches_search(index)) {
            write_u32(output + buffered * 4, index);
            buffered++;
            search_result_count++;
            if (buffered == 256) {
                if (!write_exact(search_result_fd, output, sizeof(output))) {
                    write_ok = false;
                    break;
                }
                buffered = 0;
            }
        }
    }
    if (write_ok && buffered > 0)
        write_ok = write_exact(search_result_fd, output, buffered * 4);
    if (rb->close(search_result_fd) < 0)
        write_ok = false;
    search_result_fd = -1;
    if (write_ok)
        search_result_fd = rb->open(RBPREP_SEARCH_RESULTS, O_RDONLY);
    if (!write_ok || search_result_fd < 0) {
        search_active = false;
        search_result_count = 0;
        rb->splash(HZ * 2, "Search result write failed");
        restore_black_canvas();
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

static int date_ordinal(int year, int month, int day)
{
    if (month <= 2) {
        year--;
        month += 12;
    }
    return year * 365 + year / 4 - year / 100 + year / 400 +
           (153 * (month - 3) + 2) / 5 + day - 1;
}

static void refresh_recent_track_count(void)
{
    struct tm *now = rb->get_time();
    int today;
    int cutoff;
    int index;

    recent_tracks_30d = 0;
    if (!now || library_fd < 0)
        return;
    today = date_ordinal(now->tm_year + 1900, now->tm_mon + 1,
                         now->tm_mday);
    cutoff = today - 29;
    for (index = 0; (uint32_t)index < library_track_count; index++) {
        struct rbprep_track_record track;
        int year;
        int month;
        int day;
        int ordinal;

        if (!read_track_record(index, &track) || !track.import_date)
            continue;
        year = 1980 + (track.import_date >> 9);
        month = (track.import_date >> 5) & 15;
        day = track.import_date & 31;
        if (month < 1 || month > 12 || day < 1 || day > 31)
            continue;
        ordinal = date_ordinal(year, month, day);
        if (ordinal >= cutoff && ordinal <= today)
            recent_tracks_30d++;
    }
    recent_tracks_ready = true;
}

static bool open_library_index(void)
{
    unsigned char header[RBPREP_INDEX_HEADER];
    uint64_t track_end;
    uint64_t node_end;
    uint64_t member_end;
    uint64_t string_end;
    uint64_t previous_end;
    off_t file_size;
    int version;
    int header_size;
    int i;

    if (library_fd >= 0)
        rb->close(library_fd);
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
    file_size = rb->filesize(library_fd);
    if (file_size < 0 || header_size < 40 || header_size > file_size ||
        (version >= 3 &&
        (header_size < RBPREP_INDEX_HEADER ||
         rb->read(library_fd, header + 40,
                  RBPREP_INDEX_HEADER - 40) != RBPREP_INDEX_HEADER - 40)))
        goto invalid;
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
    library_string_size = read_u32(header + 36);
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
    library_sort_offsets[TRACK_SORT_IMPORTED] = version >= 3
                                               ? read_u32(header + 60) : 0;

    track_end = (uint64_t)library_track_offset +
                (uint64_t)library_track_count * library_track_record_size;
    node_end = (uint64_t)library_node_offset +
               (uint64_t)library_node_count * library_node_record_size;
    member_end = (uint64_t)library_member_offset +
                 (uint64_t)library_member_count * 4;
    string_end = (uint64_t)library_string_offset + library_string_size;
    if (library_track_offset < (uint32_t)header_size ||
        library_node_offset < track_end ||
        library_member_offset < node_end ||
        string_end > (uint64_t)file_size)
        goto invalid;
    previous_end = member_end;
    if (version >= 3) {
        for (i = TRACK_SORT_BPM; i < TRACK_SORT_COUNT; i++) {
            uint64_t sort_end = (uint64_t)library_sort_offsets[i] +
                                (uint64_t)library_track_count * 4;
            /* RBI3 originally reserved its final header word. Accept those
               older indexes without Import Date and fall back to title order
               until the compact collection index is refreshed. */
            if (library_sort_offsets[i] == 0)
                continue;
            if (library_sort_offsets[i] < previous_end ||
                sort_end > library_string_offset)
                goto invalid;
            previous_end = sort_end;
        }
    }
    if (library_string_offset < previous_end)
        goto invalid;
    refresh_tree_children(RBPREP_ROOT_NODE);
    if (!recent_tracks_ready)
        refresh_recent_track_count();
    return true;

invalid:
    rb->close(library_fd);
    library_fd = -1;
    library_index_version = 0;
    library_track_count = library_node_count = library_member_count = 0;
    recent_tracks_30d = 0;
    recent_tracks_ready = false;
    library_track_offset = library_node_offset = library_member_offset = 0;
    library_string_offset = library_string_size = 0;
    return false;
}

static int clamp_playhead(int value)
{
    return MAX(0, MIN(track_length, value));
}

/* Whole-BPM timing is table driven so the timecode path never has to divide
   by a changing tempo at frame rate. Fractional BPMs interpolate adjacent
   entries; exact imported beat markers remain authoritative. */
static const uint16_t beat_period_lut[231] = {
    3000,2857,2727,2609,2500,2400,2308,2222,2143,2069,2000,1935,1875,
    1818,1765,1714,1667,1622,1579,1538,1500,1463,1429,1395,1364,1333,
    1304,1277,1250,1224,1200,1176,1154,1132,1111,1091,1071,1053,1034,
    1017,1000,984,968,952,938,923,909,896,882,870,857,845,833,822,811,
    800,789,779,769,759,750,741,732,723,714,706,698,690,682,674,667,
    659,652,645,638,632,625,619,612,606,600,594,588,583,577,571,566,
    561,556,550,545,541,536,531,526,522,517,513,508,504,500,496,492,
    488,484,480,476,472,469,465,462,458,455,451,448,444,441,438,435,
    432,429,426,423,420,417,414,411,408,405,403,400,397,395,392,390,
    387,385,382,380,377,375,373,370,368,366,364,361,359,357,355,353,
    351,349,347,345,343,341,339,337,335,333,331,330,328,326,324,323,
    321,319,317,316,314,312,311,309,308,306,305,303,302,300,299,297,
    296,294,293,291,290,288,287,286,284,283,282,280,279,278,276,275,
    274,273,271,270,269,268,267,265,264,263,262,261,260,259,258,256,
    255,254,253,252,251,250,249,248,247,246,245,244,243,242,241,240
};

static int beat_period_ms(void)
{
    int whole;
    int fraction;
    int first;
    int second;

    if (grid_bpm_x100 <= 0)
        return 500;
    if (grid_bpm_x100 < 2000 || grid_bpm_x100 > 25000)
        return MAX(1, 6000000 / grid_bpm_x100);
    whole = grid_bpm_x100 / 100;
    fraction = grid_bpm_x100 % 100;
    first = beat_period_lut[whole - 20];
    if (!fraction || whole >= 250)
        return first;
    second = beat_period_lut[whole - 19];
    return MAX(1, (first * (100 - fraction) + second * fraction + 50) / 100);
}

static bool imported_grid_resident(void)
{
    return imported_grid_cached && beat_count > 0;
}

static bool source_beat_at(int index, int *time_ms, int *number)
{
    struct rbprep_grid_beat beat;

    /* This function is used directly by the frame renderer.  Cached-only is a
       deliberate safety boundary: a missing page falls back to the synthetic
       BPM grid instead of seeking the RBW file while audio owns storage. */
    if (!imported_grid_resident() || index < 0 || index >= beat_count ||
        !rbprep_grid_beat_cached(&grid_reader, index, &beat))
        return false;
    if (time_ms)
        *time_ms = beat.time_ms;
    if (number)
        *number = beat.number;
    return true;
}

static int adjusted_beat_number(int index)
{
    int time_ms;
    int number;

    if (!source_beat_at(index, &time_ms, &number))
        number = (index & 3) + 1;
    else if (index > 0) {
        int previous_time;
        int previous_number;

        if (source_beat_at(index - 1, &previous_time, &previous_number)) {
            int source_period = grid_source_bpm_x100 > 0
                              ? MAX(1, 6000000 / grid_source_bpm_x100)
                              : 500;
            int delta = MAX(1, time_ms - previous_time);
            int steps = MAX(1, MIN(8,
                (delta + source_period / 2) / source_period));
            int expected = ((previous_number - 1 + steps) & 3) + 1;

            if (number != expected) {
                int next_time;
                int next_number;

                if (index + 1 >= beat_count ||
                    !source_beat_at(index + 1, &next_time, &next_number)) {
                    number = expected;
                } else {
                    int next_delta = MAX(1, next_time - time_ms);
                    int next_steps = MAX(1, MIN(8,
                        (next_delta + source_period / 2) / source_period));
                    int predicted = ((number - 1 + next_steps) & 3) + 1;

                    if (predicted != next_number)
                        number = expected;
                }
            }
        }
    }
    return ((number - 1 + grid_beat_shift) & 3) + 1;
}

static int adjusted_beat_time(int index)
{
    long long delta;
    int time_ms;

    if (!source_beat_at(index, &time_ms, NULL))
        return grid_phase_ms + grid_offset;
    delta = (long long)time_ms - grid_phase_ms;
    if (grid_bpm_x100 > 0 && grid_source_bpm_x100 > 0)
        delta = delta * grid_source_bpm_x100 / grid_bpm_x100;
    return grid_phase_ms + grid_offset + delta;
}

static int current_beat_index(int time_ms)
{
    int index;
    int step;
    int low;
    int high;

    if (!imported_grid_resident() ||
        time_ms < adjusted_beat_time(0))
        return -1;
    index = MAX(0, MIN(beat_count - 1, beat_search_hint));
    if (adjusted_beat_time(index) <= time_ms) {
        for (step = 0; step < 16 && index + 1 < beat_count; step++) {
            if (adjusted_beat_time(index + 1) > time_ms) {
                beat_search_hint = index;
                return index;
            }
            index++;
        }
        if (index + 1 >= beat_count) {
            beat_search_hint = index;
            return index;
        }
        low = index + 1;
        high = beat_count;
    } else {
        for (step = 0; step < 16 && index > 0; step++) {
            if (adjusted_beat_time(index - 1) <= time_ms) {
                beat_search_hint = index - 1;
                return index - 1;
            }
            index--;
        }
        low = 0;
        high = index;
    }
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (adjusted_beat_time(middle) <= time_ms)
            low = middle + 1;
        else
            high = middle;
    }
    beat_search_hint = MAX(0, low - 1);
    return beat_search_hint;
}

static int nearest_beat_index(int time_ms)
{
    int before = current_beat_index(time_ms);
    int after;

    if (!imported_grid_resident())
        return -1;
    if (before < 0)
        return 0;
    after = before + 1;
    if (after >= beat_count)
        return before;
    if (time_ms - adjusted_beat_time(before) <=
        adjusted_beat_time(after) - time_ms)
        return before;
    return after;
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

    /* Cue and loop placement follows the nearest imported grid marker. */
    index = nearest_beat_index(time_ms);
    if (index >= 0)
        return clamp_playhead(adjusted_beat_time(index));

    period = beat_period_ms();
    base = grid_phase_ms + grid_offset;
    delta = (long long)time_ms - base;
    if (delta >= 0)
        beat = (delta + period / 2) / period;
    else
        beat = (delta - period / 2) / period;
    return clamp_playhead(base + beat * period);
}

static int cue_slot_for_delete(int preferred)
{
    /* Delete is deliberately slot-exact.  Falling back to the cue nearest
       the playhead made an empty, newly-advanced slot silently target the
       most recently dropped cue instead of the slot shown in the HUD. */
    if (preferred >= 0 && preferred < 16 && hotcues[preferred] >= 0)
        return preferred;
    return -1;
}

static void set_cue_focus_slot(int slot)
{
    /* HOTCUE has one authoritative slot focus. Every cue tool consumes this
       value, so changing it in SLOT or DELETE immediately carries through to
       MOVE and COLOR as well (and vice versa for workflow defaults). */
    cue_slot = MAX(0, MIN(15, slot));
    overview_dirty = true;
    force_full_redraw = true;
}

static void step_cue_focus_slot(int direction)
{
    set_cue_focus_slot((cue_slot + (direction > 0 ? 1 : 15)) & 15);
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
    /* Stretch the fixed analysis cache across Rockbox's authoritative track
       duration. Codec/container padding can differ from Rekordbox's sample
       count; using the cache duration here made drift accumulate until the
       waveform reached its end early and appeared frozen. */
    center = (long long)playhead * waveform_points /
             MAX(1, track_length);
    *first = zoom == 1 ? 0 : center - *span / 2;
}

static int time_to_x(int time_ms, int first, int span)
{
    long long sample;

    if (waveform_points <= 0 || track_length <= 0)
        return -1;
    sample = (long long)time_ms * waveform_points /
             MAX(1, track_length);
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

    view_start = MAX(0, (long long)first * track_length /
                        waveform_points);
    view_end = MIN(track_length,
                   (long long)(first + span) * track_length /
                   waveform_points);
    pixels_per_beat = (long long)period * RBPREP_DECK_WIDTH /
                      MAX(1, view_end - view_start);

    if (imported_grid_resident()) {
        int low = 0;
        int high = beat_count;

        /* Imported beat times are monotonic. Start at the first visible beat
           instead of rescanning the song from beat zero on every frame. */
        while (low < high) {
            int middle = low + (high - low) / 2;
            if (adjusted_beat_time(middle) < view_start)
                low = middle + 1;
            else
                high = middle;
        }
        for (i = low; i < beat_count; i++) {
            int time_ms = adjusted_beat_time(i);
            int number = adjusted_beat_number(i);
            int x;

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
    i = 0;
    if (view_start > base)
        i = (view_start - base + period - 1) / period;
    if (i % stride)
        i += stride - i % stride;
    for (; (long long)base + (long long)i * period <= view_end;
         i += stride) {
        int time_ms = base + i * period;
        int x;
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

    for (i = 0; i < 16; i++) {
        int x;
        int box_y;
        int color;

        if (hotcues[i] < 0)
            continue;
        x = time_to_x(hotcues[i], first, span);
        if (x < RBPREP_DECK_X ||
            x >= RBPREP_DECK_X + RBPREP_DECK_WIDTH)
            continue;

        color = cue_palette[hotcue_colors[i] & 7];
        box_y = RBPREP_WAVE_TOP + 3 + (i & 1) * 9;
        /* Key the exact-position line out from the RGB waveform before
           laying the cue color over it. */
        rb->lcd_set_foreground(LCD_BLACK);
        if (x > RBPREP_DECK_X)
            rb->lcd_vline(x - 1, box_y + 5, RBPREP_WAVE_BOTTOM);
        if (x + 1 < RBPREP_DECK_X + RBPREP_DECK_WIDTH)
            rb->lcd_vline(x + 1, box_y + 5, RBPREP_WAVE_BOTTOM);
        /* The colored body remains five pixels wide; its dark keyline and
           black three-pixel glyph preserve the number on bright peaks. */
        draw_micro_cue_marker(x, box_y, i, color);
        rb->lcd_set_foreground(color);
        rb->lcd_vline(x, box_y + 5, RBPREP_WAVE_BOTTOM);
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
    int audio_status = rb->audio_status();
    bool transport_active = (audio_status & AUDIO_STATUS_PLAY) &&
                            !(audio_status & AUDIO_STATUS_PAUSE);
    int samples_per_column = (span + RBPREP_DECK_WIDTH - 1) /
                             RBPREP_DECK_WIDTH;
    int cached_begin = MAX(0, first);
    int cached_end = MIN(waveform_points, first + span);
    bool active_raw_fallback = transport_active && !wave_index.valid &&
                 capabilities.device_class == RBPREP_DEVICE_CLASSIC &&
                 wave_reader.fully_cached;
    /* At the close 32x-128x scales, replace only the center 112 columns with
       exact peaks from the Classic's fully resident RBW cache.  The outer
       columns stay on the resident multi-resolution index.  This restores a
       moving inspection window without any deck-time file access or the CPU
       cost of rescanning the entire viewport every frame. */
    bool high_res_window = transport_active && wave_index.valid &&
                 capabilities.device_class == RBPREP_DEVICE_CLASSIC &&
                 wave_reader.fully_cached && zoom >= 32 &&
                 samples_per_column < 16 && cached_end > cached_begin &&
                 rbprep_wave_range_cached(&wave_reader, cached_begin,
                                          cached_end - cached_begin);
    bool exact = (!transport_active || active_raw_fallback) &&
                 samples_per_column < 16 &&
                 cached_end > cached_begin &&
                 rbprep_wave_range_cached(&wave_reader, cached_begin,
                                          cached_end - cached_begin);
    bool exact_profile = exact || high_res_window;
    int high_res_width = MIN(RBPREP_DECK_WIDTH,
                             RBPREP_HIGH_RES_WINDOW_WIDTH);
    int high_res_left = (RBPREP_DECK_WIDTH - high_res_width) / 2;
    int high_res_right = high_res_left + high_res_width;
    int source = first;
    int source_step = span / RBPREP_DECK_WIDTH;
    int source_remainder = span % RBPREP_DECK_WIDTH;
    int source_error = 0;
    int x;

    if (waveform_columns_valid && first == waveform_column_first &&
        span == waveform_column_span &&
        exact_profile == waveform_columns_exact)
        return;

    for (x = 0; x < RBPREP_DECK_WIDTH; x++) {
        int begin = source;
        int end;
        struct rbprep_wave_sample peak;
        int index;
        bool exact_column = exact ||
            (high_res_window && x >= high_res_left && x < high_res_right);

        /* Bresenham-style source stepping exactly partitions the viewport with
           one division per frame instead of two 64-bit divisions per column. */
        source += source_step;
        source_error += source_remainder;
        if (source_error >= RBPREP_DECK_WIDTH) {
            source++;
            source_error -= RBPREP_DECK_WIDTH;
        }
        end = source;
        waveform_columns[x].valid = false;
        if (end <= 0 || begin >= waveform_points) {
            continue;
        }
        begin = MAX(0, begin);
        if (end <= begin)
            end = begin + 1;
        end = MIN(end, waveform_points);

        if (exact_column) {
            if (!rbprep_wave_sample_cached(&wave_reader, begin, &peak))
                continue;
            for (index = begin + 1; index < end; index++) {
                struct rbprep_wave_sample sample;

                if (!rbprep_wave_sample_cached(&wave_reader, index, &sample))
                    break;
                if (sample.amplitude > peak.amplitude)
                    peak = sample;
            }
        } else if (!rbprep_wave_index_range_peak(&wave_index, begin, end,
                                                  &peak)) {
            /* Never substitute the miniature 320-column overview for the main
               deck.  Missing resident data is shown as unavailable rather
               than silently reducing 64x/128x detail. */
            continue;
        }

        waveform_columns[x].amplitude = peak.amplitude;
        waveform_columns[x].red = peak.red;
        waveform_columns[x].green = peak.green;
        waveform_columns[x].blue = peak.blue;
        waveform_columns[x].valid = true;
    }
    waveform_column_first = first;
    waveform_column_span = span;
    waveform_columns_exact = exact_profile;
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

static void update_spectrum_levels(bool need_pitch, bool need_spectrum,
                                   bool need_chroma)
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
    /* One chromatic octave at C5..B5. This is high enough for the 1024-frame
       window to separate neighbouring notes, while still following the
       harmonic body of typical program material. */
    static const int32_t chroma_coefficients_44100[RBPREP_CHROMA_BANDS] = {
        2091327, 2090614, 2089814, 2088916, 2087908, 2086777,
        2085507, 2084083, 2082484, 2080690, 2078677, 2076418
    };
    static const int32_t chroma_coefficients_48000[RBPREP_CHROMA_BANDS] = {
        2092235, 2091633, 2090957, 2090199, 2089348, 2088393,
        2087321, 2086118, 2084769, 2083254, 2081554, 2079646
    };
    const int32_t *coefficients;
    const int32_t *chroma_coefficients;
    unsigned int generation;
    int capture;
    int frames;
    int i;
    int band;
    int bass_raw;

    if (TIME_BEFORE(*rb->current_tick, spectrum_deadline))
        return;
    spectrum_deadline = *rb->current_tick +
        MAX(1, HZ / MAX(1, capabilities.visualizer_fps));
    generation = pcm_capture_generation;
    if (generation == spectrum_generation) {
        if (!(rb->audio_status() & AUDIO_STATUS_PLAY) ||
            (rb->audio_status() & AUDIO_STATUS_PAUSE)) {
            if (need_spectrum) {
                for (band = 0; band < RBPREP_SPECTRUM_BANDS; band++)
                    spectrum_levels[band] = spectrum_levels[band] * 7 / 8;
                spectrum_bass = spectrum_bass * 7 / 8;
                spectrum_bass_hit = spectrum_bass_hit * 3 / 4;
            }
            if (need_chroma)
                for (band = 0; band < RBPREP_CHROMA_BANDS; band++)
                    chroma_levels[band] = chroma_levels[band] * 7 / 8;
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
    pcm_snapshot_frames = frames;
    generation = pcm_capture_generation;
    rb->pcm_play_unlock();
    spectrum_generation = generation;
    if (frames < 64)
        return;

    if (need_pitch || need_spectrum || need_chroma) {
        for (i = 0; i < frames; i++) {
            int weight = i <= frames / 2 ? i : frames - 1 - i;
            int sample = ((int)pcm_snapshot[i * 2] +
                          (int)pcm_snapshot[i * 2 + 1]) >> 1;
            /* About a 110 Hz one-pole low pass at 44.1 kHz keeps the pitch
               detector focused on the woofer range instead of harmonics. */
            if (need_pitch) {
                bass_filter_state += (sample - bass_filter_state) / 64;
                pcm_pitch[i] = bass_filter_state >> 4;
            }
            pcm_mono[i] = (sample * weight / MAX(1, frames / 2)) >> 8;
        }
    }
    if (need_spectrum) {
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
        if (need_pitch)
            update_bass_pitch(frames);
    }

    if (need_chroma) {
        chroma_coefficients = pcm_sample_rate >= 46000
                            ? chroma_coefficients_48000
                            : chroma_coefficients_44100;
        for (band = 0; band < RBPREP_CHROMA_BANDS; band++) {
            int32_t s1 = 0;
            int32_t s2 = 0;
            int coefficient = chroma_coefficients[band];
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
            if (raw > chroma_levels[band])
                chroma_levels[band] =
                    (chroma_levels[band] + raw * 3) / 4;
            else
                chroma_levels[band] =
                    (chroma_levels[band] * 7 + raw) / 8;
        }
    }
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
    /* Pitch classes around a steelpan-style circle of fifths, rooted at F#:
       F#, C#, G#, D#, A#, F, C, G, D, A, E, B. Lower, larger tone fields
       sit near the rim; the higher register moves inward. */
    static const unsigned char steelpan_angle[12] = {
        0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5
    };
    static const char *note_names[] = {
        "F#1", "G1", "G#1", "A1", "A#1", "B1", "C2", "C#2", "D2",
        "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2"
    };
    int bass = MIN(255, spectrum_bass + spectrum_bass_hit / 2);
    int neon_pulse = MIN(255, 70 + bass / 2 + spectrum_bass_hit / 3);
    bool rattling = spectrum_bass >= 204;
    int rattle = rattling && ((*rb->current_tick / MAX(1, HZ / 20)) & 1)
               ? 1 : 0;
    int centers[2] = { 78 - rattle, 222 + rattle };
    int speaker;
    int note;
    char line[32];

    rb->lcd_set_foreground(LCD_RGBPACK(1, 3, 5));
    rb->lcd_fillrect(8 + rattle, RBPREP_WAVE_TOP + 10,
                     RBPREP_DECK_WIDTH - 16, 132);
    rb->lcd_set_foreground(LCD_RGBPACK(8, 55, 38));
    rb->lcd_drawrect(6 + rattle, RBPREP_WAVE_TOP + 8,
                     RBPREP_DECK_WIDTH - 12, 136);
    rb->lcd_set_foreground(rattling ? LCD_WHITE : RBPREP_GREEN);
    rb->lcd_drawrect(8 + rattle, RBPREP_WAVE_TOP + 10,
                     RBPREP_DECK_WIDTH - 16, 132);
    rb->lcd_set_foreground(LCD_RGBPACK(35, 205, 255));
    rb->lcd_hline(14 + rattle, RBPREP_DECK_WIDTH - 15 + rattle,
                  RBPREP_WAVE_TOP + 12);
    centered_text(0, RBPREP_DECK_WIDTH, RBPREP_WAVE_TOP + 14,
                  "SUB RESONANCE", LCD_RGBPACK(135, 255, 225));
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

        rb->lcd_set_foreground(LCD_BLACK);
        xlcd_fillcircle(cx, cy, 49);
        rb->lcd_set_foreground(LCD_RGBPACK(10, 65, 40));
        xlcd_drawcircle(cx, cy, 50);
        rb->lcd_set_foreground(RBPREP_GREEN);
        xlcd_drawcircle(cx, cy, 48);
        rb->lcd_set_foreground(LCD_RGBPACK(35, 205, 255));
        xlcd_drawcircle(cx, cy, 43);
        rb->lcd_set_foreground(LCD_RGBPACK(3,
                                           18 + bass / 5,
                                           26 + bass / 4));
        xlcd_fillcircle(cx, cy, cone);
        rb->lcd_set_foreground(LCD_RGBPACK(MIN(255, 45 + neon_pulse / 3),
                                           MIN(255, 125 + neon_pulse / 2),
                                           MIN(255, 150 + neon_pulse / 3)));
        xlcd_drawcircle(cx, cy, cone);
        rb->lcd_set_foreground(LCD_BLACK);
        xlcd_fillcircle(cx, cy, 9 + spectrum_bass_hit / 40);
        rb->lcd_set_foreground(LCD_RGBPACK(225, 45, 255));
        xlcd_drawcircle(cx, cy, 10 + spectrum_bass_hit / 40);

        /* Each speaker doubles as a compact steelpan note field. */
        for (note = 0; note < (int)ARRAYLEN(note_names); note++) {
            int angle = steelpan_angle[note % 12];
            int radius = note < 6 ? 31 : note < 12 ? 26 : 17;
            int x = cx + cosine[angle] * radius / 256;
            int y = cy + sine[angle] * radius / 256;
            bool active = note == bass_note_index &&
                          bass_pitch_confidence >= 36;

            rb->lcd_set_foreground(active ? LCD_WHITE
                                          : note < 6 ? RBPREP_GREEN
                                          : note < 12
                                          ? LCD_RGBPACK(40, 205, 255)
                                          : LCD_RGBPACK(215, 45, 255));
            if (active) {
                xlcd_drawcircle(x, y, 4);
                xlcd_fillcircle(x, y, 3);
            } else {
                xlcd_drawcircle(x, y, 2);
                rb->lcd_drawpixel(x, y);
            }
        }
    }

    if (bass_note_index >= 0 && bass_pitch_confidence >= 36) {
        centered_text(118, 64, RBPREP_WAVE_TOP + 51,
                      note_names[bass_note_index], LCD_WHITE);
        rb->snprintf(line, sizeof(line), "%d.%dHz",
                     bass_frequency_x10 / 10, bass_frequency_x10 % 10);
        centered_text(118, 64, RBPREP_WAVE_TOP + 69, line,
                      LCD_RGBPACK(40, 220, 255));
    } else {
        centered_text(118, 64, RBPREP_WAVE_TOP + 51, "--", LCD_LIGHTGRAY);
        centered_text(118, 64, RBPREP_WAVE_TOP + 69,
                      "LISTEN", LCD_RGBPACK(155, 45, 205));
    }
    rb->snprintf(line, sizeof(line), "%ddB", spectrum_level_db(spectrum_bass));
    centered_text(118, 64, RBPREP_WAVE_TOP + 87, line,
                  rattling ? LCD_WHITE : RBPREP_GREEN);
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

static int visual_beat_phase(int *beat_number)
{
    int period = beat_period_ms();
    int start;
    int elapsed;

    if (imported_grid_resident()) {
        int index = current_beat_index(playhead);
        int end;

        if (index < 0) {
            if (beat_number)
                *beat_number = 1;
            return 0;
        }
        start = adjusted_beat_time(index);
        end = index + 1 < beat_count
            ? adjusted_beat_time(index + 1) : start + period;
        if (beat_number)
            *beat_number = adjusted_beat_number(index);
        return MAX(0, MIN(255,
            (long long)(playhead - start) * 256 / MAX(1, end - start)));
    }

    start = grid_phase_ms + grid_offset;
    elapsed = playhead - start;
    if (elapsed < 0) {
        if (beat_number)
            *beat_number = 1;
        return 0;
    }
    if (beat_number)
        *beat_number = (elapsed / period & 3) + 1;
    return (long long)(elapsed % period) * 256 / period;
}

static void capture_spectrum_history(void)
{
    if (spectrum_history_generation == spectrum_generation)
        return;
    spectrum_history_head =
        (spectrum_history_head + 1) % RBPREP_SPECTRUM_HISTORY;
    rb->memcpy(spectrum_history[spectrum_history_head], spectrum_levels,
               sizeof(spectrum_levels));
    spectrum_history_generation = spectrum_generation;
}

static void draw_spectral_canyon(void)
{
    const int center = RBPREP_DECK_WIDTH / 2;
    const int horizon = RBPREP_WAVE_TOP + 19;
    const int floor = RBPREP_WAVE_BOTTOM - 15;
    int beat_number;
    int beat_phase = visual_beat_phase(&beat_number);
    int row;
    int band;

    capture_spectrum_history();
    text(5, RBPREP_WAVE_TOP + 3, "SPECTRAL CANYON",
         LCD_RGBPACK(120, 255, 218));

    /* Static perspective rails establish frequency lanes without requiring
       a framebuffer or another history surface. */
    for (band = 0; band < RBPREP_SPECTRUM_BANDS; band += 2) {
        int near_x = 4 + band * (RBPREP_DECK_WIDTH - 9) /
                           (RBPREP_SPECTRUM_BANDS - 1);
        int far_x = center - 51 + band * 102 /
                              (RBPREP_SPECTRUM_BANDS - 1);

        rb->lcd_set_foreground(LCD_RGBPACK(7, 31, 23));
        rb->lcd_drawline(far_x, horizon, near_x, floor);
    }

    /* Beat-locked runway gates travel toward the listener. Downbeats are
       white; the remaining beats retain the selected accent colour. */
    for (row = 0; row < 4; row++) {
        int travel = (beat_phase + row * 64) & 255;
        int y = horizon + travel * (floor - horizon) / 255;
        int half = 51 + travel * (center - 55) / 255;

        rb->lcd_set_foreground(((beat_number - 1 + row) & 3) == 0
                              ? LCD_WHITE : RBPREP_GREEN_DIM);
        rb->lcd_hline(center - half, center + half, y);
    }

    /* Old spectra occupy the horizon; the newest ridge fills the foreground.
       Sixteen by twenty bytes is the complete scrolling history. */
    for (row = 0; row < RBPREP_SPECTRUM_HISTORY; row++) {
        int history = spectrum_history_head < 0 ? 0 :
            (spectrum_history_head + 1 + row) % RBPREP_SPECTRUM_HISTORY;
        int baseline = horizon + 5 + row * (floor - horizon - 5) /
                                      (RBPREP_SPECTRUM_HISTORY - 1);
        int half = 52 + row * (center - 57) /
                          (RBPREP_SPECTRUM_HISTORY - 1);
        int previous_x = center - half;
        int previous_y = baseline;
        int color = row > 12 ? LCD_RGBPACK(125, 255, 220)
                  : row > 7 ? RBPREP_GREEN
                            : LCD_RGBPACK(15, 88, 68);

        for (band = 0; band < RBPREP_SPECTRUM_BANDS; band++) {
            int x = center - half + band * half * 2 /
                                      (RBPREP_SPECTRUM_BANDS - 1);
            int lift = spectrum_history[history][band] * (4 + row / 2) /
                       255;
            int y = baseline - lift;

            rb->lcd_set_foreground(color);
            if (band > 0)
                rb->lcd_drawline(previous_x, previous_y, x, y);
            if (row > 10 && lift > 2)
                rb->lcd_vline(x, y, baseline);
            previous_x = x;
            previous_y = y;
        }
    }
}

static void draw_stereo_orbit(void)
{
    const int cx = RBPREP_DECK_WIDTH / 2;
    const int cy = RBPREP_WAVE_TOP + 70;
    const int radius = 58;
    uint64_t mono_energy = 0;
    uint64_t side_energy = 0;
    int previous_x = cx;
    int previous_y = cy;
    int correlation = 0;
    int point;
    char line[24];

    text(5, RBPREP_WAVE_TOP + 3, "STEREO ORBIT",
         LCD_RGBPACK(105, 225, 255));
    rb->lcd_set_foreground(LCD_RGBPACK(18, 48, 43));
    xlcd_drawcircle(cx, cy, radius);
    xlcd_drawcircle(cx, cy, radius - 1);
    rb->lcd_hline(cx - radius, cx + radius, cy);
    rb->lcd_vline(cx, cy - radius, cy + radius);
    rb->lcd_set_foreground(LCD_RGBPACK(45, 78, 69));
    xlcd_drawcircle(cx, cy, radius / 2);

    if (pcm_snapshot_frames < 2) {
        centered_text(0, RBPREP_DECK_WIDTH, cy - 4,
                      "WAITING FOR AUDIO", LCD_LIGHTGRAY);
    } else {
        for (point = 0; point < 96; point++) {
            int frame = (long long)point * (pcm_snapshot_frames - 1) / 95;
            int left = pcm_snapshot[frame * 2];
            int right = pcm_snapshot[frame * 2 + 1];
            int side = left - right;
            int mono = left + right;
            int x = cx + (long long)side * (radius - 3) / 65536;
            int y = cy - (long long)mono * (radius - 3) / 65536;

            mono_energy += ABS(mono);
            side_energy += ABS(side);
            rb->lcd_set_foreground((point & 7) == 0
                                  ? LCD_WHITE
                                  : point & 1
                                  ? RBPREP_GREEN
                                  : LCD_RGBPACK(45, 205, 255));
            if (point > 0)
                rb->lcd_drawline(previous_x, previous_y, x, y);
            previous_x = x;
            previous_y = y;
        }
        if (mono_energy + side_energy > 0)
            correlation = ((int64_t)mono_energy - (int64_t)side_energy) * 100 /
                          (int64_t)(mono_energy + side_energy);
    }

    rb->lcd_set_foreground(LCD_RGBPACK(28, 49, 42));
    rb->lcd_drawrect(52, RBPREP_WAVE_BOTTOM - 15, 194, 8);
    rb->lcd_vline(cx, RBPREP_WAVE_BOTTOM - 17,
                  RBPREP_WAVE_BOTTOM - 5);
    rb->lcd_set_foreground(correlation < 0
                          ? LCD_RGBPACK(255, 176, 50) : RBPREP_GREEN);
    if (correlation >= 0)
        rb->lcd_fillrect(cx, RBPREP_WAVE_BOTTOM - 13,
                         correlation * 94 / 100 + 1, 4);
    else
        rb->lcd_fillrect(cx + correlation * 94 / 100,
                         RBPREP_WAVE_BOTTOM - 13,
                         -correlation * 94 / 100 + 1, 4);
    rb->snprintf(line, sizeof(line), "CORR %+d", correlation);
    centered_text(0, RBPREP_DECK_WIDTH, RBPREP_WAVE_BOTTOM - 29,
                  line, LCD_WHITE);
}

static void draw_phrase_map(void)
{
    const int cells = 32;
    const int left = 5;
    const int cell_width = 9;
    const int top = RBPREP_WAVE_TOP + 25;
    const int bottom = RBPREP_WAVE_BOTTOM - 28;
    int period = beat_period_ms();
    int base = grid_phase_ms + grid_offset;
    int current;
    int beat_number;
    int first;
    int phase = visual_beat_phase(&beat_number);
    int first_beat = 1;
    int bar = 1;
    int cell;
    char line[48];
    bool imported = imported_grid_resident();

    if (imported) {
        current = current_beat_index(playhead);
        if (current < 0)
            current = 0;
        first_beat = adjusted_beat_number(0);
        beat_number = adjusted_beat_number(current);
        bar = (current + first_beat - 1) / 4 + 1;
        first = current - beat_number + 1 - 12;
    } else {
        current = MAX(0, (playhead - base) / MAX(1, period));
        beat_number = (current & 3) + 1;
        bar = current / 4 + 1;
        first = current - beat_number + 1 - 12;
    }

    text(5, RBPREP_WAVE_TOP + 3, "PHRASE MAP  /  8 BARS",
         LCD_RGBPACK(120, 255, 218));
    rb->lcd_set_foreground(LCD_RGBPACK(5, 15, 11));
    rb->lcd_fillrect(left - 2, top - 3, cells * cell_width + 3,
                     bottom - top + 7);

    for (cell = 0; cell < cells; cell++) {
        int beat = first + cell;
        int start_time;
        int end_time;
        int sample_begin;
        int sample_end;
        int sample;
        int peak = 0;
        int peak_sample = 0;
        int x = left + cell * cell_width;
        int height;
        int color;
        bool valid = !imported || (beat >= 0 && beat < beat_count);
        bool downbeat;

        if (!valid) {
            rb->lcd_set_foreground(LCD_RGBPACK(13, 22, 17));
            rb->lcd_fillrect(x, bottom - 3, cell_width - 2, 4);
            continue;
        }
        if (imported) {
            start_time = adjusted_beat_time(beat);
            end_time = beat + 1 < beat_count
                     ? adjusted_beat_time(beat + 1)
                     : start_time + period;
            downbeat = adjusted_beat_number(beat) == 1;
        } else {
            start_time = base + beat * period;
            end_time = start_time + period;
            downbeat = (beat & 3) == 0;
        }
        if (end_time <= 0 || start_time >= track_length) {
            rb->lcd_set_foreground(LCD_RGBPACK(13, 22, 17));
            rb->lcd_fillrect(x, bottom - 3, cell_width - 2, 4);
            continue;
        }
        sample_begin = MAX(0, MIN(RBPREP_OVERVIEW_WIDTH - 1,
            (long long)start_time * RBPREP_OVERVIEW_WIDTH /
            MAX(1, track_length)));
        sample_end = MAX(sample_begin + 1,
            MIN(RBPREP_OVERVIEW_WIDTH,
                (long long)end_time * RBPREP_OVERVIEW_WIDTH /
                MAX(1, track_length)));
        for (sample = sample_begin; sample < sample_end; sample++) {
            if (overview_waveform[sample][0] > peak) {
                peak = overview_waveform[sample][0];
                peak_sample = sample;
            }
        }
        height = 5 + peak * (bottom - top - 8) / 255;
        color = peak > 0
              ? LCD_RGBPACK(overview_waveform[peak_sample][1],
                            overview_waveform[peak_sample][2],
                            overview_waveform[peak_sample][3])
              : LCD_RGBPACK(24, 45, 34);
        rb->lcd_set_foreground(color);
        rb->lcd_fillrect(x, bottom - height, cell_width - 2, height);
        rb->lcd_set_foreground(downbeat ? LCD_WHITE
                                       : LCD_RGBPACK(42, 72, 57));
        rb->lcd_vline(x, top - (downbeat ? 3 : 0), bottom + 2);
        if (beat == current) {
            int cursor = x + phase * (cell_width - 3) / 255;

            rb->lcd_set_foreground(LCD_WHITE);
            rb->lcd_drawrect(x - 1, top - 3, cell_width,
                             bottom - top + 6);
            rb->lcd_vline(cursor, top, bottom);
        }
    }

    rb->snprintf(line, sizeof(line), "BAR %d.%d   -3 < NOW > +4",
                 MIN(999, bar), beat_number);
    centered_text(0, RBPREP_DECK_WIDTH, RBPREP_WAVE_BOTTOM - 18,
                  line, LCD_WHITE);
}

static int track_key_pitch_class(void)
{
    int index = key_index_from_name(selected_key);

    return index < 0 ? 0 : index % 12;
}

static void draw_harmonic_constellation(void)
{
    static const char *note_names[RBPREP_CHROMA_BANDS] = {
        "C", "Db", "D", "Eb", "E", "F",
        "F#", "G", "Ab", "A", "Bb", "B"
    };
    static const unsigned char note_angles[RBPREP_CHROMA_BANDS] = {
        48, 53, 59, 0, 5, 11, 16, 21, 27, 32, 37, 43
    };
    static const int note_colors[RBPREP_CHROMA_BANDS] = {
        LCD_RGBPACK(255, 78, 88), LCD_RGBPACK(255, 130, 55),
        LCD_RGBPACK(255, 205, 55), LCD_RGBPACK(185, 240, 60),
        LCD_RGBPACK(70, 235, 105), LCD_RGBPACK(45, 230, 185),
        LCD_RGBPACK(45, 205, 255), LCD_RGBPACK(60, 135, 255),
        LCD_RGBPACK(120, 90, 255), LCD_RGBPACK(190, 80, 255),
        LCD_RGBPACK(245, 75, 210), LCD_RGBPACK(255, 75, 145)
    };
    const int cx = RBPREP_DECK_WIDTH / 2;
    const int cy = RBPREP_WAVE_TOP + 73;
    int strongest[3] = { -1, -1, -1 };
    int root = track_key_pitch_class();
    int node_x[RBPREP_CHROMA_BANDS];
    int node_y[RBPREP_CHROMA_BANDS];
    int note;
    int edge;
    char line[32];
    char display_key[24];

    text(5, RBPREP_WAVE_TOP + 3, "HARMONIC CONSTELLATION",
         LCD_RGBPACK(255, 120, 220));
    rb->lcd_set_foreground(LCD_RGBPACK(30, 48, 43));
    xlcd_drawcircle(cx, cy, 60);
    xlcd_drawcircle(cx, cy, 42);

    for (note = 0; note < RBPREP_CHROMA_BANDS; note++) {
        int relative = (note - root + 12) % 12;
        int angle = note_angles[relative];

        node_x[note] = cx + wheel_cosine[angle] * 54 / 256;
        node_y[note] = cy + wheel_sine[angle] * 54 / 256;
        if (strongest[0] < 0 ||
            chroma_levels[note] > chroma_levels[strongest[0]]) {
            strongest[2] = strongest[1];
            strongest[1] = strongest[0];
            strongest[0] = note;
        } else if (strongest[1] < 0 ||
                   chroma_levels[note] > chroma_levels[strongest[1]]) {
            strongest[2] = strongest[1];
            strongest[1] = note;
        } else if (strongest[2] < 0 ||
                   chroma_levels[note] > chroma_levels[strongest[2]]) {
            strongest[2] = note;
        }
    }

    for (edge = 0; edge < 3; edge++) {
        int a = strongest[edge];
        int b = strongest[(edge + 1) % 3];
        int level = MIN(chroma_levels[a], chroma_levels[b]);

        rb->lcd_set_foreground(level > 150 ? LCD_WHITE
                                          : LCD_RGBPACK(55, 105, 87));
        rb->lcd_drawline(node_x[a], node_y[a], node_x[b], node_y[b]);
    }

    for (note = 0; note < RBPREP_CHROMA_BANDS; note++) {
        int level = chroma_levels[note];
        int radius = 2 + level / 64;
        int relative = (note - root + 12) % 12;
        int angle = note_angles[relative];
        int label_x = cx + wheel_cosine[angle] * 69 / 256;
        int label_y = cy + wheel_sine[angle] * 69 / 256;

        rb->lcd_set_foreground(level > 50 ? note_colors[note]
                                         : LCD_RGBPACK(35, 55, 47));
        xlcd_fillcircle(node_x[note], node_y[note], radius);
        if (note == root) {
            rb->lcd_set_foreground(LCD_WHITE);
            xlcd_drawcircle(node_x[note], node_y[note], radius + 2);
        }
        text(label_x - (note_names[note][1] ? 5 : 2), label_y - 4,
             note_names[note], level > 65 ? LCD_WHITE : LCD_DARKGRAY);
    }

    format_key_name(selected_key, display_key, sizeof(display_key));
    rb->snprintf(line, sizeof(line), "KEY %s",
                 selected_key[0] ? display_key : note_names[root]);
    centered_text(cx - 38, 76, cy - 4, line, LCD_WHITE);
}

static void draw_turntable_headshell(int style, int stylus_x, int stylus_y)
{
    int shell_color = LCD_RGBPACK(218, 224, 220);

    if (style == 0 || style == 2) {
        /* Universal SME shell: a broad perforated casting, connector collar,
           offset finger lift and cartridge below the nose. Its long axis is
           set close to the groove tangent instead of following screen axes. */
        rb->lcd_set_foreground(LCD_BLACK);
        xlcd_filltriangle(stylus_x - 3, stylus_y - 4,
                          stylus_x + 6, stylus_y - 1,
                          stylus_x + 13, stylus_y - 15);
        xlcd_filltriangle(stylus_x - 3, stylus_y - 4,
                          stylus_x + 13, stylus_y - 15,
                          stylus_x + 5, stylus_y - 19);
        rb->lcd_set_foreground(style == 0 ? shell_color
                                         : LCD_RGBPACK(130, 139, 133));
        xlcd_filltriangle(stylus_x - 1, stylus_y - 4,
                          stylus_x + 5, stylus_y - 2,
                          stylus_x + 11, stylus_y - 15);
        xlcd_filltriangle(stylus_x - 1, stylus_y - 4,
                          stylus_x + 11, stylus_y - 15,
                          stylus_x + 6, stylus_y - 17);
        rb->lcd_set_foreground(LCD_BLACK);
        rb->lcd_drawline(stylus_x + 1, stylus_y - 5,
                         stylus_x + 6, stylus_y - 13);
        rb->lcd_drawline(stylus_x + 4, stylus_y - 4,
                         stylus_x + 9, stylus_y - 12);
        rb->lcd_set_foreground(LCD_WHITE);
        rb->lcd_drawpixel(stylus_x + 7, stylus_y - 15);
        rb->lcd_drawpixel(stylus_x + 10, stylus_y - 14);
        rb->lcd_set_foreground(LCD_BLACK);
        rb->lcd_drawline(stylus_x + 11, stylus_y - 15,
                         stylus_x + 20, stylus_y - 12);
        rb->lcd_set_foreground(shell_color);
        rb->lcd_drawline(stylus_x + 11, stylus_y - 16,
                         stylus_x + 20, stylus_y - 13);
        if (style == 2) {
            rb->lcd_set_foreground(LCD_BLACK);
            rb->lcd_fillrect(stylus_x - 3, stylus_y - 4, 7, 7);
            rb->lcd_set_foreground(LCD_RGBPACK(235, 238, 236));
            rb->lcd_fillrect(stylus_x - 2, stylus_y - 3, 5, 4);
            rb->lcd_set_foreground(LCD_RGBPACK(60, 64, 62));
            rb->lcd_hline(stylus_x - 2, stylus_x + 2, stylus_y);
        } else {
            rb->lcd_set_foreground(LCD_RGBPACK(28, 31, 29));
            rb->lcd_fillrect(stylus_x - 2, stylus_y - 3, 5, 5);
        }
    } else if (style == 1) {
        /* Concorde-style integrated pickup: slim tapered body, bright spine
           and colored stylus nose. */
        rb->lcd_set_foreground(LCD_BLACK);
        xlcd_filltriangle(stylus_x - 3, stylus_y - 2,
                          stylus_x + 4, stylus_y + 1,
                          stylus_x + 12, stylus_y - 17);
        xlcd_filltriangle(stylus_x - 3, stylus_y - 2,
                          stylus_x + 12, stylus_y - 17,
                          stylus_x + 7, stylus_y - 19);
        rb->lcd_set_foreground(LCD_WHITE);
        xlcd_filltriangle(stylus_x - 1, stylus_y - 2,
                          stylus_x + 3, stylus_y,
                          stylus_x + 10, stylus_y - 17);
        xlcd_filltriangle(stylus_x - 1, stylus_y - 2,
                          stylus_x + 10, stylus_y - 17,
                          stylus_x + 8, stylus_y - 18);
        rb->lcd_set_foreground(LCD_RGBPACK(255, 65, 45));
        xlcd_fillcircle(stylus_x, stylus_y + 1, 2);
    } else {
        /* The playful iPod shell still obeys the same pickup angle: the body
           is the pod, its screen is the slot, and a real stylus remains at
           the groove contact point. */
        rb->lcd_set_foreground(LCD_BLACK);
        rb->lcd_drawline(stylus_x + 6, stylus_y - 9,
                         stylus_x + 11, stylus_y - 17);
        xlcd_fillcircle(stylus_x + 5, stylus_y - 8, 7);
        rb->lcd_set_foreground(theme_body_shadow);
        xlcd_fillcircle(stylus_x + 5, stylus_y - 8, 6);
        rb->lcd_set_foreground(theme_body);
        xlcd_fillcircle(stylus_x + 5, stylus_y - 8, 5);
        rb->lcd_set_foreground(LCD_WHITE);
        rb->lcd_drawrect(stylus_x + 2, stylus_y - 12, 7, 4);
        rb->lcd_drawpixel(stylus_x + 5, stylus_y - 5);
        rb->lcd_set_foreground(LCD_BLACK);
        rb->lcd_fillrect(stylus_x - 2, stylus_y - 3, 5, 5);
    }
    rb->lcd_set_foreground(LCD_WHITE);
    rb->lcd_drawline(stylus_x, stylus_y,
                     stylus_x - 1, stylus_y + 3);
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_drawpixel(stylus_x - 1, stylus_y + 4);
}

static void fill_axis_quad(int x0, int y0, int x1, int y1,
                           int half_width, int color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int major = MAX(ABS(dx), ABS(dy));
    int minor = MIN(ABS(dx), ABS(dy));
    int length = MAX(1, major + minor / 2);
    int px = -dy * half_width / length;
    int py = dx * half_width / length;

    rb->lcd_set_foreground(color);
    xlcd_filltriangle(x0 + px, y0 + py, x1 + px, y1 + py,
                      x1 - px, y1 - py);
    xlcd_filltriangle(x0 + px, y0 + py, x1 - px, y1 - py,
                      x0 - px, y0 - py);
}

static void draw_phase_module(int cx, int cy, int angle)
{
    int tx = -wheel_sine[angle];
    int ty = wheel_cosine[angle];
    int rx = wheel_cosine[angle];
    int ry = wheel_sine[angle];
    int outer_x0 = cx - tx * 10 / 256;
    int outer_y0 = cy - ty * 10 / 256;
    int outer_x1 = cx + tx * 10 / 256;
    int outer_y1 = cy + ty * 10 / 256;
    int inner_x0 = cx - tx * 9 / 256;
    int inner_y0 = cy - ty * 9 / 256;
    int inner_x1 = cx + tx * 9 / 256;
    int inner_y1 = cy + ty * 9 / 256;
    int ring_x = cx - tx * 5 / 256;
    int ring_y = cy - ty * 5 / 256;
    int led_x0 = cx + tx * 2 / 256;
    int led_y0 = cy + ty * 2 / 256;
    int led_x1 = cx + tx * 7 / 256;
    int led_y1 = cy + ty * 7 / 256;

    /* Phase remote: the recognisable slim black wireless unit, with a metal
       pickup ring near its crown and a luminous strip down its lower face.
       The body stays tangent to the groove as the complete unit orbits. */
    fill_axis_quad(outer_x0, outer_y0, outer_x1, outer_y1, 4, LCD_WHITE);
    fill_axis_quad(inner_x0, inner_y0, inner_x1, inner_y1, 3, LCD_BLACK);
    rb->lcd_set_foreground(LCD_RGBPACK(145, 153, 148));
    xlcd_fillcircle(ring_x, ring_y, 3);
    rb->lcd_set_foreground(LCD_WHITE);
    xlcd_fillcircle(ring_x, ring_y, 2);
    rb->lcd_set_foreground(LCD_BLACK);
    xlcd_fillcircle(ring_x, ring_y, 1);
    rb->lcd_set_foreground(RBPREP_GREEN_DIM);
    rb->lcd_drawline(led_x0 - rx / 256, led_y0 - ry / 256,
                     led_x1 - rx / 256, led_y1 - ry / 256);
    rb->lcd_drawline(led_x0 + rx / 256, led_y0 + ry / 256,
                     led_x1 + rx / 256, led_y1 + ry / 256);
    rb->lcd_set_foreground(LCD_WHITE);
    rb->lcd_drawline(led_x0, led_y0, led_x1, led_y1);
}

static void draw_turntable_pitch_fader(void)
{
    int fader_x = 198;
    int fader_top = RBPREP_WAVE_TOP + 51;
    int fader_bottom = RBPREP_WAVE_BOTTOM - 18;
    int fader_center = (fader_top + fader_bottom) / 2;
    int fader_travel = MAX(1, (fader_bottom - fader_top) / 2 - 2);
    int pitch_slider_y;
    int tick;

    text(fader_x + 7, fader_top - 13, "+", LCD_RGBPACK(105, 118, 110));
    text(fader_x + 7, fader_bottom - 5, "-", LCD_RGBPACK(105, 118, 110));
    rb->lcd_set_foreground(LCD_RGBPACK(32, 38, 34));
    rb->lcd_fillrect(fader_x - 4, fader_top - 2, 9,
                     fader_bottom - fader_top + 5);
    rb->lcd_set_foreground(LCD_RGBPACK(100, 111, 104));
    rb->lcd_vline(fader_x, fader_top, fader_bottom);
    for (tick = fader_top; tick <= fader_bottom; tick += 8)
        rb->lcd_hline(fader_x - 3, fader_x + 3, tick);
    rb->lcd_hline(fader_x - 6, fader_x + 6, fader_center);
    rb->lcd_set_foreground(LCD_WHITE);
    rb->lcd_hline(fader_x - 7, fader_x + 7, fader_center);
    pitch_slider_y = fader_center -
                     pitch_bend_x100 * fader_travel / 1600;
    rb->lcd_set_foreground(RBPREP_GREEN);
    rb->lcd_fillrect(fader_x - 7, pitch_slider_y - 2, 15, 5);
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_hline(fader_x - 5, fader_x + 5, pitch_slider_y);
}

static void draw_turntable_counterweight(int pivot_x, int pivot_y,
                                         int front_dx, int front_dy)
{
    int major = MAX(ABS(front_dx), ABS(front_dy));
    int minor = MIN(ABS(front_dx), ABS(front_dy));
    int length = MAX(1, major + minor / 2);
    int ux = front_dx * 256 / length;
    int uy = front_dy * 256 / length;
    int stub_x = pivot_x - ux * 27 / 256;
    int stub_y = pivot_y - uy * 27 / 256;
    int weight_x0 = pivot_x - ux * 13 / 256;
    int weight_y0 = pivot_y - uy * 13 / 256;
    int weight_x1 = pivot_x - ux * 27 / 256;
    int weight_y1 = pivot_y - uy * 27 / 256;
    int band_x = pivot_x - ux * 20 / 256;
    int band_y = pivot_y - uy * 20 / 256;
    int px = -uy * 5 / 256;
    int py = ux * 5 / 256;

    /* The rear stub and weight share the arm's live axis. Parking or moving
       the stylus therefore rotates the counterweight as one rigid assembly. */
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_drawline(pivot_x, pivot_y, stub_x, stub_y);
    rb->lcd_drawline(pivot_x + 1, pivot_y, stub_x + 1, stub_y);
    rb->lcd_set_foreground(LCD_RGBPACK(184, 191, 187));
    rb->lcd_drawline(pivot_x, pivot_y - 1, stub_x, stub_y - 1);
    fill_axis_quad(weight_x0, weight_y0, weight_x1, weight_y1, 7,
                   LCD_BLACK);
    fill_axis_quad(weight_x0, weight_y0, weight_x1, weight_y1, 5,
                   LCD_RGBPACK(94, 102, 97));
    rb->lcd_set_foreground(LCD_RGBPACK(196, 202, 198));
    rb->lcd_drawline(band_x - px, band_y - py,
                     band_x + px, band_y + py);
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_drawline(weight_x1 - px, weight_y1 - py,
                     weight_x1 + px, weight_y1 + py);
}

static void draw_turntable(void)
{
    static const int target_rpm_x100[] = { 3333, 4500, 7800 };
    const int cx = 86;
    const int cy = (RBPREP_WAVE_TOP + RBPREP_WAVE_BOTTOM) / 2;
    int phase;
    int point;
    int slot;
    int lamp_phase;
    int progress_x1000;
    int groove_radius;
    int stylus_x;
    int stylus_y;
    int arm_end_x;
    int arm_end_y;
    int arm_mid1_x;
    int arm_mid1_y;
    int arm_mid2_x;
    int arm_mid2_y;
    int previous_x = cx;
    int previous_y = cy;
    char line[48];
    char delta[16];

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

    lamp_phase = (long long)*rb->current_tick *
                 target_rpm_x100[played_rpm_index] * 8 /
                 (HZ * 6000);

    /* Platter strobe dots rotate at the selected played RPM. The physical
       arc under the red target lamp is illuminated like a real SL-1200. */
    for (point = 0; point < 24; point++) {
        int angle = (point * 64 / 24 + phase) & 63;
        int x = cx + wheel_cosine[angle] * 65 / 256;
        int y = cy + wheel_sine[angle] * 65 / 256;
        bool lamp_arc = angle >= 54 && angle <= 62;

        rb->lcd_set_foreground(lamp_arc
                               ? ((lamp_phase & 1)
                                  ? LCD_RGBPACK(255, 48, 36)
                                  : LCD_RGBPACK(130, 12, 9))
                               : (point + (phase >> 1)) % 6 == 0
                               ? LCD_WHITE : LCD_RGBPACK(48, 66, 55));
        if ((point + phase) % 6 == 0)
            xlcd_fillcircle(x, y, 1);
        else
            rb->lcd_drawpixel(x, y);
    }

    /* The record rotates; the oscilloscope does not. A stable left-to-right
       trace makes transients readable while the platter, strobe and cue map
       retain the physical motion of the deck. */
    previous_x = cx - 49;
    previous_y = cy;
    for (point = 0; point < 99; point++) {
        int x = cx - 49 + point;
        int distance = ABS(x - cx);
        int vertical_limit = distance < 34 ? 24 :
                             distance < 44 ? 16 : 7;
        int sample = 0;
        int y;

        if (pcm_snapshot_frames > 0) {
            int frame = (long long)point * pcm_snapshot_frames / 99;

            frame = MIN(pcm_snapshot_frames - 1, frame);
            sample = ((int)pcm_snapshot[frame * 2] +
                      (int)pcm_snapshot[frame * 2 + 1]) / 2;
        }
        y = cy + MAX(-vertical_limit,
                     MIN(vertical_limit, sample / 1050));
        rb->lcd_set_foreground(ABS(sample) > 24500
                               ? LCD_WHITE : RBPREP_GREEN);
        if (point > 0)
            rb->lcd_drawline(previous_x, previous_y, x, y);
        previous_x = x;
        previous_y = y;
    }

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
        x = cx + wheel_cosine[angle] * cue_radius / 256;
        y = cy + wheel_sine[angle] * cue_radius / 256;
        draw_radial_cue_marker(cx, cy, x, y, slot,
                               cue_palette[hotcue_colors[slot] & 7]);
    }

    /* Phase rides the record instead of impersonating a spindle cap. Its
       long axis remains tangent to the groove and rotates with the platter. */
    if (turntable_headshell_style == 4) {
        int remote_angle = (phase + 10) & 63;
        int remote_x = cx + wheel_cosine[remote_angle] * 29 / 256;
        int remote_y = cy + wheel_sine[remote_angle] * 29 / 256;

        draw_phase_module(remote_x, remote_y, remote_angle);
    }
    rb->lcd_set_foreground(LCD_BLACK);
    xlcd_fillcircle(cx, cy, 2);
    rb->lcd_set_foreground(LCD_RGBPACK(225, 230, 226));
    rb->lcd_drawpixel(cx, cy);

    /* Controls belong to the chassis, beneath the mechanical arm assembly.
       Drawing the fader first lets the parked arm cross it naturally. */
    draw_turntable_pitch_fader();

    /* A real tonearm moves inward over the full side.  The stylus follows a
       fixed pickup angle while its groove radius falls from 56 to 34 pixels. */
    progress_x1000 = track_length > 0
                   ? MAX(0, MIN(1000,
                       (long long)playhead * 1000 / track_length)) : 0;
    groove_radius = 56 - progress_x1000 * 22 / 1000;
    stylus_x = cx + wheel_cosine[7] * groove_radius / 256;
    stylus_y = cy + wheel_sine[7] * groove_radius / 256;
    if (turntable_headshell_style == 4) {
        /* Full-length park position down the arm rail. The endpoint passes
           through the rest hook instead of collapsing back into the pivot. */
        arm_end_x = 211;
        arm_end_y = RBPREP_WAVE_BOTTOM - 18;
        arm_mid1_x = 200;
        arm_mid1_y = RBPREP_WAVE_TOP + 62;
        arm_mid2_x = 208;
        arm_mid2_y = RBPREP_WAVE_TOP + 101;
    } else {
        /* The headshell connector is behind the stylus along the tangent;
           the tonearm ends at that collar, not at the needle itself. */
        arm_end_x = stylus_x + 10;
        arm_end_y = stylus_y - 17;
        arm_mid1_x = (192 * 2 + arm_end_x) / 3;
        arm_mid1_y = ((RBPREP_WAVE_TOP + 30) * 2 + arm_end_y) / 3;
        arm_mid2_x = (192 + arm_end_x * 2) / 3;
        arm_mid2_y = (RBPREP_WAVE_TOP + 30 + arm_end_y * 2) / 3;
        if (turntable_arm_style) {
            arm_mid1_x += 5;
            arm_mid2_x -= 5;
        }
    }
    draw_turntable_counterweight(192, RBPREP_WAVE_TOP + 30,
                                 arm_mid1_x - 192,
                                 arm_mid1_y - (RBPREP_WAVE_TOP + 30));
    /* A three-pixel brushed-metal tube reads much more like the curved arm
       on a real deck than a single, computer-perfect line. */
    rb->lcd_set_foreground(LCD_RGBPACK(67, 73, 69));
    rb->lcd_drawline(192, RBPREP_WAVE_TOP + 31,
                     arm_mid1_x, arm_mid1_y + 1);
    rb->lcd_drawline(arm_mid1_x, arm_mid1_y + 1,
                     arm_mid2_x, arm_mid2_y + 1);
    rb->lcd_drawline(arm_mid2_x, arm_mid2_y + 1,
                     arm_end_x, arm_end_y + 1);
    rb->lcd_set_foreground(LCD_RGBPACK(178, 186, 181));
    rb->lcd_drawline(192, RBPREP_WAVE_TOP + 30, arm_mid1_x, arm_mid1_y);
    rb->lcd_drawline(arm_mid1_x, arm_mid1_y, arm_mid2_x, arm_mid2_y);
    rb->lcd_drawline(arm_mid2_x, arm_mid2_y, arm_end_x, arm_end_y);
    rb->lcd_drawline(192, RBPREP_WAVE_TOP + 29,
                     arm_mid1_x, arm_mid1_y - 1);
    rb->lcd_drawline(arm_mid1_x, arm_mid1_y - 1,
                     arm_mid2_x, arm_mid2_y - 1);
    rb->lcd_drawline(arm_mid2_x, arm_mid2_y - 1,
                     arm_end_x, arm_end_y - 1);
    rb->lcd_set_foreground(LCD_RGBPACK(225, 230, 226));
    xlcd_fillcircle(arm_mid1_x, arm_mid1_y, 1);
    xlcd_fillcircle(arm_mid2_x, arm_mid2_y, 1);
    if (turntable_headshell_style != 4) {
        draw_turntable_headshell(turntable_headshell_style,
                                 stylus_x, stylus_y);
    } else {
        int rest_y = RBPREP_WAVE_BOTTOM - 26;

        /* A visible Technics-style arm rest: tall post, lower cradle and an
           upper retaining hook wrapped around the parked tube. */
        rb->lcd_set_foreground(LCD_RGBPACK(108, 118, 112));
        rb->lcd_vline(217, rest_y - 13, rest_y + 11);
        rb->lcd_hline(210, 217, rest_y + 10);
        rb->lcd_hline(209, 216, rest_y - 5);
        rb->lcd_vline(209, rest_y - 5, rest_y - 1);
        rb->lcd_set_foreground(LCD_WHITE);
        rb->lcd_hline(210, 215, rest_y - 4);
        rb->lcd_drawpixel(211, rest_y);
    }
    rb->lcd_set_foreground(LCD_RGBPACK(115, 124, 119));
    xlcd_fillcircle(192, RBPREP_WAVE_TOP + 30, 10);
    rb->lcd_set_foreground(LCD_BLACK);
    xlcd_fillcircle(192, RBPREP_WAVE_TOP + 30, 4);

    /* Angled target lamp housing and beam point directly at the red strobe
       section instead of floating vertically beside the platter. */
    rb->lcd_set_foreground(LCD_RGBPACK(82, 88, 84));
    rb->lcd_drawline(166, RBPREP_WAVE_TOP + 8,
                     141, RBPREP_WAVE_TOP + 29);
    rb->lcd_drawline(166, RBPREP_WAVE_TOP + 9,
                     141, RBPREP_WAVE_TOP + 30);
    rb->lcd_set_foreground((rb->audio_status() & AUDIO_STATUS_PLAY) &&
                           !(rb->audio_status() & AUDIO_STATUS_PAUSE)
                           ? ((lamp_phase & 1) ? LCD_RGBPACK(255, 60, 45)
                                               : LCD_RGBPACK(120, 12, 8))
                           : LCD_RGBPACK(45, 9, 7));
    xlcd_fillcircle(140, RBPREP_WAVE_TOP + 30, 3);
    rb->lcd_drawline(138, RBPREP_WAVE_TOP + 32,
                     130, RBPREP_WAVE_TOP + 39);
    if (lamp_phase & 1)
        xlcd_drawcircle(140, RBPREP_WAVE_TOP + 30, 5);

    /* Compact transport in clear chassis space below-left of the platter. */
    {
        bool playing = (rb->audio_status() & AUDIO_STATUS_PLAY) &&
                       !(rb->audio_status() & AUDIO_STATUS_PAUSE);
        int button_x = 4;
        int button_y = RBPREP_WAVE_BOTTOM - 19;

        rb->lcd_set_foreground(playing ? LCD_RGBPACK(12, 49, 29)
                                      : LCD_RGBPACK(11, 14, 12));
        rb->lcd_fillrect(button_x, button_y, 38, 16);
        rb->lcd_set_foreground(RBPREP_GREEN);
        rb->lcd_drawrect(button_x, button_y, 38, 16);
        rb->lcd_fillrect(button_x, button_y, 4, 16);
        rb->lcd_set_foreground(LCD_WHITE);
        if (playing) {
            rb->lcd_fillrect(button_x + 15, button_y + 4, 3, 8);
            rb->lcd_fillrect(button_x + 22, button_y + 4, 3, 8);
        } else {
            xlcd_filltriangle(button_x + 14, button_y + 3,
                              button_x + 14, button_y + 12,
                              button_x + 26, button_y + 8);
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

    /* Metrics live in a discrete instrument box instead of floating around
       the arm and platter controls. */
    rb->lcd_set_foreground(LCD_RGBPACK(10, 14, 12));
    rb->lcd_fillrect(218, RBPREP_WAVE_TOP + 7, 77, 98);
    rb->lcd_set_foreground(LCD_RGBPACK(72, 82, 76));
    rb->lcd_drawrect(218, RBPREP_WAVE_TOP + 7, 77, 98);
    rb->lcd_hline(218, 294, RBPREP_WAVE_TOP + 24);
    text(224, RBPREP_WAVE_TOP + 11,
         turntable_headshell_style == 4 ? "PHASE" : "DECK DATA",
         RBPREP_GREEN);
    rb->snprintf(line, sizeof(line), "RPM %s>%s",
                 rpm_styles[host_rpm_index], rpm_styles[played_rpm_index]);
    text(222, RBPREP_WAVE_TOP + 29, line, LCD_WHITE);
    format_percent_delta(delta, sizeof(delta),
                         deck_pitch_x100() - PITCH_SPEED_100);
    rb->snprintf(line, sizeof(line), "PIT %s%%", delta);
    text(222, RBPREP_WAVE_TOP + 46, line, LCD_WHITE);
    format_percent_delta(delta, sizeof(delta), pitch_bend_x100);
    rb->snprintf(line, sizeof(line), "BND %s%%", delta);
    text(222, RBPREP_WAVE_TOP + 63, line, RBPREP_GREEN);
    format_percent_delta(delta, sizeof(delta),
                         tempo_x100 - PITCH_SPEED_100);
    rb->snprintf(line, sizeof(line), "TMP %s%%", delta);
    text(222, RBPREP_WAVE_TOP + 80, line, RBPREP_GREEN);
    format_percent_delta(delta, sizeof(delta),
                         deck_speed_x100() - PITCH_SPEED_100);
    rb->snprintf(line, sizeof(line), "RATE%s%%", delta);
    text(222, RBPREP_WAVE_TOP + 97, line,
         LCD_RGBPACK(155, 174, 161));
}

static void draw_waveform(void)
{
    int first;
    int span;
    int x;
    int mid = (RBPREP_SIGNAL_TOP + RBPREP_SIGNAL_BOTTOM) / 2;

    if (visualizer_mode >= 1 && visualizer_mode != 6)
        update_spectrum_levels(visualizer_mode == 1,
                               visualizer_mode == 1 ||
                               visualizer_mode == 2 ||
                               visualizer_mode == 4,
                               visualizer_mode == 7);
    if (visualizer_mode == 1) {
        draw_boombox();
        return;
    } else if (visualizer_mode == 2) {
        draw_twenty_band_eq();
        return;
    } else if (visualizer_mode == 3) {
        draw_turntable();
        return;
    } else if (visualizer_mode == 4) {
        draw_spectral_canyon();
        return;
    } else if (visualizer_mode == 5) {
        draw_stereo_orbit();
        return;
    } else if (visualizer_mode == 6) {
        draw_phrase_map();
        return;
    } else if (visualizer_mode == 7) {
        draw_harmonic_constellation();
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

static void draw_overview_waveform(void)
{
    int x;
    int mid = RBPREP_OVERVIEW_Y + RBPREP_OVERVIEW_HEIGHT / 2;
    int max_height = RBPREP_OVERVIEW_HEIGHT / 2 - 2;

    if (draw_macro_strip())
        return;

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

    rbprep_wave_close(&wave_reader);
    rbprep_grid_close(&grid_reader);
    rbprep_wave_index_close(&wave_index);
    waveform_points = 0;
    waveform_columns_valid = false;
    waveform_columns_exact = false;
    rb->memset(overview_waveform, 0, sizeof(overview_waveform));
    beat_count = 0;
    beat_search_hint = 0;
    imported_grid_cached = false;
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
    uint16_t version = read_u16(data + 6);

    return !rb->memcmp(data, "RBE1", 4) &&
           read_u16(data + 4) == RBPREP_EDIT_RECORD_SIZE &&
           (version == 1 || version == 2);
}

static void apply_edit_record(const unsigned char *data)
{
    int i;

    rating = MIN(5, data[16]);
    color_index = normalize_track_color(data[17]);
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
    if (read_u16(data + 6) >= 2) {
        rb->memcpy(selected_key, data + 192, sizeof(selected_key));
        selected_key[sizeof(selected_key) - 1] = '\0';
    }
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

static bool pack_current_edit_snapshot(unsigned char *data)
{
    int i;

    if (selected_track_id < 0)
        return false;
    rb->memset(data, 0, RBPREP_EDIT_RECORD_SIZE);
    rb->memcpy(data, "RBE1", 4);
    write_u16(data + 4, RBPREP_EDIT_RECORD_SIZE);
    write_u16(data + 6, 2);
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
    rb->strlcpy((char *)data + 152, selected_title, 40);
    rb->strlcpy((char *)data + 192, selected_key, 24);
    return true;
}

static bool save_edit_snapshot(void)
{
    unsigned char data[RBPREP_EDIT_RECORD_SIZE];
    off_t original_size;
    int fd;
    bool ok;

    if (!pack_current_edit_snapshot(data))
        return false;

    fd = rb->open(RBPREP_EDIT_JOURNAL, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
        return false;
    original_size = rb->filesize(fd);
    if (original_size >= 0 &&
        original_size % RBPREP_EDIT_RECORD_SIZE) {
        original_size -= original_size % RBPREP_EDIT_RECORD_SIZE;
        if (rb->ftruncate(fd, original_size) < 0)
            original_size = -1;
    }
    ok = original_size >= 0 &&
         rb->lseek(fd, original_size, SEEK_SET) >= 0 &&
         write_exact(fd, data, sizeof(data));
    if (!ok && original_size >= 0)
        rb->ftruncate(fd, original_size);
    if (rb->close(fd) < 0)
        ok = false;
    return ok;
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
    return true;
}

static bool repair_playlist_journal_tail(void)
{
    unsigned char buffer[128];
    int fd = rb->open(RBPREP_PLAYLIST_JOURNAL, O_RDWR);
    off_t size;
    off_t cursor;
    off_t keep = 0;
    bool ok = true;

    if (fd < 0)
        return true;
    size = rb->filesize(fd);
    if (size <= 0)
        goto done;
    if (rb->lseek(fd, size - 1, SEEK_SET) < 0 ||
        rb->read(fd, buffer, 1) != 1) {
        ok = false;
        goto done;
    }
    if (buffer[0] == '\n')
        goto done;

    /* Journal appends always include their newline in the same write. An
       unterminated tail is therefore an interrupted record, never a durable
       operation. Preserve every complete line before it. */
    cursor = size;
    while (cursor > 0) {
        int count = MIN((off_t)sizeof(buffer), cursor);
        int i;

        cursor -= count;
        if (rb->lseek(fd, cursor, SEEK_SET) < 0 ||
            rb->read(fd, buffer, count) != count) {
            ok = false;
            goto done;
        }
        for (i = count - 1; i >= 0; i--) {
            if (buffer[i] == '\n') {
                keep = cursor + i + 1;
                goto found_tail;
            }
        }
    }
found_tail:
    if (rb->ftruncate(fd, keep) < 0)
        ok = false;
done:
    if (rb->close(fd) < 0)
        ok = false;
    return ok;
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

    /* A replaced/repaired journal can legitimately be shorter than an old
       completion marker. Replaying from zero is idempotent and safer than
       treating that stale marker as corruption. */
    fd = rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);
    if (fd >= 0) {
        off_t size = rb->filesize(fd);

        if (size < 0 || *edit_offset > (uint32_t)size ||
            *edit_offset % RBPREP_EDIT_RECORD_SIZE)
            *edit_offset = 0;
        rb->close(fd);
    } else {
        *edit_offset = 0;
    }
    fd = rb->open(RBPREP_PLAYLIST_JOURNAL, O_RDONLY);
    if (fd >= 0) {
        off_t size = rb->filesize(fd);
        unsigned char newline;

        if (size < 0 || *playlist_offset > (uint32_t)size) {
            *playlist_offset = 0;
        } else if (*playlist_offset > 0 &&
                   (rb->lseek(fd, *playlist_offset - 1, SEEK_SET) < 0 ||
                    rb->read(fd, &newline, 1) != 1 || newline != '\n')) {
            *playlist_offset = 0;
        }
        rb->close(fd);
    } else {
        *playlist_offset = 0;
    }
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

static bool parse_playlist_journal_line(
    char *line, struct rbprep_pending_playlist *entry)
{
    char *field[6];
    int count = 1;
    char *cursor;

    rb->memset(entry, 0, sizeof(*entry));
    field[0] = line;
    for (cursor = line; *cursor && count < (int)ARRAYLEN(field); cursor++) {
        if (*cursor == '\t') {
            *cursor = '\0';
            field[count++] = cursor + 1;
        }
    }
    if (count == 3) {
        /* RBI1/RBI2 compatibility: track<TAB>playlist<TAB>name. */
        entry->operation = PLAYLIST_OP_ADD;
        entry->kind = 1;
        entry->track_id = rb->strtoul(field[0], NULL, 10);
        entry->playlist_id = rb->strtoul(field[1], NULL, 10);
        rb->strlcpy(entry->name, field[2], sizeof(entry->name));
        return entry->track_id && entry->playlist_id;
    }
    if (count != 6 || field[0][1] != '\0' ||
        !rb->strchr("ACRMD", field[0][0]))
        return false;
    entry->operation = field[0][0];
    entry->track_id = rb->strtoul(field[1], NULL, 10);
    entry->playlist_id = rb->strtoul(field[2], NULL, 10);
    entry->parent_id = rb->strtoul(field[3], NULL, 10);
    entry->kind = MIN(2, rb->strtoul(field[4], NULL, 10));
    rb->strlcpy(entry->name, field[5], sizeof(entry->name));
    return entry->playlist_id &&
           (entry->operation != PLAYLIST_OP_ADD || entry->track_id);
}

static bool normalize_edit_journal_records(void)
{
    unsigned char record[RBPREP_EDIT_RECORD_SIZE];
    uint32_t edit_offset;
    uint32_t playlist_offset;
    uint32_t new_edit_offset = 0;
    off_t position = 0;
    ssize_t got = 0;
    int input;
    int output;
    bool changed = false;
    bool ok = true;

    read_burn_offsets(&edit_offset, &playlist_offset);
    input = rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);
    if (input < 0)
        return true;
    rb->remove(RBPREP_EDIT_JOURNAL_REPAIR);
    output = rb->open(RBPREP_EDIT_JOURNAL_REPAIR,
                      O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output < 0) {
        rb->close(input);
        return false;
    }
    while ((got = rb->read(input, record, sizeof(record))) ==
           (ssize_t)sizeof(record)) {
        if (valid_edit_record(record)) {
            if (position < (off_t)edit_offset)
                new_edit_offset += sizeof(record);
            if (!write_exact(output, record, sizeof(record))) {
                ok = false;
                break;
            }
        } else {
            /* A full-size torn record cannot be interpreted safely. Keep the
               original journal as a quarantine copy, but do not let one dead
               slot permanently block every later valid edit. */
            changed = true;
        }
        position += sizeof(record);
    }
    if (got < 0)
        ok = false;
    else if (got != 0)
        changed = true;
    if (rb->close(input) < 0)
        ok = false;
    if (rb->close(output) < 0)
        ok = false;
    if (!ok) {
        rb->remove(RBPREP_EDIT_JOURNAL_REPAIR);
        return false;
    }
    if (!changed) {
        rb->remove(RBPREP_EDIT_JOURNAL_REPAIR);
        return true;
    }

    rb->remove(RBPREP_EDIT_JOURNAL_BAD);
    if (rb->rename(RBPREP_EDIT_JOURNAL,
                   RBPREP_EDIT_JOURNAL_BAD) < 0) {
        rb->remove(RBPREP_EDIT_JOURNAL_REPAIR);
        return false;
    }
    if (rb->rename(RBPREP_EDIT_JOURNAL_REPAIR,
                   RBPREP_EDIT_JOURNAL) < 0) {
        rb->rename(RBPREP_EDIT_JOURNAL_BAD,
                   RBPREP_EDIT_JOURNAL);
        return false;
    }
    if (!write_burn_offsets(new_edit_offset, playlist_offset)) {
        rb->remove(RBPREP_EDIT_JOURNAL);
        rb->rename(RBPREP_EDIT_JOURNAL_BAD,
                   RBPREP_EDIT_JOURNAL);
        return false;
    }
    return true;
}

static bool normalize_playlist_journal_records(void)
{
    char line[192];
    char parsed[192];
    uint32_t edit_offset;
    uint32_t playlist_offset;
    uint32_t new_playlist_offset = 0;
    off_t position = 0;
    off_t line_start = 0;
    int length = 0;
    int input;
    int output;
    int got = 0;
    bool overflow = false;
    bool changed = false;
    bool ok = true;

    read_burn_offsets(&edit_offset, &playlist_offset);
    input = rb->open(RBPREP_PLAYLIST_JOURNAL, O_RDONLY);
    if (input < 0)
        return true;
    rb->remove(RBPREP_PLAYLIST_JOURNAL_REPAIR);
    output = rb->open(RBPREP_PLAYLIST_JOURNAL_REPAIR,
                      O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output < 0) {
        rb->close(input);
        return false;
    }
    while ((got = rb->read(input, parsed, 1)) == 1) {
        unsigned char value = parsed[0];

        position++;
        if (value == '\n') {
            struct rbprep_pending_playlist entry;
            bool valid = !overflow;

            line[length] = '\0';
            if (valid && length > 0) {
                rb->strlcpy(parsed, line, sizeof(parsed));
                valid = parse_playlist_journal_line(parsed, &entry);
            }
            if (valid) {
                if (line_start < (off_t)playlist_offset)
                    new_playlist_offset += length + 1;
                if ((length > 0 && !write_exact(output, line, length)) ||
                    !write_exact(output, "\n", 1)) {
                    ok = false;
                    break;
                }
            } else {
                changed = true;
            }
            length = 0;
            overflow = false;
            line_start = position;
        } else if (value == '\r') {
            /* Accept a host-created CRLF journal without treating the CR as
               part of a playlist name. */
            changed = true;
        } else if (length + 1 < (int)sizeof(line)) {
            line[length++] = value;
        } else {
            overflow = true;
        }
    }
    if (got < 0)
        ok = false;
    if (length > 0 || overflow)
        changed = true;
    if (rb->close(input) < 0)
        ok = false;
    if (rb->close(output) < 0)
        ok = false;
    if (!ok) {
        rb->remove(RBPREP_PLAYLIST_JOURNAL_REPAIR);
        return false;
    }
    if (!changed) {
        rb->remove(RBPREP_PLAYLIST_JOURNAL_REPAIR);
        return true;
    }

    rb->remove(RBPREP_PLAYLIST_JOURNAL_BAD);
    if (rb->rename(RBPREP_PLAYLIST_JOURNAL,
                   RBPREP_PLAYLIST_JOURNAL_BAD) < 0) {
        rb->remove(RBPREP_PLAYLIST_JOURNAL_REPAIR);
        return false;
    }
    if (rb->rename(RBPREP_PLAYLIST_JOURNAL_REPAIR,
                   RBPREP_PLAYLIST_JOURNAL) < 0) {
        rb->rename(RBPREP_PLAYLIST_JOURNAL_BAD,
                   RBPREP_PLAYLIST_JOURNAL);
        return false;
    }
    if (!write_burn_offsets(edit_offset, new_playlist_offset)) {
        rb->remove(RBPREP_PLAYLIST_JOURNAL);
        rb->rename(RBPREP_PLAYLIST_JOURNAL_BAD,
                   RBPREP_PLAYLIST_JOURNAL);
        return false;
    }
    return true;
}

static bool repair_pending_journals(void)
{
    return normalize_edit_journal_records() &&
           normalize_playlist_journal_records();
}

static void remove_pending_playlist_at(int slot)
{
    for (; slot + 1 < pending_playlist_count; slot++)
        pending_playlists[slot] = pending_playlists[slot + 1];
    pending_playlist_count--;
}

static void merge_pending_playlist(
    const struct rbprep_pending_playlist *incoming)
{
    int i;

    if (incoming->operation == PLAYLIST_OP_DELETE) {
        bool cancels_create = false;
        for (i = pending_playlist_count - 1; i >= 0; i--) {
            if (pending_playlists[i].playlist_id != incoming->playlist_id)
                continue;
            if (pending_playlists[i].operation == PLAYLIST_OP_CREATE)
                cancels_create = true;
            remove_pending_playlist_at(i);
        }
        if (cancels_create)
            return;
    } else if (incoming->operation == PLAYLIST_OP_RENAME ||
               incoming->operation == PLAYLIST_OP_MOVE) {
        for (i = 0; i < pending_playlist_count; i++) {
            struct rbprep_pending_playlist *current = &pending_playlists[i];
            if (current->playlist_id != incoming->playlist_id)
                continue;
            if (current->operation == PLAYLIST_OP_DELETE)
                return;
            if (current->operation == PLAYLIST_OP_CREATE) {
                if (incoming->operation == PLAYLIST_OP_RENAME)
                    rb->strlcpy(current->name, incoming->name,
                                sizeof(current->name));
                else
                    current->parent_id = incoming->parent_id;
                return;
            }
            if (current->operation == incoming->operation) {
                *current = *incoming;
                return;
            }
        }
    } else if (incoming->operation == PLAYLIST_OP_ADD) {
        for (i = 0; i < pending_playlist_count; i++) {
            struct rbprep_pending_playlist *current = &pending_playlists[i];
            if (current->playlist_id == incoming->playlist_id &&
                (current->operation == PLAYLIST_OP_DELETE ||
                 (current->operation == PLAYLIST_OP_ADD &&
                  current->track_id == incoming->track_id)))
                return;
        }
    } else if (incoming->operation == PLAYLIST_OP_CREATE) {
        for (i = 0; i < pending_playlist_count; i++) {
            if (pending_playlists[i].playlist_id == incoming->playlist_id) {
                pending_playlists[i] = *incoming;
                return;
            }
        }
    }
    if (pending_playlist_count < RBPREP_PENDING_MAX)
        pending_playlists[pending_playlist_count++] = *incoming;
    else
        pending_summary_overflow = true;
}

static void refresh_pending_summary_from_offsets(uint32_t edit_offset,
                                                 uint32_t playlist_offset,
                                                 uint32_t strict_edit_offset,
                                                 uint32_t strict_playlist_offset)
{
    unsigned char data[RBPREP_EDIT_RECORD_SIZE];
    char line[160];
    int fd;
    int i;
    int length;
    bool line_overflow;
    off_t journal_size;
    uint32_t record_offset;
    uint32_t playlist_cursor;
    uint32_t playlist_line_offset;

    pending_snapshot_count = 0;
    pending_playlist_count = 0;
    pending_summary_overflow = false;
    pending_journal_invalid = false;
    pending_visible_count = 0;
    fd = rb->open(RBPREP_EDIT_JOURNAL, O_RDONLY);
    if (fd >= 0) {
        journal_size = rb->filesize(fd);
        if (journal_size < 0 || edit_offset > (uint32_t)journal_size ||
            edit_offset % RBPREP_EDIT_RECORD_SIZE) {
            pending_journal_invalid = true;
            edit_offset = 0;
        }
        if (journal_size >= 0 &&
            ((uint32_t)journal_size - edit_offset) %
                RBPREP_EDIT_RECORD_SIZE &&
            (uint32_t)journal_size > strict_edit_offset)
            pending_journal_invalid = true;
        if (edit_offset > 0)
            rb->lseek(fd, edit_offset, SEEK_SET);
        record_offset = edit_offset;
        while (rb->read(fd, data, sizeof(data)) == sizeof(data)) {
            struct rbprep_pending_entry *entry;
            int slot = -1;
            uint32_t current_record_offset = record_offset;

            record_offset += sizeof(data);

            if (!valid_edit_record(data)) {
                if (current_record_offset >= strict_edit_offset)
                    pending_journal_invalid = true;
                continue;
            }
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
                pending_summary_overflow = true;
                continue;
            }
            entry = &pending_entries[slot];
            entry->track_id = read_u32(data + 8);
            entry->saved_tick = read_u32(data + 12);
            entry->rating = MIN(5, data[16]);
            entry->color = normalize_track_color(data[17]);
            entry->bpm_x100 = read_u32(data + 24);
            rb->memset(entry->title, 0, sizeof(entry->title));
            rb->memcpy(entry->title, data + 152,
                       read_u16(data + 6) >= 2 ? 40 : 64);
            entry->title[sizeof(entry->title) - 1] = '\0';
        }
        rb->close(fd);
    }
    fd = rb->open(RBPREP_PLAYLIST_JOURNAL, O_RDONLY);
    if (fd >= 0) {
        journal_size = rb->filesize(fd);
        if (journal_size >= 0 &&
            playlist_offset <= (uint32_t)journal_size) {
            rb->lseek(fd, playlist_offset, SEEK_SET);
        } else {
            pending_journal_invalid = true;
            playlist_offset = 0;
        }
        length = 0;
        line_overflow = false;
        playlist_cursor = playlist_offset;
        playlist_line_offset = playlist_offset;
        while (true) {
            unsigned char value;
            int got = rb->read(fd, &value, 1);
            if (got != 1) {
                if (length > 0 && !line_overflow) {
                    struct rbprep_pending_playlist entry;

                    line[length] = '\0';
                    if (parse_playlist_journal_line(line, &entry))
                        merge_pending_playlist(&entry);
                    else if (playlist_line_offset >=
                             strict_playlist_offset)
                        pending_journal_invalid = true;
                } else if (line_overflow && playlist_line_offset >=
                           strict_playlist_offset) {
                    pending_journal_invalid = true;
                }
                break;
            }
            playlist_cursor++;
            if (value == '\n') {
                struct rbprep_pending_playlist entry;

                if (line_overflow) {
                    if (playlist_line_offset >= strict_playlist_offset)
                        pending_journal_invalid = true;
                } else if (length > 0) {
                    line[length] = '\0';
                    if (parse_playlist_journal_line(line, &entry))
                        merge_pending_playlist(&entry);
                    else if (playlist_line_offset >=
                             strict_playlist_offset)
                        pending_journal_invalid = true;
                }
                length = 0;
                line_overflow = false;
                playlist_line_offset = playlist_cursor;
            } else if (value != '\r' &&
                       length + 1 < (int)sizeof(line)) {
                line[length++] = value;
            } else if (value != '\r') {
                line_overflow = true;
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

static void refresh_pending_summary(void)
{
    uint32_t edit_offset;
    uint32_t playlist_offset;
    bool journals_repaired = repair_pending_journals();

    read_burn_offsets(&edit_offset, &playlist_offset);
    refresh_pending_summary_from_offsets(edit_offset, playlist_offset,
                                         edit_offset, playlist_offset);
    if (!journals_repaired)
        pending_journal_invalid = true;
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

static bool delete_pending_playlist(unsigned char operation,
                                    uint32_t track_id,
                                    uint32_t playlist_id)
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
            struct rbprep_pending_playlist entry;
            bool remove = false;

            line[length] = '\0';
            rb->strlcpy(parse, line, sizeof(parse));
            if (parse_playlist_journal_line(parse, &entry))
                remove = entry.operation == operation &&
                         entry.playlist_id == playlist_id &&
                         (operation != PLAYLIST_OP_ADD ||
                          entry.track_id == track_id);
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

/* Keep the validated, rollback-safe DeviceSQL/RBI transaction out of the UI
   core while sharing its already-coalesced pending state. */
#include "rbprep_burn.h"

static int find_node_index_by_source_id(uint32_t source_id)
{
    struct rbprep_node_record node;
    int index;

    for (index = 0; (uint32_t)index < library_node_count; index++) {
        if (read_node_record(index, &node) && node.source_id == source_id)
            return index;
    }
    return -1;
}

static int find_track_row_in_playlist(int node_index, int track_index)
{
    struct rbprep_node_record node;
    unsigned char data[4];
    uint32_t row;

    if (track_index < 0 || !read_node_record(node_index, &node))
        return -1;
    for (row = 0; row < node.member_count; row++) {
        if (!read_index_at(library_member_offset +
                           (node.first_member + row) * 4,
                           data, sizeof(data)))
            break;
        if ((int)read_u32(data) == track_index)
            return row;
    }
    return -1;
}

static void refresh_active_playlist_context(uint32_t active_source_id)
{
    struct rbprep_node_record active;
    int linked_macro;

    if (selected_track_id >= 0)
        selected_track_index = find_track_index_by_id(selected_track_id);
    if (!active_source_id)
        return;
    active_playlist_node = find_node_index_by_source_id(active_source_id);
    if (active_playlist_node >= 0) {
        if (read_node_record(active_playlist_node, &active))
            track_row_count = active.member_count;
        playing_track_row = find_track_row_in_playlist(
                                active_playlist_node, selected_track_index);
        if (playing_track_row >= 0)
            track_selection = playing_track_row;
        track_top = MAX(0, MIN(track_selection,
                        MAX(0, track_row_count - RBPREP_LIST_ROWS)));
        linked_macro = macro_link_for(active_source_id);
        if (linked_macro >= 0)
            macro_active = linked_macro;
        macro_position = 0;
    } else {
        playing_track_row = -1;
        collection_shuffle_active = false;
        track_row_count = library_track_count;
    }
}

static bool burn_playlist_changes_now(void)
{
    struct rbprep_node_record active;
    uint32_t active_source_id = 0;
    bool saved_playlist_playback = playlist_playback;
    bool success;

    if (active_playlist_node >= 0 &&
        read_node_record(active_playlist_node, &active))
        active_source_id = active.source_id;
    success = rbprep_burn_playlists();
    refresh_pending_summary();
    playlist_playback = saved_playlist_playback;
    if (!success)
        return false;

    refresh_active_playlist_context(active_source_id);
    force_full_redraw = true;
    return true;
}

static bool auto_burn_pending_changes(void)
{
    if (!auto_burn)
        return true;
    /* Auto Burn records intent immediately, but the transaction itself runs
       only at the next real track-load boundary so playback is never stopped
       halfway through a song for analysis workspace. */
    if (!flush_deferred_edit()) {
        rb->splash(HZ * 2, "Auto Burn: journal write failed");
        restore_black_canvas();
        return false;
    }
    refresh_pending_summary();
    if (pending_snapshot_count == 0 && pending_playlist_count == 0 &&
        !pending_summary_overflow && !pending_journal_invalid)
        return true;
    burn_request = BURN_REQUEST_ALL;
    return true;
}

static bool service_deferred_burn(void)
{
    struct rbprep_node_record active;
    uint32_t active_source_id = 0;
    bool success;
    bool saved_playlist_playback = playlist_playback;
    int track_burns;

    if (burn_request == BURN_REQUEST_NONE)
        return true;
    if (active_playlist_node >= 0 &&
        read_node_record(active_playlist_node, &active))
        active_source_id = active.source_id;
    refresh_pending_summary();
    track_burns = pending_snapshot_count;
    success = rbprep_burn_all();
    playlist_playback = saved_playlist_playback;
    refresh_pending_summary();
    if (success) {
        uptime_tracks_burned += track_burns;
        burn_request = BURN_REQUEST_NONE;
        refresh_active_playlist_context(active_source_id);
        rb->splash(HZ, "Burn all complete");
    } else {
        rb->splash(HZ * 2, "Automatic burn remains pending");
    }
    restore_black_canvas();
    return success;
}

static bool burn_loaded_track_now(void)
{
    int status;
    int resume_index = 0;
    int resume_position;
    bool had_audio;
    bool was_paused;
    bool saved_playlist_playback;
    bool capture_was_active;
    bool success;

    activity_ticker_ping(80);
    if (selected_track_id < 0)
        return false;
    if (!flush_deferred_edit()) {
        rb->splash(HZ * 2, "Track burn: journal write failed");
        restore_black_canvas();
        return false;
    }

    status = rb->audio_status();
    had_audio = !!(status & AUDIO_STATUS_PLAY);
    was_paused = !!(status & AUDIO_STATUS_PAUSE);
    if (had_audio)
        rb->playlist_get_resume_info(&resume_index);
    resume_position = clamp_playhead(playhead);
    saved_playlist_playback = playlist_playback;
    capture_was_active = spectrum_capture_active;
    stop_spectrum_capture();

    activity_ticker_ping(350);
    success = rbprep_burn_song((uint32_t)selected_track_id);
    activity_ticker_ping(820);
    refresh_pending_summary();
    if (success && pending_snapshot_count == 0 &&
        pending_playlist_count == 0 && !pending_summary_overflow &&
        !pending_journal_invalid)
        burn_request = BURN_REQUEST_NONE;

    /* A track transaction borrows Rockbox's audio buffer. Reconstitute the
       one-track playback context at the exact deck position afterwards. */
    playlist_playback = saved_playlist_playback;
    if (had_audio && !(rb->audio_status() & AUDIO_STATUS_PLAY)) {
        rb->playlist_start(resume_index, resume_position, 0);
        if (was_paused)
            rb->audio_pause();
    }
    if (had_audio)
        audio_was_running = true;
    playhead = resume_position;
    reset_play_clock(playhead, *rb->current_tick);
    if (capture_was_active && !display_locked)
        start_spectrum_capture();
    overview_dirty = true;
    force_full_redraw = true;
    if (success) {
        uptime_tracks_burned++;
        rb->splash(HZ, "Track burn complete");
    }
    activity_ticker_ping(1000);
    restore_black_canvas();
    return success;
}

static bool burn_all_now(void)
{
    struct rbprep_node_record active;
    uint32_t active_source_id = 0;
    int status;
    int resume_index = 0;
    int resume_position = clamp_playhead(playhead);
    int track_burns;
    bool had_audio;
    bool was_paused;
    bool saved_playlist_playback = playlist_playback;
    bool capture_was_active = spectrum_capture_active;
    bool success;

    activity_ticker_ping(80);
    if (!flush_deferred_edit()) {
        rb->splash(HZ * 2, "Instant burn: journal write failed");
        restore_black_canvas();
        return false;
    }
    refresh_pending_summary();
    if (pending_snapshot_count == 0 && pending_playlist_count == 0) {
        rb->splash(HZ, "Nothing to burn");
        restore_black_canvas();
        return true;
    }
    track_burns = pending_snapshot_count;
    if (active_playlist_node >= 0 &&
        read_node_record(active_playlist_node, &active))
        active_source_id = active.source_id;
    status = rb->audio_status();
    had_audio = !!(status & AUDIO_STATUS_PLAY);
    was_paused = !!(status & AUDIO_STATUS_PAUSE);
    if (had_audio)
        rb->playlist_get_resume_info(&resume_index);
    stop_spectrum_capture();

    activity_ticker_ping(350);
    success = rbprep_burn_all();
    activity_ticker_ping(820);
    refresh_pending_summary();
    playlist_playback = saved_playlist_playback;
    if (success) {
        uptime_tracks_burned += track_burns;
        burn_request = BURN_REQUEST_NONE;
        refresh_active_playlist_context(active_source_id);
    }

    /* A user-triggered full burn is immediate. If analysis work borrowed the
       audio buffer, rebuild the current one-track transport in place. */
    if (had_audio && !(rb->audio_status() & AUDIO_STATUS_PLAY)) {
        rb->playlist_start(resume_index, resume_position, 0);
        if (was_paused)
            rb->audio_pause();
    }
    if (had_audio)
        audio_was_running = true;
    playhead = resume_position;
    reset_play_clock(playhead, *rb->current_tick);
    if (capture_was_active && !display_locked)
        start_spectrum_capture();
    overview_dirty = true;
    force_full_redraw = true;
    if (success)
        rb->splash(HZ, "Instant burn complete");
    activity_ticker_ping(1000);
    restore_black_canvas();
    return success;
}

static void reset_play_stat(int track_id)
{
    play_stat_track_id = track_id;
    play_stat_last_tick = *rb->current_tick;
    play_stat_ticks = 0;
    play_stat_counted = false;
}

static void service_play_statistics(void)
{
    long now = *rb->current_tick;
    long elapsed;
    int status;

    if (selected_track_id < 0)
        return;
    if (play_stat_track_id != selected_track_id)
        reset_play_stat(selected_track_id);
    elapsed = now - play_stat_last_tick;
    play_stat_last_tick = now;
    status = rb->audio_status();
    if (!(status & AUDIO_STATUS_PLAY) || (status & AUDIO_STATUS_PAUSE) ||
        seek_state != SEEK_IDLE || elapsed <= 0 || elapsed > HZ * 2)
        return;
    play_stat_ticks += elapsed;
    if (!play_stat_counted && play_stat_ticks >= HZ * 60) {
        play_stat_counted = true;
        if (total_plays < INT_MAX)
            total_plays++;
        /* This used to rewrite rbprep.cfg synchronously at exactly 60 s.
           iFlash erase/program latency then froze the render loop for several
           seconds. Persist at pause, track change, USB, or plugin exit. */
        mark_rbprep_config_dirty();
    }
}

static int waveform_index_progress(void)
{
    switch (wave_index.stage) {
    case RBPREP_WAVE_INDEX_SCAN:
        return 20 + (int)((uint64_t)wave_index.scan_position * 40 /
                          MAX(1u, wave_index.source_points));
    case RBPREP_WAVE_INDEX_OPEN_WRITE:
        return 60;
    case RBPREP_WAVE_INDEX_WRITE:
        return 60 + (int)((uint64_t)wave_index.transfer_position * 18 /
                          MAX((size_t)1, wave_index.payload_size));
    case RBPREP_WAVE_INDEX_OPEN_VERIFY:
        return 78;
    case RBPREP_WAVE_INDEX_VERIFY:
        return 78 + (int)((uint64_t)wave_index.transfer_position * 20 /
                          MAX((size_t)1, wave_index.payload_size));
    case RBPREP_WAVE_INDEX_PUBLISH:
        return 99;
    case RBPREP_WAVE_INDEX_IDLE:
        return wave_index.valid ? 100 : 0;
    default:
        return 0;
    }
}

static void draw_waveform_preparation(int progress, const char *stage)
{
    int width = LCD_WIDTH - 56;
    int fill = width * MAX(0, MIN(100, progress)) / 100;
    char percent[16];

    activity_ticker_ping(progress * 10);
    rb->lcd_set_background(LCD_BLACK);
    rb->lcd_clear_display();
    draw_status_bar();
    centered_text(0, LCD_WIDTH, 72, "PREPARING WAVEFORM", LCD_WHITE);
    centered_text(0, LCD_WIDTH, 91, stage, LCD_RGBPACK(145, 165, 151));
    rb->lcd_set_foreground(LCD_RGBPACK(22, 35, 27));
    rb->lcd_fillrect(28, 118, width, 12);
    rb->lcd_set_foreground(RBPREP_GREEN);
    if (fill > 0)
        rb->lcd_fillrect(28, 118, fill, 12);
    rb->lcd_set_foreground(LCD_RGBPACK(95, 120, 103));
    rb->lcd_drawrect(27, 117, width + 2, 14);
    rb->snprintf(percent, sizeof(percent), "%d%%", progress);
    centered_text(0, LCD_WIDTH, 143, percent, LCD_WHITE);
    centered_text(0, LCD_WIDTH, 176,
                  "AUDIO WAITS UNTIL THE DECK IS RESIDENT",
                  LCD_RGBPACK(95, 120, 103));
    rb->lcd_update();
}

static bool finish_waveform_index_preparation(void)
{
    int shown_progress = -1;
    long draw_deadline = *rb->current_tick;

    while (wave_index.stage != RBPREP_WAVE_INDEX_IDLE &&
           wave_index.stage != RBPREP_WAVE_INDEX_FAILED) {
        enum rbprep_wave_index_service_result result =
            rbprep_wave_index_service_step(&wave_index);
        int progress = waveform_index_progress();
        long now = *rb->current_tick;

        if (progress != shown_progress &&
            !TIME_BEFORE(now, draw_deadline)) {
            const char *stage = wave_index.stage == RBPREP_WAVE_INDEX_SCAN
                              ? "BUILDING RESIDENT PEAKS"
                              : wave_index.stage == RBPREP_WAVE_INDEX_WRITE ||
                                wave_index.stage == RBPREP_WAVE_INDEX_OPEN_WRITE
                              ? "SAVING DEVICE INDEX"
                              : "VERIFYING DEVICE INDEX";

            draw_waveform_preparation(progress, stage);
            shown_progress = progress;
            draw_deadline = now + MAX(1, HZ / 10);
        }
        if (result == RBPREP_WAVE_INDEX_SERVICE_FAILED)
            break;
        rb->yield();
    }
    return wave_index.valid;
}

static bool load_waveform(int track_id)
{
    int fd;
    int i;
    int cue_count;
    uint32_t declared_points;
    int declared_beats;
    off_t metadata_offset;
    off_t beat_data_offset;
    off_t required_size;
    off_t source_file_size;
    uint32_t source_size;
    uint32_t source_fingerprint;
    char filename[MAX_PATH];
    char index_filename[MAX_PATH];
    unsigned char header[40];

    clear_analysis();
    rb->snprintf(filename, sizeof(filename), "%s/%06d.rbw",
                 RBPREP_TRACK_DIR, track_id);
    fd = rb->open(filename, O_RDONLY);
    if (fd < 0)
        return false;
    if (!read_exact(fd, header, sizeof(header)) ||
        rb->memcmp(header, "RBW3", 4) || read_u16(header + 4) != 40) {
        rb->close(fd);
        return false;
    }

    declared_points = read_u32(header + 8);
    cue_count = read_u16(header + 12);
    declared_beats = read_u16(header + 14);
    track_length = MAX(1, (int)read_u32(header + 16));
    grid_bpm_x100 = read_u16(header + 24);
    grid_source_bpm_x100 = grid_bpm_x100;
    grid_phase_ms = read_u32(header + 28);
    rating = MIN(5, header[32]);
    color_index = normalize_track_color(header[33]);

    metadata_offset = sizeof(header) +
                      (off_t)declared_points * RBPREP_WAVE_SAMPLE_BYTES;
    beat_data_offset = metadata_offset + (off_t)cue_count * 8;
    required_size = beat_data_offset +
                    (off_t)declared_beats * 8;
    source_file_size = rb->filesize(fd);
    if (!declared_points || declared_points > RBPREP_POINTS ||
        declared_beats > RBPREP_BEATS ||
        metadata_offset < (off_t)sizeof(header) ||
        source_file_size < required_size ||
        (uint64_t)source_file_size > 0xffffffffu) {
        clear_analysis();
        rb->close(fd);
        return false;
    }
    source_size = source_file_size;

    waveform_points = declared_points;
    if (!rbprep_wave_open(&wave_reader, filename, waveform_points,
                          sizeof(header))) {
        clear_analysis();
        rb->close(fd);
        return false;
    }
    draw_waveform_preparation(8, "LOADING RGB WAVEFORM");
    /* Classic has enough plugin RAM for the complete source waveform.  Video
       may decline this preload and will use its resident peak index instead. */
    if (!rbprep_wave_cache_all(&wave_reader) &&
        capabilities.device_class == RBPREP_DEVICE_CLASSIC) {
        rb->close(fd);
        clear_analysis();
        return false;
    }
    draw_waveform_preparation(20, "LOADING DEVICE INDEX");

    source_fingerprint = rbprep_wave_index_fingerprint(
        header, sizeof(header), source_size);
    rb->snprintf(index_filename, sizeof(index_filename),
                 "/.rockbox/rbprep/wave-index/%06d.rbx", track_id);
    if (!rbprep_wave_index_open(&wave_index, index_filename, track_id,
                                waveform_points, source_size,
                                source_fingerprint, overview_waveform) &&
        !rbprep_wave_index_start(&wave_index, index_filename, track_id,
                                 waveform_points, source_size,
                                 source_fingerprint, filename, sizeof(header),
                                 overview_waveform)) {
        rb->close(fd);
        clear_analysis();
        return false;
    }
    if (!finish_waveform_index_preparation()) {
        rb->close(fd);
        clear_analysis();
        return false;
    }
    if (rb->lseek(fd, metadata_offset, SEEK_SET) < 0) {
        clear_analysis();
        rb->close(fd);
        return false;
    }

    for (i = 0; i < cue_count; i++) {
        unsigned char cue[8];
        int slot;
        if (!read_exact(fd, cue, sizeof(cue))) {
            rb->close(fd);
            clear_analysis();
            return false;
        }
        slot = cue[5];
        if (slot > 0 && slot <= 16) {
            hotcues[slot - 1] = read_u32(cue);
            hotcue_colors[slot - 1] = cue[4] & 7;
        }
    }
    beat_count = declared_beats;
    if (beat_count > 0 &&
        !rbprep_grid_open(&grid_reader, filename, beat_count,
                          beat_data_offset)) {
        rb->close(fd);
        clear_analysis();
        return false;
    }
    if (beat_count > 0) {
        draw_waveform_preparation(98, "LOADING BEAT GRID");
        rbprep_grid_cache_all(&grid_reader);
        imported_grid_cached = rbprep_grid_fully_resident(&grid_reader);
        /* Never replace a valid imported/flexible Rekordbox grid with a
           synthetic constant-BPM grid merely because this target could not
           cache it.  Reject the deck load while the previous track is still
           recoverable instead of displaying musically incorrect markers. */
        if (!imported_grid_cached) {
            rb->close(fd);
            clear_analysis();
            return false;
        }
    }
    beat_search_hint = 0;
    rb->close(fd);
    draw_waveform_preparation(100, "DECK READY");
    overview_dirty = true;
    return true;
}

static void open_track_browser(int playlist_node)
{
    struct rbprep_node_record node;
    int linked_macro;

    active_playlist_node = playlist_node;
    track_selection = 0;
    track_top = 0;
    if (playlist_node < 0) {
        track_row_count = (search_active || collection_shuffle_active)
                        ? search_result_count : (int)library_track_count;
    } else if (read_node_record(playlist_node, &node)) {
        linked_macro = macro_link_for(node.source_id);
        if (linked_macro >= 0)
            macro_active = linked_macro;
        macro_position = 0;
        track_row_count = node.member_count;
    } else {
        track_row_count = 0;
    }
    mode = MODE_TRACKS;
    force_full_redraw = true;
}

/* Keep the asynchronous pre/finish seek protocol balanced.  In particular,
   preview debounce can leave Rockbox in ff/rw mode with PCM held until a later
   service tick posts the finishing seek. */
static void prepare_transport_seek(void)
{
    if (seek_prepared)
        return;
    rb->audio_pre_ff_rewind();
    seek_prepared = true;
}

static void finish_transport_seek(int target)
{
    rb->audio_ff_rewind(clamp_playhead(target));
    seek_prepared = false;
}

/* A load transition is not an editor seek.  Capture the user's logical play
   state separately from success, finish any outstanding pre-ff request, and
   wait until both playback state and the actual PCM mixer channel agree that
   audio is paused.  Synchronous analysis must not begin on a timeout. */
static bool quiesce_audio_for_track_load(bool *resume_after_prepare)
{
    int status = rb->audio_status();
    bool was_running = ((status & AUDIO_STATUS_PLAY) &&
                        !(status & AUDIO_STATUS_PAUSE)) ||
                       (seek_state != SEEK_IDLE && !seek_was_paused);
    long deadline;
    bool settled = false;

    if (resume_after_prepare)
        *resume_after_prepare = was_running;

    if (seek_state == SEEK_IDLE && was_running)
        update_play_clock();
    if (seek_state != SEEK_IDLE) {
        prepare_transport_seek();
        finish_transport_seek(playhead);
    }

    /* audio_pause() is also our synchronous queue barrier: a finishing seek
       posted above is handled before it returns.  Rockbox may then complete a
       configured fade-out asynchronously, so mixer state is checked below. */
    rb->audio_pause();
    seek_state = SEEK_IDLE;
    seek_preview = false;
    cue_audition_active = false;
    cue_audition_latched = false;
    deadline = *rb->current_tick + MAX(1, HZ);
    do {
        status = rb->audio_status();
        if ((!(status & AUDIO_STATUS_PLAY) ||
             (status & AUDIO_STATUS_PAUSE)) &&
            rb->mixer_channel_status(PCM_MIXER_CHAN_PLAYBACK) !=
                CHANNEL_PLAYING) {
            settled = true;
            break;
        }
        rb->yield();
    } while (TIME_BEFORE(*rb->current_tick, deadline));
    if (!settled && was_running && (status & AUDIO_STATUS_PAUSE))
        rb->audio_resume();
    reset_play_clock(playhead, *rb->current_tick);
    return settled;
}

static void restore_failed_track_load(int old_track_id,
                                      const char *old_path,
                                      int old_position,
                                      int old_resume_index,
                                      bool old_had_audio,
                                      bool old_was_paused,
                                      bool playlist_replaced,
                                      bool analysis_changed,
                                      bool capture_was_active)
{
    bool transport_restored = !old_had_audio;
    int status;

    if (analysis_changed) {
        if (old_track_id >= 0) {
            if (load_waveform(old_track_id))
                load_latest_edit(old_track_id);
        } else {
            clear_analysis();
        }
    }

    status = rb->audio_status();
    if (old_had_audio && playlist_replaced && old_path && old_path[0]) {
        rb->audio_stop();
        rb->yield();
        if (rb->playlist_create(NULL, NULL) >= 0 &&
            rb->playlist_insert_track(NULL, old_path, PLAYLIST_INSERT_LAST,
                                      false, true) >= 0) {
            rb->playlist_start(0, old_position, 0);
            transport_restored = true;
        }
    } else if (old_had_audio && !(status & AUDIO_STATUS_PLAY)) {
        rb->playlist_start(old_resume_index, old_position, 0);
        transport_restored = true;
    } else if (old_had_audio) {
        transport_restored = true;
    }

    if (transport_restored && old_had_audio) {
        status = rb->audio_status();
        if (old_was_paused) {
            if ((status & AUDIO_STATUS_PLAY) &&
                !(status & AUDIO_STATUS_PAUSE))
                rb->audio_pause();
        } else if (status & AUDIO_STATUS_PAUSE) {
            rb->audio_resume();
        }
    }
    audio_was_running = transport_restored && old_had_audio;
    playhead = old_position;
    reset_play_clock(playhead, *rb->current_tick);
    if (capture_was_active && !display_locked)
        start_spectrum_capture();
    overview_dirty = true;
    force_full_redraw = true;
}

static bool play_track_index(int index, int row,
                             enum rbprep_mode return_mode)
{
    struct rbprep_track_record track;
    struct mp3entry *id3;
    char path[MAX_PATH];
    char old_path[MAX_PATH];
    const char *extension;
    bool already_loaded;
    bool force_reload = force_track_reload;
    bool resume_existing = false;
    bool capture_was_active = false;
    bool old_had_audio;
    bool old_was_paused;
    bool playlist_replaced = false;
    bool analysis_changed = false;
    bool target_from_playlist;
    int old_track_id = selected_track_id;
    int old_position = clamp_playhead(playhead);
    int old_resume_index = 0;
    int old_status;

    activity_ticker_ping(40);
    if (!read_track_record(index, &track) ||
        !read_index_string(track.path_offset, path, sizeof(path)))
        return false;
    activity_ticker_ping(140);
    if (!rb->file_exists(path)) {
        rb->splashf(HZ * 2, "Missing: %s", path);
        restore_black_canvas();
        return false;
    }
    if ((index != selected_track_index || force_reload) &&
        track_edit_dirty) {
        begin_track_load_confirmation(index, row, return_mode,
                                      force_reload);
        return false;
    }
    id3 = rb->audio_current_track();
    already_loaded = !force_reload && id3 && id3->path &&
                     !rb->strcmp(id3->path, path);
    if (!force_reload && index == selected_track_index && id3 && id3->path &&
        !rb->strcmp(id3->path, path)) {
        if (waveform_points <= 0) {
            bool resume_after_prepare = false;

            if (!quiesce_audio_for_track_load(&resume_after_prepare)) {
                rb->splash(HZ * 2, "Audio pause timed out");
                restore_black_canvas();
                return false;
            }

            capture_was_active = spectrum_capture_active;
            stop_spectrum_capture();
            if (!load_waveform(track.id)) {
                if (resume_after_prepare &&
                    (rb->audio_status() & AUDIO_STATUS_PAUSE))
                    rb->audio_resume();
                if (capture_was_active && !display_locked)
                    start_spectrum_capture();
                rb->splash(HZ * 2, "Waveform preparation failed");
                restore_black_canvas();
                return false;
            }
            load_latest_edit(track.id);
            if (resume_after_prepare &&
                (rb->audio_status() & AUDIO_STATUS_PAUSE))
                rb->audio_resume();
            if (capture_was_active && !display_locked)
                start_spectrum_capture();
        }
        playhead = clamp_playhead(id3->elapsed);
        playing_track_row = row;
        deck_return_mode = return_mode;
        mode = MODE_DECK;
        if (macro_active >= 0 && tool_macros[macro_active].count > 0)
            apply_macro_step(&tool_macros[macro_active].steps[0]);
        reset_play_clock(playhead, *rb->current_tick);
        force_full_redraw = true;
        activity_ticker_ping(1000);
        return true;
    }

    old_status = rb->audio_status();
    old_had_audio = !!(old_status & AUDIO_STATUS_PLAY);
    old_was_paused = !!(old_status & AUDIO_STATUS_PAUSE);
    old_path[0] = '\0';
    if (id3 && id3->path)
        rb->strlcpy(old_path, id3->path, sizeof(old_path));
    if (old_had_audio)
        rb->playlist_get_resume_info(&old_resume_index);
    target_from_playlist = row >= 0 && active_playlist_node >= 0;
    if (!quiesce_audio_for_track_load(&resume_existing)) {
        rb->splash(HZ * 2, "Audio pause timed out");
        restore_black_canvas();
        return false;
    }
    activity_ticker_ping(280);
    old_position = clamp_playhead(playhead);
    capture_was_active = spectrum_capture_active;
    stop_spectrum_capture();
    if (index != selected_track_index)
        save_rbprep_config();
    if (index != selected_track_index || force_reload) {
        activity_ticker_ping(390);
        if (!service_deferred_burn()) {
            restore_failed_track_load(old_track_id, old_path, old_position,
                                      old_resume_index, old_had_audio,
                                      old_was_paused, false, false,
                                      capture_was_active);
            return false;
        }
        {
            bool resume_after_burn = false;

            if (!quiesce_audio_for_track_load(&resume_after_burn)) {
                restore_failed_track_load(old_track_id, old_path, old_position,
                                          old_resume_index, old_had_audio,
                                          old_was_paused, false, false,
                                          capture_was_active);
                rb->splash(HZ * 2, "Audio pause timed out");
                restore_black_canvas();
                return false;
            }
            resume_existing = resume_existing || resume_after_burn;
        }
        /* A playlist/database burn may rebuild the local index.  Continue by
           stable Rekordbox track id rather than a now-stale numeric row. */
        {
            int refreshed_index = find_track_index_by_id(track.id);

            if (refreshed_index < 0 ||
                !read_track_record(refreshed_index, &track) ||
                !read_index_string(track.path_offset, path, sizeof(path)) ||
                !rb->file_exists(path)) {
                restore_failed_track_load(old_track_id, old_path,
                                          old_position, old_resume_index,
                                          old_had_audio, old_was_paused,
                                          false, false, capture_was_active);
                rb->splash(HZ * 2, "Track changed during burn");
                restore_black_canvas();
                return false;
            }
            index = refreshed_index;
            if (target_from_playlist) {
                row = active_playlist_node >= 0
                    ? find_track_row_in_playlist(active_playlist_node, index)
                    : -1;
                if (row < 0) {
                    restore_failed_track_load(old_track_id, old_path,
                                              old_position, old_resume_index,
                                              old_had_audio, old_was_paused,
                                              false, false,
                                              capture_was_active);
                    rb->splash(HZ * 2, "Track left playlist during burn");
                    restore_black_canvas();
                    return false;
                }
            }
        }
    }

    already_loaded = !force_reload && old_had_audio && old_path[0] &&
                     !rb->strcmp(old_path, path);
    analysis_changed = true;
    activity_ticker_ping(520);
    if (!load_waveform(track.id)) {
        restore_failed_track_load(old_track_id, old_path, old_position,
                                  old_resume_index, old_had_audio,
                                  old_was_paused, false, analysis_changed,
                                  capture_was_active);
        rb->splash(HZ * 2, "Waveform preparation failed");
        restore_black_canvas();
        return false;
    }
    activity_ticker_ping(760);
    if (!already_loaded) {
        if (force_reload) {
            rb->audio_stop();
            rb->yield();
            audio_was_running = false;
        }
        if (rb->playlist_create(NULL, NULL) < 0) {
            restore_failed_track_load(old_track_id, old_path, old_position,
                                      old_resume_index, old_had_audio,
                                      old_was_paused, false, analysis_changed,
                                      capture_was_active);
            rb->splash(HZ * 2, "Could not load track");
            restore_black_canvas();
            return false;
        }
        playlist_replaced = true;
        if (rb->playlist_insert_track(NULL, path, PLAYLIST_INSERT_LAST,
                                      false, true) < 0) {
            restore_failed_track_load(old_track_id, old_path, old_position,
                                      old_resume_index, old_had_audio,
                                      old_was_paused, playlist_replaced,
                                      analysis_changed, capture_was_active);
            rb->splash(HZ * 2, "Could not load track");
            restore_black_canvas();
            return false;
        }
    }

    /* Do not alter the old deck's transport rate until every fallible part of
       the replacement load has succeeded.  A failed preparation or playlist
       mutation can then resume the previous deck exactly as it was. */
    if (host_rpm_index != 0 || played_rpm_index != 0 ||
        pitch_bend_x100 != 0 || tempo_x100 != PITCH_SPEED_100) {
        host_rpm_index = played_rpm_index = 0;
        pitch_bend_x100 = 0;
        tempo_x100 = PITCH_SPEED_100;
        apply_playback_rate();
    }

    selected_track_index = index;
    selected_track_id = track.id;
    reset_play_stat(selected_track_id);
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
    if (track.comments_offset)
        read_index_string(track.comments_offset, selected_comments,
                          sizeof(selected_comments));
    else
        selected_comments[0] = '\0';
    track_year = track.year;
    load_latest_edit(track.id);
    activity_ticker_ping(880);
    track_edit_dirty = false;
    id3 = rb->audio_current_track();
    if (already_loaded) {
        if (id3) {
            if (id3->length > 0)
                track_length = id3->length;
            playhead = clamp_playhead(id3->elapsed);
        } else {
            playhead = old_position;
            rb->playlist_start(old_resume_index, playhead, 0);
            if (!resume_existing)
                rb->audio_pause();
        }
        if (resume_existing &&
            (rb->audio_status() & AUDIO_STATUS_PAUSE))
            rb->audio_resume();
    } else {
        playhead = 0;
        rb->playlist_start(0, 0, 0);
    }
    if (capture_was_active && !display_locked)
        start_spectrum_capture();
    reset_play_clock(playhead, *rb->current_tick);
    playing_track_row = row;
    audio_was_running = !!(rb->audio_status() & AUDIO_STATUS_PLAY);
    deck_return_mode = return_mode;
    mode = MODE_DECK;
    if (macro_active >= 0 && tool_macros[macro_active].count > 0)
        apply_macro_step(&tool_macros[macro_active].steps[0]);
    force_full_redraw = true;
    activity_ticker_ping(1000);
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

static void start_shuffled_collection(void)
{
    int count = library_track_count;
    uint32_t candidate;
    uint32_t divisor;

    if (count <= 0) {
        rb->splash(HZ * 2, "Collection is empty");
        restore_black_canvas();
        return;
    }
    search_active = false;
    if (search_result_fd >= 0) {
        rb->close(search_result_fd);
        search_result_fd = -1;
    }
    track_search[0] = '\0';
    active_playlist_node = -1;
    collection_shuffle_active = true;
    search_result_count = count;
    rb->srand((unsigned int)(*rb->current_tick ^ library_track_count));
    candidate = ((uint32_t)rb->rand() | 1u) % count;
    if (!candidate)
        candidate = 1;
    while (true) {
        uint32_t left = candidate;
        uint32_t right = count;

        while (right) {
            divisor = left % right;
            left = right;
            right = divisor;
        }
        if (left == 1)
            break;
        candidate = (candidate + 2) % count;
        if (!candidate)
            candidate = 1;
    }
    shuffle_multiplier = candidate;
    shuffle_offset = (uint32_t)rb->rand() % count;
    track_row_count = count;
    track_selection = track_top = 0;
    playing_track_row = -1;
    playlist_playback = true;
    macro_active = -1;
    play_track_row(0);
}

static bool navigate_playlist_track(int direction)
{
    int target = playing_track_row + direction;
    bool success;

    if (playing_track_row < 0 || target < 0 || target >= track_row_count)
        return false;
    track_selection = target;
    if (track_selection < track_top)
        track_top = track_selection;
    else if (track_selection >= track_top + RBPREP_LIST_ROWS)
        track_top = track_selection - RBPREP_LIST_ROWS + 1;
    success = play_track_row(track_selection);
    if (success && macro_active < 0)
        mode = MODE_PNAV;
    return success;
}

static bool reload_current_track(void)
{
    int index = selected_track_index;
    int row = playing_track_row;
    enum rbprep_mode return_mode = deck_return_mode;
    bool success;

    if (index < 0)
        return false;
    force_track_reload = true;
    success = play_track_index(index, row, return_mode);
    force_track_reload = false;
    if (success && macro_active < 0)
        mode = MODE_PNAV;
    return success;
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
    rb->lcd_set_foreground(LCD_RGBPACK(18, 21, 19));
    rb->lcd_fillrect(2, RBPREP_STATUS_HEIGHT + 1, LCD_WIDTH - 4,
                     LCD_HEIGHT - RBPREP_STATUS_HEIGHT - 3);
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(5, RBPREP_STATUS_HEIGHT + 3, LCD_WIDTH - 10,
                     LCD_HEIGHT - RBPREP_STATUS_HEIGHT - 8);
    rb->lcd_set_foreground(LCD_RGBPACK(5, 12, 8));
    rb->lcd_fillrect(5, RBPREP_STATUS_HEIGHT + 3, LCD_WIDTH - 10, 19);
    rb->lcd_set_foreground(accent);
    rb->lcd_fillrect(5, RBPREP_STATUS_HEIGHT + 3, 3,
                     LCD_HEIGHT - RBPREP_STATUS_HEIGHT - 8);
    text(10, RBPREP_STATUS_HEIGHT + 4, title, LCD_WHITE);
    {
        static const char *tabs[] = { "BRS", "TAG", "INF", "MNU" };
        int tab;

        for (tab = 0; tab < 4; tab++) {
            int x = 218 + tab * 24;
            rb->lcd_set_foreground(LCD_RGBPACK(25, 31, 27));
            rb->lcd_fillrect(x, RBPREP_STATUS_HEIGHT + 5, 21, 12);
            rb->lcd_set_foreground(tab == 0 ? accent
                                            : LCD_RGBPACK(72, 82, 76));
            rb->lcd_drawrect(x, RBPREP_STATUS_HEIGHT + 5, 21, 12);
            text(x + 2, RBPREP_STATUS_HEIGHT + 7, tabs[tab],
                 tab == 0 ? LCD_WHITE : LCD_RGBPACK(140, 150, 144));
        }
    }
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
    xlcd_drawcircle(cx, cy, 4);
    if (item == 0) {
        xlcd_drawcircle(cx, cy, 2);
        xlcd_fillcircle(cx, cy, 1);
    } else if (item == 1) {
        rb->lcd_drawline(cx - 3, cy - 2, cx + 3, cy + 2);
        rb->lcd_drawline(cx - 3, cy + 2, cx + 3, cy - 2);
    } else if (item == 2) {
        rb->lcd_fillrect(cx - 3, cy - 1, 7, 4);
        rb->lcd_fillrect(cx - 2, cy - 3, 3, 2);
    } else if (item == 3) {
        xlcd_drawcircle(cx - 1, cy, 2);
        xlcd_fillcircle(cx - 1, cy, 1);
        rb->lcd_drawline(cx + 3, cy - 3, cx + 1, cy + 2);
    } else if (item == 4) {
        rb->lcd_vline(cx, cy - 3, cy + 2);
        rb->lcd_drawline(cx, cy - 2, cx - 3, cy - 1);
        rb->lcd_drawline(cx, cy, cx + 3, cy - 1);
        rb->lcd_drawpixel(cx - 3, cy - 1);
        rb->lcd_drawpixel(cx + 3, cy - 1);
        rb->lcd_drawpixel(cx, cy + 3);
    } else if (item == 5) {
        rb->lcd_vline(cx - 2, cy - 3, cy + 3);
        rb->lcd_vline(cx + 2, cy - 3, cy + 3);
        rb->lcd_fillrect(cx - 3, cy - 1, 3, 2);
        rb->lcd_fillrect(cx + 1, cy + 1, 3, 2);
    } else if (item == 6) {
        rb->lcd_hline(cx - 2, cx + 3, cy - 2);
        rb->lcd_hline(cx - 2, cx + 3, cy);
        rb->lcd_hline(cx - 2, cx + 3, cy + 2);
        rb->lcd_drawpixel(cx - 3, cy - 2);
        rb->lcd_drawpixel(cx - 3, cy);
        rb->lcd_drawpixel(cx - 3, cy + 2);
    } else {
        rb->lcd_vline(cx - 3, cy - 3, cy + 3);
        rb->lcd_hline(cx - 3, cx, cy - 3);
        rb->lcd_hline(cx - 3, cx, cy + 3);
        rb->lcd_drawline(cx, cy, cx + 3, cy);
        rb->lcd_drawline(cx + 3, cy, cx + 1, cy - 2);
        rb->lcd_drawline(cx + 3, cy, cx + 1, cy + 2);
    }
}

static void draw_playlist_browser(void)
{
    int row;
    char line[96];

    draw_blade_shell(playlist_add_mode ? "ADD TO PLAYLIST" :
                     playlist_move_mode ? "MOVE PLAYLIST" : "PLAYLISTS",
                     RBPREP_GREEN);
    if (tree_parent == RBPREP_ROOT_NODE) {
        text(112, RBPREP_STATUS_HEIGHT + 3, "/", LCD_LIGHTGRAY);
    } else if (tree_parent_name[0])
        text(112, RBPREP_STATUS_HEIGHT + 4, tree_parent_name,
             LCD_LIGHTGRAY);

    for (row = 0; row < RBPREP_LIST_ROWS; row++) {
        int ordinal = tree_top + row;
        int y = 34 + row * 19;
        struct rbprep_playlist_cache_row *cached;
        struct rbprep_node_record favorite_node;
        struct rbprep_node_record *node;
        char favorite_name[80];
        const char *display_name = "";
        int favorite_slot = -1;
        int actual_ordinal;

        if (ordinal >= playlist_browser_count())
            break;
        if (playlist_add_mode && ordinal == 0) {
            if (ordinal == tree_selection)
                draw_blade_selection(y - 2, 19,
                                     LCD_RGBPACK(34, 105, 73));
            rb->lcd_set_foreground(ordinal == tree_selection
                                   ? LCD_WHITE : RBPREP_GREEN);
            rb->lcd_drawrect(10, y + 1, 13, 13);
            rb->lcd_hline(13, 20, y + 7);
            rb->lcd_vline(16, y + 4, y + 10);
            text(30, y + 2, "NEW PLAYLIST...", LCD_WHITE);
            continue;
        }
        actual_ordinal = ordinal - (playlist_add_mode ? 1 : 0);
        if (tree_parent == RBPREP_ROOT_NODE) {
            int slot;

            for (slot = 0; slot < 2; slot++) {
                if (favorite_playlist_nodes[slot] < 0)
                    continue;
                if (actual_ordinal-- == 0) {
                    favorite_slot = slot;
                    break;
                }
            }
        }
        if (favorite_slot >= 0) {
            if (!read_node_record(favorite_playlist_nodes[favorite_slot],
                                  &favorite_node) ||
                !read_index_string(favorite_node.name_offset, favorite_name,
                                   sizeof(favorite_name)))
                continue;
            node = &favorite_node;
            display_name = favorite_name;
        } else {
            if (!cached_playlist_node(actual_ordinal, &cached))
                continue;
            node = &cached->node;
            display_name = cached->name;
        }
        if (ordinal == tree_selection) {
            draw_blade_selection(y - 2, 19,
                                 node->kind == 0
                                 ? LCD_RGBPACK(34, 105, 73)
                                 : LCD_RGBPACK(27, 78, 128));
        }
        draw_playlist_node_glyph(16, y + 7, node->kind == 0,
                                 ordinal == tree_selection);
        rb->snprintf(line, sizeof(line), "%s%.34s",
                     favorite_slot >= 0 ? (favorite_slot ? "F2  " : "F1  ")
                                        : "",
                     display_name);
        text(30, y + 2, line, LCD_WHITE);
        if (node->kind != 0) {
            int macro_slot = macro_link_for(node->source_id);
            if (node->kind == 2)
                text(212, y + 2, "SMART", RBPREP_GREEN);
            if (macro_slot >= 0)
                text(258, y + 2, macro_slot ? "M2" : "M1",
                     LCD_RGBPACK(255, 145, 40));
            rb->snprintf(line, sizeof(line), "%lu",
                         (unsigned long)node->member_count);
            text(282, y + 2, line, LCD_LIGHTGRAY);
        }
    }
    rb->lcd_set_foreground(LCD_RGBPACK(14, 24, 18));
    rb->lcd_fillrect(0, 210, LCD_WIDTH, 30);
    rb->snprintf(line, sizeof(line), "%d/%d", tree_selection + 1,
                 playlist_browser_count());
    text(7, 213, line, RBPREP_GREEN);
    text(70, 213, "WHEEL: BROWSE", LCD_LIGHTGRAY);
    text(7, 226, playlist_add_mode
         ? "SELECT: CHOOSE DESTINATION     MENU: CANCEL"
         : playlist_move_mode
         ? "SELECT: OPEN FOLDER   PLAY: MOVE HERE   MENU: CANCEL"
         : "SELECT: OPEN   HOLD: MANAGE   MENU: BACK", LCD_WHITE);
}

static void draw_playlist_actions(void)
{
    static const char * const actions[] = {
        "CREATE PLAYLIST HERE", "RENAME PLAYLIST",
        "MOVE PLAYLIST", "DELETE PLAYLIST", "WORKFLOW PAD",
        "SET AS FAVORITE 1", "SET AS FAVORITE 2", "CLEAR FAVORITE"
    };
    struct rbprep_node_record node;
    char name[64];
    char line[96];
    bool has_target = playlist_action_node >= 0 &&
                      read_node_record(playlist_action_node, &node);
    bool editable = has_target && node.kind != 0;
    int i;

    draw_blade_shell("PLAYLIST MANAGER", RBPREP_GREEN);
    if (has_target && read_index_string(node.name_offset, name, sizeof(name)))
        rb->snprintf(line, sizeof(line), "SELECTED: %.42s", name);
    else
        rb->snprintf(line, sizeof(line), "DESTINATION: CURRENT FOLDER");
    text(12, 35, line, LCD_RGBPACK(145, 165, 151));
    for (i = 0; i < (int)ARRAYLEN(actions); i++) {
        int y = 48 + i * 20;
        bool enabled = i == 0 || (i >= 5 ? has_target : editable);
        if (i == playlist_action_selection)
            draw_blade_selection(y - 2, 18, RBPREP_GREEN);
        draw_playlist_node_glyph(20, y + 7, false,
                                 i == playlist_action_selection);
        text(40, y + 3, actions[i],
             enabled ? LCD_WHITE : LCD_RGBPACK(65, 75, 68));
        if (i == 4 && editable) {
            int macro_slot = macro_link_for(node.source_id);
            text(255, y + 3, macro_slot < 0 ? "OFF" :
                 macro_slot == 0 ? "M1" : "M2", RBPREP_GREEN);
        }
        if ((i == 5 || i == 6) && has_target &&
            favorite_playlist_ids[i - 5] == (int)node.source_id)
            text(278, y + 3, "SET", RBPREP_GREEN);
    }
    text(7, 210, "WHEEL: CHOOSE    SELECT: OPEN", LCD_WHITE);
    text(7, 226, "MENU: BACK", LCD_LIGHTGRAY);
}

static void draw_track_browser(void)
{
    int row;
    char title_buffer[96];
    char artist_buffer[72];
    char genre_buffer[48];
    char key_buffer[24];
    char display_key[24];
    char name[80];
    char line[96];

    draw_blade_shell(active_playlist_node < 0 ? "COLLECTION" : "PLAYLIST",
                     RBPREP_GREEN);
    if (active_playlist_node >= 0) {
        struct rbprep_node_record node;
        if (read_node_record(active_playlist_node, &node) &&
            read_index_string(node.name_offset, name, sizeof(name))) {
            text(75, RBPREP_STATUS_HEIGHT + 4, name, LCD_LIGHTGRAY);
            if (node.kind == 2)
                text(272, RBPREP_STATUS_HEIGHT + 4, "SMART", RBPREP_GREEN);
        }
    } else {
        if (collection_shuffle_active) {
            rb->strlcpy(line, "SHUFFLED  /  AUTO-NEXT", sizeof(line));
            text(154, RBPREP_STATUS_HEIGHT + 4, line, RBPREP_GREEN);
        } else {
            rb->snprintf(line, sizeof(line), "%s %s%s",
                         track_sort_names[track_sort_key],
                         track_sort_descending ? "v" : "^",
                         search_active ? "  FILTERED" : "");
            text(184, RBPREP_STATUS_HEIGHT + 4, line, search_active
                 ? LCD_RGBPACK(255, 145, 40)
                 : LCD_RGBPACK(105, 125, 112));
        }
    }

    for (row = 0; row < RBPREP_LIST_ROWS; row++) {
        int ordinal = track_top + row;
        int index;
        int y = 32 + row * 20;
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
        if (!read_index_string(track.genre_offset, genre_buffer,
                               sizeof(genre_buffer)))
            genre_buffer[0] = '\0';
        if (!read_index_string(track.key_offset, key_buffer,
                               sizeof(key_buffer)))
            key_buffer[0] = '\0';
        format_key_name(key_buffer, display_key, sizeof(display_key));
        if (track_sort_key == TRACK_SORT_COMMENTS)
            read_index_string(track.comments_offset, artist_buffer,
                              sizeof(artist_buffer));
        else if (track_sort_key == TRACK_SORT_TAGS)
            read_index_string(track.tags_offset, artist_buffer,
                              sizeof(artist_buffer));
        else if (track_sort_key == TRACK_SORT_YEAR) {
            rb->strlcpy(name, artist_buffer, sizeof(name));
            rb->snprintf(artist_buffer, sizeof(artist_buffer),
                         "%04d  %.24s", track.year, name);
        } else if (track_sort_key == TRACK_SORT_IMPORTED) {
            rb->strlcpy(name, artist_buffer, sizeof(name));
            if (track.import_date) {
                int year = 1980 + (track.import_date >> 9);
                int month = (track.import_date >> 5) & 15;
                int day = track.import_date & 31;
                rb->snprintf(artist_buffer, sizeof(artist_buffer),
                             "%04d-%02d-%02d  %.18s", year, month, day,
                             name);
            } else {
                rb->snprintf(artist_buffer, sizeof(artist_buffer),
                             "DATE --  %.22s", name);
            }
        }
        if (genre_buffer[0]) {
            rb->strlcat(artist_buffer, " [", sizeof(artist_buffer));
            rb->strlcat(artist_buffer, genre_buffer, sizeof(artist_buffer));
            rb->strlcat(artist_buffer, "]", sizeof(artist_buffer));
        }
        if (ordinal == track_selection) {
            draw_blade_selection(y - 1, 20,
                                 LCD_RGBPACK(25, 91, 148));
        }
        draw_record_badge(12, y + 8, 5,
                          track_color_display(track.color),
                          ordinal == track_selection);
        rb->snprintf(line, sizeof(line), "%.38s", title_buffer);
        text(22, y, line, LCD_WHITE);
        /* Let the secondary metadata run beneath the fixed DJ columns. BPM
           and Key are drawn afterwards, so they remain authoritative when a
           long Artist + [Genre] string reaches the right-hand side. */
        rb->snprintf(line, sizeof(line), "%.46s", artist_buffer);
        text(28, y + 10, line, LCD_RGBPACK(145, 165, 151));
        rb->snprintf(line, sizeof(line), "%d.%02d",
                     track.bpm_x100 / 100, track.bpm_x100 % 100);
        text(232, y + 10, line, RBPREP_GREEN);
        rb->snprintf(line, sizeof(line), "%.6s",
                     key_buffer[0] ? display_key : "--");
        text(276, y + 10, line, LCD_WHITE);
    }
    rb->lcd_set_foreground(LCD_RGBPACK(14, 24, 18));
    rb->lcd_fillrect(0, 210, LCD_WIDTH, 30);
    rb->snprintf(line, sizeof(line), "%d/%d", track_selection + 1,
                 track_row_count);
    text(7, 213, line, RBPREP_GREEN);
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
        "SORT YEAR", "SORT KEY", "SORT COMMENTS", "SORT TAGS",
        "SORT IMPORT DATE"
    };
    char line[80];
    int row;

    draw_blade_shell("COLLECTION SEARCH + SORT",
                     RBPREP_GREEN);
    rb->snprintf(line, sizeof(line), "QUERY  %s",
                 track_search[0] ? track_search : "<ALL TRACKS>");
    text(7, 34, line, track_search[0]
         ? RBPREP_GREEN : LCD_RGBPACK(105, 125, 112));
    for (row = 0; row < (int)ARRAYLEN(items); row++) {
        int y = 52 + row * 19;
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
             : active ? RBPREP_GREEN
                      : LCD_RGBPACK(178, 192, 183));
        if (active)
            text(286, y, track_sort_descending ? "v" : "^",
                 RBPREP_GREEN);
    }
    text(7, 226, "WHEEL: CHOOSE   SELECT: APPLY   MENU: TRACKS", LCD_WHITE);
}

static enum rbprep_tool active_tool(void)
{
    int page;
    int selected;

    if (macro_active >= 0 && macro_tool_override >= 0 &&
        macro_tool_override < TOOL_COUNT)
        return (enum rbprep_tool)macro_tool_override;
    for (page = 0; page < (int)ARRAYLEN(tool_pages); page++) {
        if (tool_pages[page].mode != mode)
            continue;
        if (mode == MODE_DECK) selected = deck_tool;
        else if (mode == MODE_TEMPO) selected = tempo_tool;
        else if (mode == MODE_GRID) selected = grid_tool;
        else if (mode == MODE_CUES) selected = cue_tool;
        else if (mode == MODE_LOOP) selected = loop_tool;
        else if (mode == MODE_METADATA) selected = metadata_tool;
        else if (mode == MODE_PITCH) selected = pitch_tool;
        else if (mode == MODE_LIST) selected = list_tool;
        else if (mode == MODE_PNAV) selected = pnav_tool;
        else if (mode == MODE_VISUALIZER) selected = visualizer_tool;
        else if (mode == MODE_VISUALIZER_TWO)
            selected = visualizer_two_tool;
        else selected = macro_tool;
        selected = MAX(0, MIN(tool_pages[page].count - 1, selected));
        return tool_pages[page].tools[selected];
    }
    return TOOL_SEEK;
}

static int tool_page_index(void)
{
    int page;
    for (page = 0; page < (int)ARRAYLEN(tool_pages); page++)
        if (tool_pages[page].mode == mode)
            return page;
    return 0;
}

static int tool_count(void)
{
    return tool_pages[tool_page_index()].count;
}

static int *tool_selection(void)
{
    if (mode == MODE_DECK)
        return &deck_tool;
    if (mode == MODE_TEMPO)
        return &tempo_tool;
    if (mode == MODE_GRID)
        return &grid_tool;
    if (mode == MODE_CUES)
        return &cue_tool;
    if (mode == MODE_LOOP)
        return &loop_tool;
    if (mode == MODE_METADATA)
        return &metadata_tool;
    if (mode == MODE_PITCH)
        return &pitch_tool;
    if (mode == MODE_LIST)
        return &list_tool;
    if (mode == MODE_PNAV)
        return &pnav_tool;
    if (mode == MODE_VISUALIZER)
        return &visualizer_tool;
    if (mode == MODE_VISUALIZER_TWO)
        return &visualizer_two_tool;
    return &macro_tool;
}

static void draw_tool_icon(int cx, int cy, enum rbprep_tool tool, int color)
{
    int x = cx - 4;
    int y = cy - 4;

    rb->lcd_set_foreground(color);
    if (tool == TOOL_IPOD_SEEK) {
        /* A tiny wheel iPod: deliberately unmistakable at 9 x 9 pixels. */
        rb->lcd_drawrect(x + 1, y, 7, 9);
        rb->lcd_drawrect(x + 2, y + 1, 5, 3);
        xlcd_drawcircle(cx, y + 6, 2);
        rb->lcd_drawpixel(cx, y + 6);
    } else if (tool == TOOL_SEEK) {
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
        /* Continuous-play/auto-next: two forward play heads and an end bar.
           The old three-line glyph was too easy to mistake for bookmarks. */
        rb->lcd_drawline(x, y + 1, x, y + 7);
        rb->lcd_drawline(x, y + 1, x + 3, cy);
        rb->lcd_drawline(x + 3, cy, x, y + 7);
        rb->lcd_drawline(x + 4, y + 1, x + 7, cy);
        rb->lcd_drawline(x + 7, cy, x + 4, y + 7);
        rb->lcd_vline(x + 8, y + 1, y + 7);
    } else if (tool == TOOL_ADD_PLAYLIST) {
        rb->lcd_hline(x, x + 4, y + 1);
        rb->lcd_hline(x, x + 4, y + 4);
        rb->lcd_hline(x, x + 4, y + 7);
        rb->lcd_hline(x + 5, x + 9, y + 5);
        rb->lcd_vline(x + 7, y + 3, y + 7);
    } else if (tool == TOOL_FAVORITE_ONE ||
               tool == TOOL_FAVORITE_TWO) {
        int digit = tool == TOOL_FAVORITE_ONE ? 1 : 2;

        rb->lcd_drawline(cx, y, cx + 2, cy - 1);
        rb->lcd_drawline(cx + 2, cy - 1, x + 8, cy - 1);
        rb->lcd_drawline(x + 8, cy - 1, cx + 3, cy + 1);
        rb->lcd_drawline(cx + 3, cy + 1, cx + 4, y + 8);
        rb->lcd_drawline(cx + 4, y + 8, cx, cy + 3);
        rb->lcd_drawline(cx, cy + 3, x, y + 8);
        rb->lcd_drawline(x, y + 8, x + 1, cy + 1);
        rb->lcd_drawline(x + 1, cy + 1, x - 1, cy - 1);
        rb->lcd_drawpixel(cx + digit - 2, cy);
    } else if (tool == TOOL_PLAYLIST_PREVIOUS ||
               tool == TOOL_PLAYLIST_NEXT) {
        bool previous = tool == TOOL_PLAYLIST_PREVIOUS;
        int direction = previous ? -1 : 1;
        int bar_x = previous ? x : x + 8;
        int tip_x = cx + direction * 4;

        rb->lcd_vline(bar_x, y + 1, y + 7);
        rb->lcd_drawline(cx - direction * 3, y + 1, tip_x, cy);
        rb->lcd_drawline(tip_x, cy, cx - direction * 3, y + 7);
    } else if (tool == TOOL_RESTART_PLAYBACK) {
        rb->lcd_vline(x, y + 1, y + 7);
        rb->lcd_drawline(x + 2, cy, x + 5, y + 1);
        rb->lcd_drawline(x + 2, cy, x + 5, y + 7);
        rb->lcd_drawline(x + 5, y + 1, x + 8, cy);
        rb->lcd_drawline(x + 8, cy, x + 5, y + 7);
    } else if (tool == TOOL_RELOAD_TRACK) {
        xlcd_drawcircle(cx, cy, 4);
        rb->lcd_drawline(cx - 4, cy - 1, cx - 1, cy - 4);
        rb->lcd_drawline(cx - 4, cy - 1, cx, cy - 1);
    } else if (tool == TOOL_BURN_SONG || tool == TOOL_BURN_ALL) {
        rb->lcd_drawline(cx, y, cx, y + 6);
        rb->lcd_drawline(cx, y + 6, cx - 3, y + 3);
        rb->lcd_drawline(cx, y + 6, cx + 3, y + 3);
        rb->lcd_hline(x, x + 8, y + 8);
        if (tool == TOOL_BURN_ALL)
            rb->lcd_hline(x + 1, x + 7, y + 6);
    } else if (tool == TOOL_WAVEFORM_STYLE) {
        rb->lcd_vline(x, cy - 1, cy + 1);
        rb->lcd_vline(x + 2, cy - 3, cy + 3);
        rb->lcd_vline(x + 4, cy - 4, cy + 4);
        rb->lcd_vline(x + 6, cy - 2, cy + 2);
        rb->lcd_vline(x + 8, cy - 1, cy + 1);
    } else if (tool == TOOL_KEYLOCK) {
        rb->lcd_drawrect(x + 1, cy - 1, 7, 6);
        rb->lcd_hline(cx - 2, cx + 2, cy - 4);
        rb->lcd_vline(cx - 3, cy - 3, cy - 1);
        rb->lcd_vline(cx + 3, cy - 3, cy - 1);
        rb->lcd_vline(cx, cy + 1, cy + 3);
    } else if (tool == TOOL_META_KEY) {
        rb->lcd_drawline(x + 1, y + 1, x + 1, y + 7);
        rb->lcd_drawline(x + 1, y + 1, x + 6, y);
        rb->lcd_drawline(x + 6, y, x + 6, y + 6);
        xlcd_fillcircle(x, y + 7, 2);
        xlcd_fillcircle(x + 5, y + 6, 2);
    } else if (tool == TOOL_KEY_NOTATION) {
        rb->lcd_drawline(x, cy - 2, x + 7, cy - 2);
        rb->lcd_drawline(x + 7, cy - 2, x + 5, cy - 4);
        rb->lcd_drawline(x + 8, cy + 2, x + 1, cy + 2);
        rb->lcd_drawline(x + 1, cy + 2, x + 3, cy + 4);
    } else if (tool == TOOL_VIS_BOOMBOX) {
        rb->lcd_drawrect(x, y + 1, 9, 8);
        xlcd_drawcircle(cx - 2, cy + 1, 2);
        xlcd_drawcircle(cx + 3, cy + 1, 2);
        rb->lcd_hline(x + 1, x + 7, y + 2);
    } else if (tool == TOOL_VIS_EQ) {
        rb->lcd_vline(x, cy - 1, cy + 4);
        rb->lcd_vline(x + 2, cy - 4, cy + 4);
        rb->lcd_vline(x + 4, cy, cy + 4);
        rb->lcd_vline(x + 6, cy - 3, cy + 4);
        rb->lcd_vline(x + 8, cy - 1, cy + 4);
    } else if (tool == TOOL_VIS_TURNTABLE) {
        xlcd_drawcircle(cx, cy, 4);
        xlcd_fillcircle(cx, cy, 1);
        rb->lcd_drawline(cx + 3, cy - 4, cx + 5, cy + 2);
    } else if (tool == TOOL_VIS_CANYON) {
        rb->lcd_drawline(cx, y, x, y + 8);
        rb->lcd_drawline(cx, y, x + 8, y + 8);
        rb->lcd_hline(x + 2, x + 6, y + 5);
        rb->lcd_hline(x + 1, x + 7, y + 7);
    } else if (tool == TOOL_VIS_ORBIT) {
        xlcd_drawcircle(cx, cy, 4);
        rb->lcd_drawline(cx, y, cx, y + 8);
        rb->lcd_drawline(cx - 3, cy - 2, cx + 3, cy + 2);
    } else if (tool == TOOL_VIS_PHRASE) {
        rb->lcd_vline(x, y + 1, y + 8);
        rb->lcd_vline(x + 2, y + 4, y + 8);
        rb->lcd_vline(x + 4, y + 2, y + 8);
        rb->lcd_vline(x + 6, y + 5, y + 8);
        rb->lcd_vline(x + 8, y, y + 8);
        rb->lcd_hline(x, x + 8, y + 8);
    } else if (tool == TOOL_VIS_HARMONIC) {
        xlcd_drawcircle(cx, cy, 4);
        rb->lcd_fillrect(cx - 1, y, 2, 2);
        rb->lcd_fillrect(x, cy + 1, 2, 2);
        rb->lcd_fillrect(x + 7, cy + 1, 2, 2);
        rb->lcd_drawline(cx, y + 1, x + 1, cy + 2);
        rb->lcd_drawline(x + 1, cy + 2, x + 8, cy + 2);
        rb->lcd_drawline(x + 8, cy + 2, cx, y + 1);
    } else if (tool == TOOL_MACRO_ONE || tool == TOOL_MACRO_TWO) {
        rb->lcd_drawrect(x, y + 1, 9, 7);
        rb->lcd_fillrect(x + 2, y + 3, 2, 2);
        rb->lcd_fillrect(x + 5, y + 3, 2, 2);
        if (tool == TOOL_MACRO_TWO)
            rb->lcd_hline(x + 2, x + 6, y + 7);
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

static bool draw_macro_strip(void)
{
    int active = macro_active;
    int slot;

    if (active < 0 || active >= RBPREP_MACRO_COUNT)
        return false;
    rb->lcd_set_foreground(LCD_RGBPACK(5, 10, 7));
    rb->lcd_fillrect(RBPREP_OVERVIEW_X, RBPREP_OVERVIEW_Y,
                     RBPREP_OVERVIEW_WIDTH, RBPREP_OVERVIEW_HEIGHT);
    for (slot = 0; slot < RBPREP_MACRO_COUNT; slot++) {
        int count = tool_macros[slot].count;
        int visible = MIN(count, 20);
        int first = MAX(0, MIN(count - visible,
                        macro_position - visible / 2));
        int cy = RBPREP_OVERVIEW_Y + 7 + slot * 14;
        int i;

        text(2, cy - 4, slot == 0 ? "M1" : "M2",
             active == slot ? RBPREP_GREEN : LCD_RGBPACK(82, 100, 88));
        if (count <= 0) {
            text(25, cy - 4, "EMPTY - HOLD ORB TO PROGRAM",
                 LCD_RGBPACK(90, 110, 97));
            continue;
        }
        for (i = 0; i < visible; i++) {
            int step = first + i;
            int cx = 31 + i * 14;
            bool selected = slot == active && step == macro_position;
            int color = selected ? LCD_WHITE :
                        slot == active ? LCD_RGBPACK(42, 78, 55) :
                                         LCD_RGBPACK(30, 44, 35);

            rb->lcd_set_foreground(color);
            xlcd_fillcircle(cx, cy, selected ? 6 : 5);
            draw_tool_icon(cx, cy, tool_macros[slot].steps[step].tool,
                           selected ? LCD_BLACK : LCD_RGBPACK(170, 192, 177));
            if (tool_macros[slot].steps[step].value != RBPREP_MACRO_CHOOSE) {
                rb->lcd_set_foreground(LCD_RGBPACK(255, 145, 40));
                rb->lcd_fillrect(cx + 4, cy - 6, 2, 2);
            }
        }
    }
    if (!macro_store_valid)
        text(278, RBPREP_OVERVIEW_Y + 1, "!", LCD_RGBPACK(255, 145, 40));
    rb->lcd_set_foreground(LCD_RGBPACK(55, 75, 62));
    rb->lcd_drawrect(RBPREP_OVERVIEW_X, RBPREP_OVERVIEW_Y,
                     RBPREP_OVERVIEW_WIDTH, RBPREP_OVERVIEW_HEIGHT);
    return true;
}

static int macro_picker_count_for_page(int page)
{
    int count = 0;
    int item;

    page = MAX(0, MIN((int)ARRAYLEN(tool_pages) - 1, page));
    /* A workflow may select ordinary deck tools, but never another workflow.
       Filter them by identity so their visual position on MACR is irrelevant. */
    for (item = 0; item < tool_pages[page].count; item++) {
        enum rbprep_tool tool = tool_pages[page].tools[item];

        if (tool != TOOL_MACRO_ONE && tool != TOOL_MACRO_TWO)
            count++;
    }
    return count;
}

static enum rbprep_tool macro_picker_tool_at(int page, int ordinal)
{
    int item;

    page = MAX(0, MIN((int)ARRAYLEN(tool_pages) - 1, page));
    for (item = 0; item < tool_pages[page].count; item++) {
        enum rbprep_tool tool = tool_pages[page].tools[item];

        if (tool == TOOL_MACRO_ONE || tool == TOOL_MACRO_TWO)
            continue;
        if (ordinal-- == 0)
            return tool;
    }
    return TOOL_SEEK;
}

static int macro_picker_page_count(void)
{
    return macro_picker_count_for_page(macro_picker_page);
}

static bool macro_tool_supports_fixed_value(enum rbprep_tool tool)
{
    return tool == TOOL_SCRUB_STEP || tool == TOOL_ZOOM ||
           tool == TOOL_GAIN || tool == TOOL_HOST_RPM ||
           tool == TOOL_PLAY_RPM || tool == TOOL_PITCH_BEND ||
           tool == TOOL_TEMPO || tool == TOOL_PLAYLIST_MODE ||
           tool == TOOL_KEYLOCK || tool == TOOL_META_KEY ||
           tool == TOOL_KEY_NOTATION || tool == TOOL_GRID_BPM ||
           tool == TOOL_GRID_QUANTIZE || tool == TOOL_CUE_SLOT ||
           tool == TOOL_CUE_MOVE || tool == TOOL_CUE_COLOR ||
           tool == TOOL_CUE_DELETE || tool == TOOL_LOOP_LENGTH ||
           tool == TOOL_LOOP_ACTIVE;
}

static int macro_current_tool_value(enum rbprep_tool tool)
{
    if (tool == TOOL_SCRUB_STEP)
        return scrub_step_index;
    if (tool == TOOL_ZOOM)
        return zoom;
    if (tool == TOOL_GAIN)
        return rb->global_status->volume;
    if (tool == TOOL_HOST_RPM)
        return host_rpm_index;
    if (tool == TOOL_PLAY_RPM)
        return played_rpm_index;
    if (tool == TOOL_PITCH_BEND)
        return pitch_bend_x100;
    if (tool == TOOL_TEMPO)
        return tempo_x100;
    if (tool == TOOL_PLAYLIST_MODE)
        return playlist_playback;
    if (tool == TOOL_KEYLOCK)
        return keylock_enabled;
    if (tool == TOOL_META_KEY) {
        int index = key_index_from_name(selected_key);
        return index < 0 ? 0 : index;
    }
    if (tool == TOOL_KEY_NOTATION)
        return key_notation;
    if (tool == TOOL_GRID_BPM)
        return grid_bpm_x100;
    if (tool == TOOL_GRID_QUANTIZE)
        return quantize;
    if (tool == TOOL_CUE_SLOT || tool == TOOL_CUE_MOVE ||
        tool == TOOL_CUE_COLOR || tool == TOOL_CUE_DELETE)
        return cue_slot;
    if (tool == TOOL_LOOP_LENGTH)
        return loop_length_index;
    if (tool == TOOL_LOOP_ACTIVE)
        return loop_active;
    return RBPREP_MACRO_CHOOSE;
}

static void format_macro_value(char *buffer, size_t size,
                               enum rbprep_tool tool, int value)
{
    if (value == RBPREP_MACRO_CHOOSE) {
        rb->strlcpy(buffer, "CHOOSE WHEN USED", size);
    } else if (tool == TOOL_SCRUB_STEP) {
        value = MAX(0, MIN((int)ARRAYLEN(scrub_steps) - 1, value));
        rb->snprintf(buffer, size, "%d ms / TICK", scrub_steps[value]);
    } else if (tool == TOOL_ZOOM) {
        rb->snprintf(buffer, size, "%d x ZOOM", value);
    } else if (tool == TOOL_GAIN) {
        rb->snprintf(buffer, size, "%d %s",
                     rb->sound_val2phys(SOUND_VOLUME, value),
                     rb->sound_unit(SOUND_VOLUME));
    } else if (tool == TOOL_HOST_RPM || tool == TOOL_PLAY_RPM) {
        value = MAX(0, MIN((int)ARRAYLEN(rpm_styles) - 1, value));
        rb->snprintf(buffer, size, "%s RPM", rpm_styles[value]);
    } else if (tool == TOOL_PITCH_BEND) {
        format_percent_delta(buffer, size, value);
        rb->strlcat(buffer, "% BEND", size);
    } else if (tool == TOOL_TEMPO) {
        rb->snprintf(buffer, size, "%d.%02d %%", value / 100,
                     ABS(value % 100));
    } else if (tool == TOOL_GRID_BPM) {
        rb->snprintf(buffer, size, "%d.%02d BPM", value / 100,
                     ABS(value % 100));
    } else if (tool == TOOL_META_KEY) {
        value = MAX(0, MIN((int)ARRAYLEN(chromatic_key_names) - 1, value));
        rb->snprintf(buffer, size, "KEY %s",
                     key_notation == KEY_DISPLAY_CAMELOT
                     ? camelot_key_names[value]
                     : chromatic_key_names[value]);
    } else if (tool == TOOL_KEY_NOTATION) {
        value = MAX(0, MIN((int)ARRAYLEN(key_notation_styles) - 1,
                           value));
        rb->snprintf(buffer, size, "KEY DISPLAY %s",
                     value == KEY_DISPLAY_ORIGINAL ? "ORIGINAL" :
                     value == KEY_DISPLAY_CAMELOT ? "CAMELOT" :
                                                    "CHROMATIC");
    } else if (tool == TOOL_CUE_SLOT || tool == TOOL_CUE_MOVE ||
               tool == TOOL_CUE_COLOR || tool == TOOL_CUE_DELETE) {
        rb->snprintf(buffer, size, "CUE %02d", value + 1);
    } else if (tool == TOOL_LOOP_LENGTH) {
        value = MAX(0, MIN((int)ARRAYLEN(loop_length_names) - 1, value));
        rb->snprintf(buffer, size, "%s BEATS", loop_length_names[value]);
    } else {
        rb->strlcpy(buffer, value ? "ON" : "OFF", size);
    }
}

static void draw_macro_actions(void)
{
    static const char * const actions[] = {
        "RENAME", "EDIT SEQUENCE", "CLEAR"
    };
    struct rbprep_tool_macro *macro = &tool_macros[macro_manage_slot];
    char line[80];
    int i;

    draw_blade_shell("WORKFLOW MANAGER", RBPREP_GREEN);
    rb->snprintf(line, sizeof(line), "M%d  %.23s  /  %d STEPS",
                 macro_manage_slot + 1, macro->name, macro->count);
    text(12, 29, line, LCD_RGBPACK(145, 165, 151));
    for (i = 0; i < (int)ARRAYLEN(actions); i++) {
        int y = 62 + i * 38;
        bool selected = i == macro_action_selection;

        if (selected)
            draw_blade_selection(y - 5, 25, RBPREP_GREEN);
        rb->lcd_set_foreground(selected ? LCD_WHITE : RBPREP_GREEN);
        xlcd_drawcircle(24, y + 7, 7);
        if (i == 0) {
            rb->lcd_drawline(20, y + 10, 28, y + 2);
            rb->lcd_drawline(25, y + 2, 28, y + 5);
        } else if (i == 1) {
            rb->lcd_hline(19, 29, y + 3);
            rb->lcd_hline(19, 29, y + 7);
            rb->lcd_hline(19, 29, y + 11);
        } else {
            rb->lcd_drawrect(20, y + 3, 9, 9);
            rb->lcd_hline(19, 30, y + 1);
        }
        text(44, y + 3, actions[i],
             selected ? LCD_WHITE : LCD_RGBPACK(160, 181, 166));
    }
    text(8, 205, "WHEEL / LEFT RIGHT: CHOOSE", LCD_WHITE);
    text(8, 222, "SELECT: OPEN     HOLD MENU: BACK", LCD_LIGHTGRAY);
}

static void draw_macro_editor(void)
{
    static const char * const operations[] = {
        "INSERT", "REPLACE", "DELETE", "DEFAULT"
    };
    struct rbprep_tool_macro *macro = &tool_macros[macro_manage_slot];
    char line[80];
    int page_first = (macro_edit_position / 24) * 24;
    int i;

    draw_blade_shell("WORKFLOW EDITOR", RBPREP_GREEN);
    rb->snprintf(line, sizeof(line), "M%d  %.18s  %d/%d  PAGE %d/2",
                 macro_manage_slot + 1, macro->name, macro->count,
                 RBPREP_MACRO_STEPS, page_first / 24 + 1);
    text(9, 27, line, LCD_RGBPACK(155, 179, 161));
    for (i = 0; i < 4; i++) {
        int left = 7 + i * 77;
        int color = i == macro_edit_operation ? RBPREP_GREEN
                                               : LCD_RGBPACK(55, 76, 63);

        rb->lcd_set_foreground(color);
        rb->lcd_fillrect(left, 43, 73, 19);
        centered_text(left, 73, 47, operations[i],
                      i == macro_edit_operation ? LCD_BLACK : LCD_LIGHTGRAY);
    }
    for (i = 0; i < 24; i++) {
        int step = page_first + i;
        int column = i % 8;
        int row = i / 8;
        int cx = 23 + column * 39;
        int cy = 82 + row * 36;
        bool used = step < macro->count;
        bool insertion = macro_edit_operation == 0 &&
                         step == macro->count &&
                         macro->count < RBPREP_MACRO_STEPS;
        bool selected = step == macro_edit_position && (used || insertion);
        int color = selected ? LCD_WHITE : used ? LCD_RGBPACK(40, 78, 54)
                                                 : LCD_RGBPACK(21, 31, 25);

        rb->lcd_set_foreground(color);
        if (used)
            xlcd_fillcircle(cx, cy, selected ? 10 : 8);
        else
            xlcd_drawcircle(cx, cy, selected ? 10 : 8);
        if (used)
            draw_tool_icon(cx, cy, macro->steps[step].tool,
                           selected ? LCD_BLACK : LCD_RGBPACK(190, 213, 197));
        else if (insertion)
            text(cx - 3, cy - 5, "+", selected ? LCD_WHITE : RBPREP_GREEN);
        if (used && macro->steps[step].value != RBPREP_MACRO_CHOOSE) {
            rb->lcd_set_foreground(LCD_RGBPACK(255, 145, 40));
            rb->lcd_fillrect(cx + 7, cy - 9, 3, 3);
        }
        rb->snprintf(line, sizeof(line), "%02d", step + 1);
        centered_text(cx - 12, 24, cy + 11, line,
                      selected ? RBPREP_GREEN : LCD_RGBPACK(68, 84, 73));
    }
    text(8, 193, "WHEEL / LEFT RIGHT: POSITION", LCD_WHITE);
    text(8, 208, "MENU / PLAY: INSERT REPLACE DELETE DEFAULT", LCD_LIGHTGRAY);
    text(8, 223, "SELECT: APPLY     HOLD MENU: BACK", RBPREP_GREEN);
}

static void draw_macro_picker(void)
{
    static const char * const operations[] = {
        "INSERT", "REPLACE", "DELETE"
    };
    int page = MAX(0, MIN((int)ARRAYLEN(tool_pages) - 1,
                          macro_picker_page));
    int count = macro_picker_page_count();
    char line[80];
    int i;

    draw_blade_shell("CHOOSE WORKFLOW TOOL", RBPREP_GREEN);
    rb->snprintf(line, sizeof(line), "%s AT %02d  /  PAGE %s",
                 operations[macro_edit_operation], macro_edit_position + 1,
                 tool_pages[page].label);
    text(10, 28, line, LCD_RGBPACK(155, 179, 161));
    for (i = 0; i < count; i++) {
        enum rbprep_tool tool = macro_picker_tool_at(page, i);
        int column = i % 3;
        int row = i / 3;
        int left = 7 + column * 104;
        int cx = left + 52;
        int cy = 82 + row * 65;
        bool selected = i == macro_picker_tool;

        if (selected)
            draw_blade_selection(cy - 18, 35, RBPREP_GREEN);
        rb->lcd_set_foreground(selected ? LCD_WHITE
                                        : LCD_RGBPACK(42, 76, 53));
        xlcd_fillcircle(cx, cy - 2, selected ? 13 : 11);
        draw_tool_icon(cx, cy - 2, tool,
                       selected ? LCD_BLACK : LCD_RGBPACK(188, 209, 194));
        centered_text(left, 104, cy + 15, tool_names[tool],
                      selected ? LCD_WHITE : LCD_RGBPACK(142, 164, 149));
    }
    text(8, 203, "WHEEL / LEFT RIGHT: TOOL", LCD_WHITE);
    text(8, 219, "MENU / PLAY: PAGE   SELECT: CHOOSE", RBPREP_GREEN);
}

static void draw_macro_value_editor(void)
{
    char value[64];
    const char *choice = macro_value_choose ? "CHOOSE WHEN USED"
                                            : "USE STORED VALUE";

    draw_blade_shell("WORKFLOW DEFAULT", RBPREP_GREEN);
    text(10, 37, tool_names[macro_value_tool], LCD_WHITE);
    text(10, 55, macro_value_new_step ? "NEW CELL" : "EXISTING CELL",
         LCD_RGBPACK(130, 151, 136));
    rb->lcd_set_foreground(LCD_RGBPACK(17, 24, 20));
    rb->lcd_fillrect(9, 78, LCD_WIDTH - 18, 54);
    rb->lcd_set_foreground(macro_value_choose ? LCD_RGBPACK(80, 180, 245)
                                              : RBPREP_GREEN);
    rb->lcd_drawrect(9, 78, LCD_WIDTH - 18, 54);
    format_macro_value(value, sizeof(value), macro_value_tool,
                       macro_value_choose ? RBPREP_MACRO_CHOOSE
                                          : macro_value_draft);
    centered_text(9, LCD_WIDTH - 18, 91, value, LCD_WHITE);
    centered_text(9, LCD_WIDTH - 18, 112, choice,
                  macro_value_choose ? LCD_RGBPACK(80, 180, 245)
                                     : RBPREP_GREEN);

    rb->lcd_set_foreground(LCD_RGBPACK(24, 30, 27));
    rb->lcd_fillrect(9, 145, LCD_WIDTH - 18, 27);
    text(18, 153, "WHEEL TOUCH", LCD_LIGHTGRAY);
    text(224, 153, macro_value_choose ? "LIVE" :
         macro_value_lock ? "LOCKED" : "LIVE",
         !macro_value_choose && macro_value_lock
         ? LCD_RGBPACK(255, 145, 40) : RBPREP_GREEN);

    text(8, 188, "WHEEL: VALUE    MENU: FIXED / CHOOSE", LCD_WHITE);
    text(8, 204, "PLAY: WHEEL LOCK    SELECT: SAVE", RBPREP_GREEN);
    text(8, 220, "HOLD MENU: BACK", LCD_LIGHTGRAY);
}

static void draw_beat_phase(void)
{
    char position[8];
    int bar = 1;
    int beat = 1;
    int index = imported_grid_resident()
              ? current_beat_index(playhead) : -1;
    int i;

    if (index >= 0) {
        int first_beat = adjusted_beat_number(0);

        beat = adjusted_beat_number(index);
        bar = (index + first_beat - 1) / 4 + 1;
    } else if (!imported_grid_resident()) {
        int ordinal = (playhead - grid_phase_ms - grid_offset) /
                      beat_period_ms();
        if (ordinal >= 0) {
            beat = (ordinal & 3) + 1;
            bar = ordinal / 4 + 1;
        }
    }

    rb->snprintf(position, sizeof(position), "%d.%d", MIN(999, bar), beat);
    text(232, 69, position, LCD_WHITE);

    for (i = 0; i < 4; i++) {
        int x = 264 + i * 5;
        rb->lcd_set_foreground(i + 1 == beat
                              ? RBPREP_GREEN
                              : LCD_RGBPACK(35, 53, 42));
        rb->lcd_fillrect(x, 70, 3, 7);
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
    char title[320];
    char identity[220];
    char fixed[64];
    char tail[96];
    char fitted[64];
    char display_key[24];
    int width;
    bool title_overflow;
    bool metadata_overflow;
    const char *extension = selected_extension;
    int title_cycle;

    rb->lcd_set_foreground(LCD_RGBPACK(13, 13, 13));
    rb->lcd_fillrect(0, RBPREP_STATUS_HEIGHT, LCD_WIDTH, 13);
    rb->lcd_set_foreground(LCD_RGBPACK(8, 8, 8));
    rb->lcd_fillrect(0, RBPREP_STATUS_HEIGHT + 13, LCD_WIDTH, 13);

    if (extension[0] == '.')
        extension++;
    if (track_year > 0)
        rb->snprintf(identity, sizeof(identity), "[%04d] %s - %s",
                     track_year,
                     selected_artist[0] ? selected_artist : "RBPREP",
                     selected_title[0] ? selected_title : mode_names[mode]);
    else
        rb->snprintf(identity, sizeof(identity), "%s - %s",
                     selected_artist[0] ? selected_artist : "RBPREP",
                     selected_title[0] ? selected_title : mode_names[mode]);
    if (selected_comments[0]) {
        if (extension[0])
            rb->snprintf(title, sizeof(title), "%s [%s] || %s ||",
                         identity,
                         extension, selected_comments);
        else
            rb->snprintf(title, sizeof(title), "%s || %s ||",
                         identity,
                         selected_comments);
    } else {
        rb->snprintf(title, sizeof(title), extension[0]
                     ? "%s [%s]" : "%s",
                     identity,
                     extension);
    }
    rb->lcd_getstringsize(title, &width, NULL);
    title_overflow = width > LCD_WIDTH - 4;
    if (!title_overflow)
        title_scroll_px = 0;
    else if (advance_scroll) {
        title_cycle = width + 24;
        title_scroll_px = (title_scroll_px + 2) % title_cycle;
    }
    text(2 - title_scroll_px, RBPREP_STATUS_HEIGHT + 2, title, LCD_WHITE);
    if (title_overflow) {
        title_cycle = width + 24;
        text(2 - title_scroll_px + title_cycle,
             RBPREP_STATUS_HEIGHT + 2, title, LCD_WHITE);
    }

    rb->snprintf(tail, sizeof(tail), "%s  /  %s",
                 color_labels[normalize_track_color(color_index)],
                 selected_genre[0] ? selected_genre : "--");
    rb->lcd_getstringsize(tail, &width, NULL);
    metadata_overflow = width > LCD_WIDTH - 132;
    if (!metadata_overflow)
        metadata_scroll_px = 0;
    else if (advance_scroll)
        advance_hud_scroll(&metadata_scroll_px, width, LCD_WIDTH - 132);
    text(132 - metadata_scroll_px, RBPREP_STATUS_HEIGHT + 15, tail,
         track_color_display(color_index));

    /* The left metadata block also acts as a clip for the scrolling tail. */
    rb->lcd_set_foreground(LCD_RGBPACK(8, 8, 8));
    rb->lcd_fillrect(0, RBPREP_STATUS_HEIGHT + 13, 132, 13);
    format_key_name(selected_key, display_key, sizeof(display_key));
    rb->snprintf(fixed, sizeof(fixed), "%d.%02d  %s",
                 grid_bpm_x100 / 100, grid_bpm_x100 % 100,
                 selected_key[0] ? display_key : "--");
    fit_text(fitted, sizeof(fitted), fixed, 72);
    text(2, RBPREP_STATUS_HEIGHT + 15, fitted,
         LCD_RGBPACK(85, 220, 255));
    draw_rating_stars(76, RBPREP_STATUS_HEIGHT + 17);
    rb->lcd_set_foreground(track_color_display(color_index));
    if (normalize_track_color(color_index) == RBPREP_TRACK_COLOR_NONE)
        rb->lcd_drawrect(120, RBPREP_STATUS_HEIGHT + 16, 7, 7);
    else
        rb->lcd_fillrect(120, RBPREP_STATUS_HEIGHT + 16, 7, 7);
    hud_scroll_active = title_overflow || metadata_overflow;
}

static bool pnav_tool_available(enum rbprep_tool tool)
{
    if (tool == TOOL_PLAYLIST_PREVIOUS)
        return playing_track_row > 0 && track_row_count > 0;
    if (tool == TOOL_PLAYLIST_NEXT)
        return playing_track_row >= 0 &&
               playing_track_row + 1 < track_row_count;
    if (tool == TOOL_RESTART_PLAYBACK || tool == TOOL_RELOAD_TRACK)
        return selected_track_index >= 0;
    if (tool == TOOL_FAVORITE_ONE)
        return selected_track_id >= 0 && favorite_playlist_nodes[0] >= 0;
    if (tool == TOOL_FAVORITE_TWO)
        return selected_track_id >= 0 && favorite_playlist_nodes[1] >= 0;
    return true;
}

static void format_favorite_tool_status(int slot, char *buffer, size_t size)
{
    struct rbprep_node_record node;
    char name[48];

    if (slot < 0 || slot > 1 || favorite_playlist_nodes[slot] < 0 ||
        !read_node_record(favorite_playlist_nodes[slot], &node) ||
        !read_index_string(node.name_offset, name, sizeof(name))) {
        rb->snprintf(buffer, size, "FAVLIST%d  NOT SET", slot + 1);
        return;
    }
    rb->snprintf(buffer, size, "FAVLIST%d  %.28s%s", slot + 1, name,
                 node.kind == 0 ? " /" : "");
}

static void draw_tool_orbs(void)
{
    int page = tool_page_index();
    int count = tool_count();
    int selected = *tool_selection();
    int i;

    text(2, 69, tool_pages[page].label,
         tool_menu_active ? LCD_WHITE : RBPREP_GREEN);
    for (i = 0; i < count; i++) {
        enum rbprep_tool tool = tool_pages[page].tools[i];
        int cx = 49 + i * 16;
        bool enabled = pnav_tool_available(tool);
        int color = !enabled ? LCD_RGBPACK(8, 10, 9) : i == selected
                  ? (tool_menu_active ? LCD_WHITE : RBPREP_GREEN)
                  : LCD_RGBPACK(48, 70, 56);
        rb->lcd_set_foreground(color);
        if (enabled)
            xlcd_fillcircle(cx, 73, i == selected ? 7 : 6);
        else
            xlcd_drawcircle(cx, 73, i == selected ? 7 : 6);
        draw_tool_icon(cx, 73, tool,
                       !enabled ? LCD_RGBPACK(25, 29, 26) :
                       i == selected ? LCD_BLACK
                                     : LCD_RGBPACK(195, 215, 201));
    }

    if (mode == MODE_CUES) {
        char focus[3];
        int color = hotcues[cue_slot] >= 0
                  ? cue_palette[hotcue_colors[cue_slot] & 7]
                  : LCD_RGBPACK(58, 76, 64);

        rb->snprintf(focus, sizeof(focus), "%02d", cue_slot + 1);
        rb->lcd_set_foreground(color);
        rb->lcd_drawrect(106, 66, 15, 14);
        text(108, 69, focus, LCD_WHITE);
    }
}

static void draw_tool_status(void)
{
    char line[96];
    char fitted[96];
    enum rbprep_tool tool = active_tool();
    int color = tool_menu_active ? LCD_WHITE : RBPREP_GREEN;
    int status_x = 123;

    if (tool == TOOL_SEEK)
        rb->snprintf(line, sizeof(line), "SEEK  %dms/tick", scrub_step_ms());
    else if (tool == TOOL_IPOD_SEEK)
        rb->snprintf(line, sizeof(line), "IPOD SEEK  WHEEL:GAIN  < >:SEEK");
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
        rb->snprintf(line, sizeof(line), "PLAY RPM %s  HOST %s",
                     rpm_styles[played_rpm_index], rpm_styles[host_rpm_index]);
    else if (tool == TOOL_PITCH_BEND) {
        char delta[16];
        format_percent_delta(delta, sizeof(delta), pitch_bend_x100);
        rb->snprintf(line, sizeof(line), "PITCH BEND %s%%", delta);
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
    else if (tool == TOOL_FAVORITE_ONE)
        format_favorite_tool_status(0, line, sizeof(line));
    else if (tool == TOOL_FAVORITE_TWO)
        format_favorite_tool_status(1, line, sizeof(line));
    else if (tool == TOOL_BURN_SONG)
        rb->snprintf(line, sizeof(line), "BURN TRACK  NOW");
    else if (tool == TOOL_BURN_ALL)
        rb->snprintf(line, sizeof(line), "BURN ALL  INSTANT");
    else if (tool == TOOL_PLAYLIST_PREVIOUS)
        rb->snprintf(line, sizeof(line), "PREV TRACK IN PLAYLIST");
    else if (tool == TOOL_PLAYLIST_NEXT)
        rb->snprintf(line, sizeof(line), "NEXT TRACK IN PLAYLIST");
    else if (tool == TOOL_RESTART_PLAYBACK)
        rb->snprintf(line, sizeof(line), "RESTART  PLAYBACK");
    else if (tool == TOOL_RELOAD_TRACK)
        rb->snprintf(line, sizeof(line), "RELOAD  CURRENT TRACK");
    else if (tool == TOOL_WAVEFORM_STYLE)
        rb->snprintf(line, sizeof(line), "RGB WAVEFORM");
    else if (tool == TOOL_KEYLOCK)
        rb->snprintf(line, sizeof(line), "PITCH LOCK  %s",
                     keylock_enabled ? "ON" : "OFF");
    else if (tool == TOOL_META_KEY) {
        char primary[24];
        char alternate[24];
        int index = key_index_from_name(selected_key);

        format_key_name(selected_key, primary, sizeof(primary));
        if (index < 0)
            rb->strlcpy(alternate, "--", sizeof(alternate));
        else
            rb->strlcpy(alternate,
                        key_notation == KEY_DISPLAY_CAMELOT
                        ? chromatic_key_names[index]
                        : camelot_key_names[index], sizeof(alternate));
        rb->snprintf(line, sizeof(line), "KEY  %s  [%s]",
                     primary, alternate);
    }
    else if (tool == TOOL_KEY_NOTATION)
        rb->snprintf(line, sizeof(line), "KEY DISPLAY  %s",
                     key_notation == KEY_DISPLAY_ORIGINAL ? "ORIGINAL" :
                     key_notation == KEY_DISPLAY_CAMELOT ? "CAMELOT" :
                                                          "CHROMATIC");
    else if (tool == TOOL_VIS_BOOMBOX)
        rb->snprintf(line, sizeof(line), "BOOMBOX  BASS");
    else if (tool == TOOL_VIS_EQ)
        rb->snprintf(line, sizeof(line), "20-BAND EQ");
    else if (tool == TOOL_VIS_TURNTABLE)
        rb->snprintf(line, sizeof(line), "OSCILLO-TURNTABLE");
    else if (tool == TOOL_VIS_CANYON)
        rb->snprintf(line, sizeof(line), "SPECTRAL CANYON");
    else if (tool == TOOL_VIS_ORBIT)
        rb->snprintf(line, sizeof(line), "STEREO ORBIT");
    else if (tool == TOOL_VIS_PHRASE)
        rb->snprintf(line, sizeof(line), "PHRASE MAP  8 BARS");
    else if (tool == TOOL_VIS_HARMONIC)
        rb->snprintf(line, sizeof(line), "HARMONIC CONSTELLATION");
    else if (tool == TOOL_MACRO_ONE || tool == TOOL_MACRO_TWO) {
        int slot = tool == TOOL_MACRO_ONE ? 0 : 1;
        rb->snprintf(line, sizeof(line), "M%d %.16s [%d]", slot + 1,
                     tool_macros[slot].name, tool_macros[slot].count);
    }
    else if (tool == TOOL_GRID_NUDGE)
        rb->snprintf(line, sizeof(line), "GRID NUDGE  %+dms", grid_offset);
    else if (tool == TOOL_GRID_BPM)
        rb->snprintf(line, sizeof(line), "GRID BPM  %d.%02d",
                     grid_bpm_x100 / 100, grid_bpm_x100 % 100);
    else if (tool == TOOL_GRID_ORIGIN)
        rb->snprintf(line, sizeof(line), "DOWNBEAT  SET");
    else if (tool == TOOL_GRID_QUANTIZE)
        rb->snprintf(line, sizeof(line), "QUANTIZE  %s",
                     quantize ? "ON" : "OFF");
    else if (tool == TOOL_CUE_SLOT) {
        rb->snprintf(line, sizeof(line), "CUE %02d  SLOT %s", cue_slot + 1,
                     hotcues[cue_slot] >= 0 ? "SET" : "EMPTY");
    } else if (tool == TOOL_CUE_MOVE)
        rb->snprintf(line, sizeof(line), "CUE %02d  MOVE", cue_slot + 1);
    else if (tool == TOOL_CUE_COLOR)
        rb->snprintf(line, sizeof(line), "CUE %02d  COLOR %s", cue_slot + 1,
                     cue_color_names[hotcue_colors[cue_slot] & 7]);
    else if (tool == TOOL_CUE_DELETE) {
        rb->snprintf(line, sizeof(line), "CUE %02d  DELETE", cue_slot + 1);
        color = LCD_RGBPACK(255, 90, 70);
    } else if (tool == TOOL_LOOP_LENGTH)
        rb->snprintf(line, sizeof(line), "LOOP SIZE  %s BEAT%s",
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
        rb->snprintf(line, sizeof(line), "LOOP ACTIVE  %s",
                     loop_active ? "ACTIVE" : "EXIT");
    else if (tool == TOOL_META_RATING)
        rb->snprintf(line, sizeof(line), "RATING  %d/5", rating);
    else if (tool == TOOL_META_COLOR)
        rb->snprintf(line, sizeof(line), "TRACK COLOR  %s",
                     color_labels[color_index]);
    else if (tool == TOOL_META_YEAR)
        rb->snprintf(line, sizeof(line), "YEAR  %04d", track_year);
    else if (tool == TOOL_META_GENRE)
        rb->snprintf(line, sizeof(line), "GENRE  %.30s", selected_genre);
    else
        rb->snprintf(line, sizeof(line), "%s", tool_names[tool]);
    if (macro_wheel_locked)
        rb->strlcat(line, " [LOCK/SEEK]", sizeof(line));
    if (!pnav_tool_available(tool))
        color = LCD_RGBPACK(38, 43, 40);
    fit_text(fitted, sizeof(fitted), line, 232 - status_x);
    text(status_x, 69, fitted, color);
}

static void draw_tool_indicators(void)
{
    int color = cue_audio_active() ? LCD_RGBPACK(255, 135, 35)
                                   : LCD_RGBPACK(82, 87, 84);

    draw_beat_phase();
    text(284, 69, "Q",
         quantize ? RBPREP_GREEN
                  : LCD_RGBPACK(105, 115, 108));
    text(299, 69, "CUE", color);
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
    draw_status_bar();
}

static void draw_settings(void)
{
    int row;
    int value_width;
    int value_x;
    const char *names[] = {
        "AUTOBOOT REKORDPOD", "AUTO-NEXT DEFAULT", "WAVEFORM SHAPE",
        "IPOD SEEK ORB", "KEY DISPLAY", "TRACK EXIT", "COMMIT POLICY",
        "WHEEL CLICK", "ACCENT COLOR", "IPOD BODY COLOR",
        "WHEEL / VINYL COLOR", "TURNTABLE ARM", "HEADSHELL DESIGN",
        "PLATTER WHEEL MODE"
    };
    const char *values[] = {
        autoboot_enabled ? "ON" : "OFF",
        autoplay_enabled ? "ON" : "OFF",
        waveform_half ? "HALF" : "FULL",
        ipod_seek_first ? "FIRST" : "LAST",
        key_notation == KEY_DISPLAY_ORIGINAL ? "ORIGINAL" :
        key_notation == KEY_DISPLAY_CAMELOT ? "CAMELOT" : "CHROMATIC",
        "ASK EACH TIME", "SAVE & LOAD", click_sound ? "ON" : "OFF",
        "HSB", "HSB", "HSB",
        turntable_arm_style ? "S-SHAPED" : "STRAIGHT",
        turntable_headshell_styles[turntable_headshell_style],
        platter_wheel_mode ? "ON" : "OFF"
    };
    const char *description;

    draw_blade_shell("REKORDPOD SETTINGS", RBPREP_GREEN);
    text(10, 29, "PLAYBACK  /  WORKFLOW  /  DISPLAY",
         LCD_RGBPACK(105, 125, 112));
    for (row = 0; row < (int)ARRAYLEN(names); row++) {
        int y = 33 + row * 11;
        if (row == settings_selection)
            draw_blade_selection(y - 1, 11, RBPREP_GREEN);
        rb->lcd_set_foreground(row == settings_selection
                               ? LCD_WHITE : RBPREP_GREEN);
        xlcd_fillcircle(17, y + 5, row == settings_selection ? 4 : 3);
        rb->lcd_set_foreground(row == settings_selection
                               ? LCD_BLACK : LCD_RGBPACK(10, 38, 23));
        xlcd_fillcircle(17, y + 5, 1);
        text(30, y, names[row], LCD_WHITE);
        rb->lcd_getstringsize(values[row], &value_width, NULL);
        value_x = MAX(192, LCD_WIDTH - value_width - 13);
        rb->lcd_set_foreground(row == settings_selection
                               ? LCD_RGBPACK(7, 25, 15)
                               : LCD_RGBPACK(17, 25, 20));
        rb->lcd_fillrect(value_x - 5, y, value_width + 10, 11);
        text(value_x, y, values[row], RBPREP_GREEN);
    }
    description = settings_selection == 0
         ? (autoboot_enabled ? "Launch Rekordpod automatically after boot"
                             : "Boot into the normal Rockbox main menu")
         : settings_selection == 1
         ? (autoplay_enabled ? "New deck sessions advance automatically"
                             : "New deck sessions stop after each track")
         : settings_selection == 2
         ? (waveform_half ? "One-sided RGB waveform" : "Mirrored RGB waveform")
         : settings_selection == 3
         ? (ipod_seek_first
            ? "Traditional iPod seek appears first on PLAYER"
            : "Traditional iPod seek appears last on PLAYER")
         : settings_selection == 4
         ? (key_notation == KEY_DISPLAY_ORIGINAL
            ? "Show rekordbox key text exactly as imported"
            : key_notation == KEY_DISPLAY_CAMELOT
            ? "Show Camelot equivalent; unknown keys stay original"
            : "Show flat chromatic equivalent; unknown keys stay original")
         : settings_selection == 5
         ? "Choose Save, Discard or Stay before another track loads"
         : settings_selection == 6
         ? "Save verifies the PDB + Rekordpod index before loading"
         : settings_selection == 7
         ? (click_sound ? "Audible feedback on clickwheel scroll"
                        : "Silent clickwheel navigation")
         : settings_selection == 8
         ? "Color used for controls, selections and accents"
         : settings_selection == 9
         ? "Color of the iPod body in the boot animation"
         : settings_selection == 10
         ? "Color of the boot record, wheel and record motifs"
         : settings_selection == 11
         ? "Choose a straight or classic S-shaped pickup arm"
         : settings_selection == 12
         ? "Technics, Concorde, M44, iPod shell, or wireless Phase"
         : (platter_wheel_mode
            ? "Touch, drag + momentum drive platter and seek"
            : "Discrete wheel events; touch physics isolated");
    rb->lcd_set_foreground(LCD_RGBPACK(13, 19, 16));
    rb->lcd_fillrect(8, 198, LCD_WIDTH - 16, 22);
    text(13, 204, description, LCD_RGBPACK(145, 165, 151));
    text(8, 226, "WHEEL: CHOOSE   SELECT: CHANGE   MENU: BACK", LCD_WHITE);
}

static void draw_accent_picker(void)
{
    char line[64];
    const char *labels[] = { "HUE", "SATURATION", "BRIGHTNESS" };
    int *hue;
    int *saturation;
    int *brightness;
    int values[3];
    int maximums[] = { 359, 100, 100 };
    int preview;
    int row;

    active_color_channels(&hue, &saturation, &brightness);
    values[0] = *hue;
    values[1] = *saturation;
    values[2] = *brightness;
    preview = hsb_rgb(*hue, *saturation, *brightness);
    draw_blade_shell(active_color_title(), RBPREP_GREEN);
    rb->lcd_set_foreground(preview);
    rb->lcd_fillrect(245, 34, 65, 10);
    for (row = 0; row < 3; row++) {
        int y = 52 + row * 48;
        text(12, y, labels[row], row == accent_component
             ? LCD_WHITE : LCD_RGBPACK(145, 165, 151));
        rb->snprintf(line, sizeof(line), "%d", values[row]);
        text(275, y, line, preview);
        rb->lcd_set_foreground(LCD_RGBPACK(28, 36, 31));
        rb->lcd_fillrect(12, y + 18, 296, 8);
        rb->lcd_set_foreground(preview);
        rb->lcd_fillrect(12, y + 18,
                         MAX(1, 296 * values[row] / maximums[row]), 8);
        if (row == accent_component) {
            rb->lcd_set_foreground(LCD_WHITE);
            rb->lcd_drawrect(10, y + 16, 300, 12);
        }
    }
    text(7, 216, "WHEEL: VALUE   LEFT/RIGHT: H S B", LCD_WHITE);
    text(7, 229, "SELECT: SAVE   MENU: CANCEL", LCD_LIGHTGRAY);
}

static void apply_usb_choice(int choice)
{
    activity_ticker_ping(choice == 0 ? 850 : 240);
    if (choice == 2 && !capabilities.usb_audio)
        choice = 0;
    usb_selection = MAX(0, MIN(capabilities.usb_audio ? 2 : 1, choice));
    usb_armed_selection = usb_selection;
#ifdef USB_ENABLE_HID
    rb->global_settings->usb_hid = false;
    rb->usb_set_hid(false);
#endif
#ifdef USB_ENABLE_AUDIO
    rb->global_settings->usb_audio = capabilities.usb_audio &&
                                     usb_selection == 2 ? 1 : 0;
    rb->usb_set_audio(rb->global_settings->usb_audio);
#endif
#if !defined(SIMULATOR) && !defined(USB_NONE) && \
    (defined(HAVE_USB_ADB) || defined(HAVE_USB_POWER))
    rb->global_settings->usb_mode = usb_selection == 1
                                  ? USB_MODE_MASS_STORAGE
                                  : USB_MODE_CHARGE;
    rb->usb_set_mode(rb->global_settings->usb_mode);
#endif
    rb->settings_save();
    activity_ticker_ping(1000);
}

static void draw_usb_mode(void)
{
    int row;
    int count = capabilities.usb_audio ? 3 : 2;
    const char *names[] = { "POWER ONLY", "DATA TRANSFER", "USB DAC" };

    draw_blade_shell("USB MODE", RBPREP_GREEN);
    for (row = 0; row < count; row++) {
        int y = 61 + row * 42;
        if (row == usb_selection)
            draw_blade_selection(y - 8, 28, RBPREP_GREEN);
        centered_text(12, LCD_WIDTH - 24, y, names[row],
                      row == usb_selection ? LCD_WHITE
                                           : LCD_RGBPACK(150, 164, 155));
    }
}

static void draw_genre_picker(void)
{
    int row;
    char name[32];
    char line[48];

    draw_blade_shell("GENRE ROLLUP", RBPREP_GREEN);
    rb->snprintf(line, sizeof(line), "%d LIBRARY GENRES", genre_count);
    text(185, RBPREP_STATUS_HEIGHT + 4, line,
         LCD_RGBPACK(105, 125, 112));
    for (row = 0; row < RBPREP_LIST_ROWS; row++) {
        int ordinal = genre_top + row;
        int y = 34 + row * 19;
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

static void wrap_main_wheel_phase(void)
{
    const int cycle = 64 * 256;

    main_wheel_phase_fp %= cycle;
    if (main_wheel_phase_fp < 0)
        main_wheel_phase_fp += cycle;
}

static bool main_wheel_motion_active(void)
{
    if (!TIME_BEFORE(activity_ticker_until, *rb->current_tick))
        return true;
#ifdef HAVE_WHEEL_POSITION
    return platter_wheel_mode &&
           (rb->wheel_status() >= 0 || main_wheel_touch_position >= 0 ||
            ABS(main_wheel_velocity_fp) >= 96);
#else
    return false;
#endif
}

static void update_main_wheel_motion(void)
{
#ifdef HAVE_WHEEL_POSITION
    long now = *rb->current_tick;
    long elapsed = now - main_wheel_motion_tick;
    int position = rb->wheel_status();

    if (!platter_wheel_mode) {
        main_wheel_touch_position = -1;
        main_wheel_velocity_fp = 0;
        main_wheel_motion_tick = now;
        return;
    }
    if (elapsed <= 0)
        return;
    elapsed = MIN(elapsed, MAX(1, HZ / 4));
    if (position >= 0) {
        if (main_wheel_touch_position >= 0) {
            int delta = position - main_wheel_touch_position;
            int movement;
            int instantaneous;

            if (delta > 48)
                delta -= 96;
            else if (delta < -48)
                delta += 96;
            movement = delta * 512 / 3; /* 96 touch units = one turn. */
            main_wheel_phase_fp += movement;
            if (delta) {
                instantaneous = movement * HZ / elapsed;
                main_wheel_velocity_fp =
                    (main_wheel_velocity_fp + instantaneous * 2) / 3;
            } else {
                main_wheel_velocity_fp = main_wheel_velocity_fp * 3 / 4;
            }
        }
        main_wheel_touch_position = position;
    } else {
        main_wheel_touch_position = -1;
        if (ABS(main_wheel_velocity_fp) >= 96) {
            int damping = MIN(224, (int)(elapsed * 700 / MAX(1, HZ)));

            main_wheel_phase_fp +=
                (long long)main_wheel_velocity_fp * elapsed / HZ;
            main_wheel_velocity_fp =
                (long long)main_wheel_velocity_fp * (256 - damping) / 256;
        } else {
            main_wheel_velocity_fp = 0;
        }
    }
    wrap_main_wheel_phase();
    main_wheel_motion_tick = now;
#endif
}

static void draw_main_menu_fx(void)
{
    int cx = LCD_WIDTH / 2;
    int cy = 169;
    int phase;
    int point;

    update_main_wheel_motion();
    phase = (main_wheel_phase_fp >> 8) & 63;

    /* A centered platter anchors the carousel. Its motion remains a trace of
       real wheel energy rather than a synthetic selection-position dial. */
    rb->lcd_set_foreground(LCD_BLACK);
    xlcd_fillcircle(cx, cy, 54);
    rb->lcd_set_foreground(RBPREP_GREEN_DIM);
    xlcd_drawcircle(cx, cy, 53);
    rb->lcd_set_foreground(theme_wheel_outline);
    xlcd_drawcircle(cx, cy, 51);
    rb->lcd_set_foreground(theme_wheel);
    xlcd_fillcircle(cx, cy, 48);
    rb->lcd_set_foreground(LCD_RGBPACK(40, 48, 43));
    xlcd_drawcircle(cx, cy, 45);
    xlcd_drawcircle(cx, cy, 40);
    rb->lcd_set_foreground(RBPREP_GREEN_DIM);
    xlcd_drawcircle(cx, cy, 32);
    for (point = 0; point < 16; point++) {
        int angle = (point * 4 + phase) & 63;
        int x = cx + wheel_cosine[angle] * 49 / 256;
        int y = cy + wheel_sine[angle] * 49 / 256;

        rb->lcd_set_foreground(point % 4 == 0 ? LCD_WHITE
                                              : RBPREP_GREEN);
        if ((point & 3) == 0)
            xlcd_fillcircle(x, y, 2);
        else
            rb->lcd_drawpixel(x, y);
    }
#ifdef HAVE_WHEEL_POSITION
    if (platter_wheel_mode && rb->wheel_status() >= 0) {
        /* Align the hardware wheel origin with the drawing table. */
        int touch_angle = (rb->wheel_status() * 64 / 96 + 48) & 63;
        int touch_x = cx + wheel_cosine[touch_angle] * 43 / 256;
        int touch_y = cy + wheel_sine[touch_angle] * 43 / 256;

        rb->lcd_set_foreground(LCD_BLACK);
        xlcd_fillcircle(touch_x, touch_y, 3);
        rb->lcd_set_foreground(theme_accent);
        xlcd_fillcircle(touch_x, touch_y, 2);
        rb->lcd_set_foreground(LCD_WHITE);
        rb->lcd_drawpixel(touch_x, touch_y);
    }
#endif
    point = phase;
    rb->lcd_set_foreground(LCD_WHITE);
    rb->lcd_drawline(cx, cy,
        cx + wheel_cosine[point] * 37 / 256,
        cy + wheel_sine[point] * 37 / 256);
    rb->lcd_set_foreground(LCD_RGBPACK(5, 14, 10));
    xlcd_fillcircle(cx, cy, 17);
    rb->lcd_set_foreground(RBPREP_GREEN);
    xlcd_drawcircle(cx, cy, 17);
    rb->lcd_set_foreground(LCD_RGBPACK(35, 205, 255));
    xlcd_drawcircle(cx, cy, 12);
    {
        char marquee[9];
        int length = rb->strlen(device_usb_name);
        int visible = 7;
        int start = 0;
        int index;

        if (length > visible)
            start = (*rb->current_tick / MAX(1, HZ / 5)) % (length + 3);
        for (index = 0; index < visible; index++) {
            int source = start + index;

            if (length <= visible)
                marquee[index] = index < length ? device_usb_name[index] : ' ';
            else if (source < length)
                marquee[index] = device_usb_name[source];
            else if (source < length + 3)
                marquee[index] = ' ';
            else
                marquee[index] = device_usb_name[source - length - 3];
        }
        marquee[visible] = '\0';
        centered_text(cx - 24, 48, cy - 5, marquee, LCD_WHITE);
    }
    rb->lcd_set_foreground(LCD_WHITE);
    xlcd_fillcircle(cx, cy + 9, 2);
    main_name_scroll_deadline = *rb->current_tick + MAX(1, HZ / 5);
}

static const char * const main_menu_items[] = {
    "COLLECTION", "SHUFFLE COLLECTION", "PLAYLISTS", "PREP DECK",
    "USB MODE", "REKORDPOD SETTINGS", "INDEX STATUS", "EXIT TO ROCKBOX"
};

static void draw_cdj_face_button(int x, int y, int width, int height,
                                 const char *label, int color, bool lit)
{
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(x - 1, y + 2, width + 2, height + 1);
    rb->lcd_set_foreground(lit ? LCD_RGBPACK(42, 46, 43)
                               : LCD_RGBPACK(20, 22, 21));
    rb->lcd_fillrect(x, y, width, height);
    rb->lcd_set_foreground(LCD_RGBPACK(125, 130, 127));
    rb->lcd_drawrect(x, y, width, height);
    rb->lcd_set_foreground(LCD_RGBPACK(54, 58, 55));
    rb->lcd_hline(x + 2, x + width - 3, y + height - 2);
    rb->lcd_set_foreground(lit ? LCD_RGBPACK(112, 118, 114)
                               : LCD_RGBPACK(56, 65, 59));
    rb->lcd_drawrect(x + 2, y + 2, width - 4, height - 4);
    if (lit) {
        rb->lcd_set_foreground(color);
        rb->lcd_hline(x + 4, x + width - 5, y + height - 3);
    }
    if (label && label[0])
        centered_text(x, width, y + MAX(1, (height - 8) / 2), label,
                      lit ? LCD_WHITE : LCD_RGBPACK(154, 160, 156));
}

static void draw_cdj_round_button(int cx, int cy, int radius,
                                  const char *label, int color, bool lit,
                                  bool play_symbol)
{
    int line;

    if (label && label[0])
        centered_text(cx - radius - 8, radius * 2 + 16,
                      cy - radius - 10, label,
                      LCD_RGBPACK(175, 180, 177));
    rb->lcd_set_foreground(LCD_BLACK);
    xlcd_fillcircle(cx + 1, cy + 2, radius + 2);
    rb->lcd_set_foreground(LCD_RGBPACK(118, 123, 120));
    xlcd_fillcircle(cx, cy, radius + 1);
    rb->lcd_set_foreground(lit ? color : LCD_RGBPACK(42, 46, 43));
    xlcd_fillcircle(cx, cy, radius);
    rb->lcd_set_foreground(LCD_RGBPACK(12, 14, 13));
    xlcd_fillcircle(cx, cy, MAX(2, radius - 3));
    rb->lcd_set_foreground(lit ? LCD_WHITE : LCD_RGBPACK(158, 164, 160));
    if (play_symbol) {
        for (line = -4; line <= 4; line++)
            rb->lcd_hline(cx - 2, cx + 3 - ABS(line), cy + line);
    } else {
        xlcd_fillcircle(cx, cy, MAX(1, radius / 4));
        rb->lcd_hline(cx - radius / 2, cx + radius / 2, cy + radius / 2);
    }
}

static void draw_cdj_hotcue_pad(int x, int y, int width, const char *label,
                                int color, bool lit)
{
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(x + 1, y + 2, width, 9);
    rb->lcd_set_foreground(LCD_RGBPACK(100, 105, 102));
    rb->lcd_drawrect(x, y, width, 9);
    rb->lcd_set_foreground(lit ? color : LCD_RGBPACK(24, 27, 25));
    rb->lcd_fillrect(x + 2, y + 2, width - 4, 5);
    rb->lcd_set_foreground(color);
    rb->lcd_hline(x + 2, x + width - 3, y + 7);
    if (label && label[0])
        text(x + 3, y, label,
             lit ? LCD_WHITE : LCD_RGBPACK(150, 156, 152));
}

static void draw_cdj_encoder(int cx, int cy, bool lit)
{
    int color = lit ? theme_accent : LCD_RGBPACK(120, 126, 122);

    rb->lcd_set_foreground(LCD_BLACK);
    xlcd_fillcircle(cx + 1, cy + 2, 14);
    rb->lcd_set_foreground(LCD_RGBPACK(148, 153, 150));
    xlcd_fillcircle(cx, cy, 13);
    rb->lcd_set_foreground(LCD_RGBPACK(40, 43, 41));
    xlcd_fillcircle(cx, cy, 10);
    rb->lcd_set_foreground(LCD_RGBPACK(118, 124, 120));
    xlcd_drawcircle(cx, cy, 8);
    rb->lcd_set_foreground(color);
    rb->lcd_drawline(cx, cy - 7, cx + 2, cy - 4);
    rb->lcd_set_foreground(LCD_RGBPACK(210, 214, 211));
    xlcd_fillcircle(cx, cy, 3);
}

static void draw_cdj_tempo_fader(int glow)
{
    const int rail_x = 250;
    const int rail_top = 148;
    const int rail_bottom = 220;
    const int slider_y = 182;

    rb->lcd_set_foreground(LCD_RGBPACK(19, 21, 20));
    rb->lcd_fillrect(244, rail_top - 3, 14, rail_bottom - rail_top + 7);
    rb->lcd_set_foreground(LCD_RGBPACK(73, 78, 75));
    rb->lcd_drawrect(244, rail_top - 3, 14, rail_bottom - rail_top + 7);
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(rail_x - 1, rail_top, 3, rail_bottom - rail_top + 1);
    rb->lcd_set_foreground(LCD_RGBPACK(95, 101, 97));
    rb->lcd_vline(rail_x, rail_top, rail_bottom);
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(239, slider_y - 3, 22, 9);
    rb->lcd_set_foreground(glow == 6 ? theme_accent
                                     : LCD_RGBPACK(142, 148, 144));
    rb->lcd_fillrect(241, slider_y - 2, 18, 6);
    rb->lcd_set_foreground(LCD_RGBPACK(42, 45, 43));
    rb->lcd_hline(243, 257, slider_y + 1);
    rb->lcd_set_foreground(LCD_RGBPACK(94, 100, 96));
    rb->lcd_hline(238, 242, rail_top + 3);
    rb->lcd_hline(238, 242, slider_y);
    rb->lcd_hline(238, 242, rail_bottom - 3);
    rb->lcd_set_foreground(glow == 6 ? theme_accent
                                     : LCD_RGBPACK(52, 58, 54));
    xlcd_fillcircle(250, 228, 3);
}

static void draw_booth_outline_knob(int cx, int cy, int radius)
{
    rb->lcd_set_foreground(LCD_RGBPACK(73, 78, 75));
    xlcd_drawcircle(cx, cy, radius);
    if (radius > 4)
        xlcd_drawcircle(cx, cy, radius - 2);
    rb->lcd_drawline(cx, cy - radius + 1, cx + 1, cy - 2);
}

static void draw_main_booth_shell(void)
{
    /* One coherent center chassis on black. Keeping the neighboring hardware
       out of this 320-pixel canvas lets the menu screen and real clickwheel
       trace remain the only visual anchors. */
    rb->lcd_set_foreground(LCD_RGBPACK(10, 11, 11));
    rb->lcd_fillrect(56, 9, 209, LCD_HEIGHT - 9);
    rb->lcd_set_foreground(LCD_RGBPACK(67, 72, 69));
    rb->lcd_drawrect(56, 9, 209, LCD_HEIGHT - 9);
    rb->lcd_hline(58, 262, 12);
    rb->lcd_vline(59, 13, LCD_HEIGHT - 2);
    rb->lcd_vline(262, 13, LCD_HEIGHT - 2);
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_drawpixel(56, 9);
    rb->lcd_drawpixel(264, 9);
    rb->lcd_set_foreground(LCD_RGBPACK(93, 98, 95));
    xlcd_drawcircle(63, 16, 2);
    xlcd_drawcircle(257, 16, 2);
    xlcd_drawcircle(63, 232, 2);
    xlcd_drawcircle(257, 232, 2);
    rb->lcd_set_foreground(LCD_RGBPACK(42, 46, 43));
    rb->lcd_hline(60, 261, 86);
    rb->lcd_vline(102, 88, 228);
    rb->lcd_vline(221, 88, 228);
}

static void draw_main_cdj_controls(int glow)
{
    static const int cue_colors[] = {
        LCD_RGBPACK(255, 70, 62), LCD_RGBPACK(55, 225, 245),
        LCD_RGBPACK(238, 215, 62), LCD_RGBPACK(75, 125, 255)
    };
    int index;

    for (index = 0; index < 4; index++)
        draw_cdj_face_button(82 + index * 40, 10, 37, 11, "",
                             theme_accent,
                             glow == index * 2);

    /* Unlabeled utility glyphs preserve the real control hierarchy when the
       full-size legends would be smaller than a readable LCD pixel. */
    rb->lcd_set_foreground(LCD_RGBPACK(185, 191, 187));
    for (index = 0; index < 4; index++) {
        int cx = 100 + index * 40;
        rb->lcd_hline(cx - 4, cx + 4, 14);
        rb->lcd_drawpixel(cx, 12);
        if (index & 1)
            rb->lcd_vline(cx, 13, 17);
    }

    rb->lcd_set_foreground(LCD_RGBPACK(24, 27, 25));
    rb->lcd_fillrect(60, 23, 37, 61);
    rb->lcd_set_foreground(LCD_RGBPACK(70, 75, 72));
    rb->lcd_drawrect(60, 23, 37, 61);
    for (index = 0; index < 4; index++)
        draw_cdj_hotcue_pad(64, 31 + index * 12, 28, "",
                            cue_colors[index], glow == index * 2);
    rb->lcd_set_foreground(LCD_RGBPACK(86, 92, 88));
    rb->lcd_hline(64, 91, 78);

    rb->lcd_set_foreground(LCD_RGBPACK(60, 65, 62));
    rb->lcd_drawrect(225, 23, 38, 67);
    draw_cdj_encoder(249, 49, glow == 1);
    draw_cdj_face_button(231, 69, 29, 11, "", LCD_WHITE, glow == 3);
    rb->lcd_set_foreground(LCD_RGBPACK(180, 186, 182));
    rb->lcd_drawline(239, 74, 246, 74);
    rb->lcd_drawline(239, 74, 242, 71);
    rb->lcd_drawline(239, 74, 242, 77);
    rb->lcd_set_foreground(glow == 5 ? theme_accent
                                     : LCD_RGBPACK(70, 76, 72));
    xlcd_fillcircle(249, 85, 3);

    rb->lcd_set_foreground(LCD_RGBPACK(53, 58, 55));
    rb->lcd_drawrect(56, 84, 47, 28);
    draw_cdj_round_button(69, 102, 7, "", LCD_RGBPACK(255, 157, 42),
                          glow == 0, false);
    draw_cdj_round_button(91, 102, 7, "", LCD_RGBPACK(255, 157, 42),
                          glow == 1, false);
    rb->lcd_set_foreground(LCD_RGBPACK(220, 225, 221));
    rb->lcd_vline(68, 98, 105);
    rb->lcd_hline(68, 72, 98);
    rb->lcd_vline(92, 98, 105);
    rb->lcd_hline(88, 92, 105);

    draw_cdj_round_button(70, 127, 5, "", LCD_WHITE, false, false);
    draw_cdj_round_button(87, 127, 5, "", LCD_WHITE, false, false);
    draw_cdj_round_button(70, 142, 5, "", LCD_WHITE, false, false);
    draw_cdj_round_button(87, 142, 5, "", LCD_WHITE, false, false);
    rb->lcd_set_foreground(LCD_RGBPACK(172, 178, 174));
    rb->lcd_drawline(68, 127, 72, 124);
    rb->lcd_drawline(68, 127, 72, 130);
    rb->lcd_drawline(89, 127, 85, 124);
    rb->lcd_drawline(89, 127, 85, 130);
    rb->lcd_drawline(68, 142, 72, 139);
    rb->lcd_drawline(68, 142, 72, 145);
    rb->lcd_drawline(89, 142, 85, 139);
    rb->lcd_drawline(89, 142, 85, 145);
    draw_cdj_round_button(79, 174, 9, "", LCD_RGBPACK(255, 132, 26),
                          glow == 2, false);
    draw_cdj_round_button(79, 211, 10, "", theme_accent,
                          glow == 4, true);

    rb->lcd_set_foreground(glow == 7 ? theme_accent
                                     : LCD_RGBPACK(75, 81, 77));
    xlcd_fillcircle(249, 104, 3);
    draw_cdj_face_button(231, 111, 29, 10, "", LCD_WHITE,
                         glow == 7);
    rb->lcd_set_foreground(LCD_RGBPACK(185, 191, 187));
    xlcd_drawcircle(245, 116, 3);
    rb->lcd_drawpixel(245, 116);
    draw_booth_outline_knob(238, 130, 4);
    draw_booth_outline_knob(253, 130, 4);
    draw_cdj_tempo_fader(glow);
}

static void format_main_screen_label(char *dest, size_t size,
                                     const char *source, int visible,
                                     bool animate)
{
    int length = rb->strlen(source);
    int start = 0;
    int index;

    visible = MIN(visible, (int)size - 1);
    if (length > visible && animate)
        start = (*rb->current_tick / MAX(1, HZ / 5)) % (length + 3);
    for (index = 0; index < visible; index++) {
        int offset = start + index;

        if (length <= visible)
            dest[index] = index < length ? source[index] : ' ';
        else if (!animate)
            dest[index] = index < length ? source[index] : ' ';
        else if (offset < length)
            dest[index] = source[offset];
        else if (offset < length + 3)
            dest[index] = ' ';
        else
            dest[index] = source[offset - length - 3];
    }
    dest[visible] = '\0';
}

static void draw_main_cdj_screen(void)
{
    const int screen_x = RBPREP_MAIN_SCREEN_X;
    const int screen_y = RBPREP_MAIN_SCREEN_Y;
    const int screen_w = RBPREP_MAIN_SCREEN_W;
    const int screen_h = RBPREP_MAIN_SCREEN_H;
    int top = MAX(0, MIN((int)ARRAYLEN(main_menu_items) - 3,
                         selection - 1));
    int row;
    char counter[8];

    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(screen_x - 5, screen_y - 3,
                     screen_w + 10, screen_h + 7);
    rb->lcd_set_foreground(LCD_RGBPACK(108, 114, 110));
    rb->lcd_drawrect(screen_x - 4, screen_y - 3,
                     screen_w + 8, screen_h + 6);
    rb->lcd_set_foreground(LCD_RGBPACK(35, 39, 36));
    rb->lcd_drawrect(screen_x - 2, screen_y - 1,
                     screen_w + 4, screen_h + 2);
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_fillrect(screen_x, screen_y, screen_w, screen_h);
    rb->lcd_set_foreground(LCD_RGBPACK(20, 30, 24));
    rb->lcd_fillrect(screen_x + 1, screen_y + 1, screen_w - 2, 10);
    text(screen_x + 4, screen_y + 2, "BROWSE", LCD_WHITE);
    rb->snprintf(counter, sizeof(counter), "%d/8", selection + 1);
    text(screen_x + screen_w - 23, screen_y + 2, counter,
         LCD_RGBPACK(155, 168, 159));
    for (row = 0; row < 3; row++) {
        int item = top + row;
        int y = screen_y + 12 + row * 14;
        bool selected_row = item == selection;
        char fitted[16];

        if (selected_row) {
            rb->lcd_set_foreground(hsb_rgb(
                accent_hue, MIN(100, accent_saturation + 10),
                MAX(16, accent_brightness * 34 / 100)));
            rb->lcd_fillrect(screen_x + 2, y, screen_w - 4, 13);
            rb->lcd_set_foreground(theme_accent);
            rb->lcd_fillrect(screen_x + 2, y, 2, 13);
        }
        draw_main_glyph(screen_x + 9, y + 6, item, selected_row);
        format_main_screen_label(fitted, sizeof(fitted),
                                 main_menu_items[item], 14, selected_row);
        text(screen_x + 16, y + 2, fitted,
             selected_row ? LCD_WHITE : LCD_RGBPACK(146, 160, 151));
        if (selected_row)
            text(screen_x + screen_w - 8, y + 2, ">", theme_accent);
    }
    rb->lcd_set_foreground(LCD_RGBPACK(83, 90, 86));
    rb->lcd_drawrect(screen_x, screen_y, screen_w, screen_h);
}

static void navigate_main_menu(int direction)
{
    int next = MAX(0, MIN(7, selection + direction));

    /* The platter is an input trace, not a selection-position dial. Every
       physical wheel event rotates it, even at the first or last menu row. */
#ifdef HAVE_WHEEL_POSITION
    if (!platter_wheel_mode || main_wheel_touch_position < 0)
#endif
    {
        main_wheel_phase_fp += direction > 0 ? 4 * 256 : -4 * 256;
        wrap_main_wheel_phase();
    }
    if (next != selection) {
        selection = next;
        main_name_scroll_deadline = *rb->current_tick;
    }
}

static void draw_scaled_transition_thumbnail(int x, int y,
                                              int width, int height,
                                              int source_x, int source_y,
                                              int source_width,
                                              int source_height)
{
    int row;
    int column;

    if (!main_viewport || !main_transition_thumbnail_valid ||
        width <= 0 || height <= 0 ||
        source_width <= 0 || source_height <= 0)
        return;
    width = MIN(width, LCD_WIDTH - x);
    height = MIN(height, LCD_HEIGHT - y);
    for (column = 0; column < width; column++)
        main_transition_x_lut[column] = MIN(RBPREP_RETURN_THUMB_W - 1,
            source_x + column * source_width / width);
    for (row = 0; row < height; row++)
        main_transition_y_lut[row] = MIN(RBPREP_RETURN_THUMB_H - 1,
            source_y + row * source_height / height);

    /* Coordinate division happens once per row/column, not once per pixel.
       That removes roughly 76,000 divides from every full transition frame
       on a 320x240 target and leaves the LCD transfer as the main cost. */
    for (row = 0; row < height; row++) {
        int source_row = main_transition_y_lut[row] *
                         RBPREP_RETURN_THUMB_W;

        for (column = 0; column < width; column++)
            *FBADDRBUF(main_viewport->buffer, x + column, y + row) =
                main_transition_thumbnail[source_row +
                                          main_transition_x_lut[column]];
    }
}

static void capture_main_transition_thumbnail(void)
{
    int y;

    main_transition_thumbnail_valid = main_viewport != NULL;
    if (main_viewport) {
        for (y = 0; y < RBPREP_RETURN_THUMB_H; y++) {
            int sy = y * LCD_HEIGHT / RBPREP_RETURN_THUMB_H;
            int x;

            for (x = 0; x < RBPREP_RETURN_THUMB_W; x++) {
                int sx = x * LCD_WIDTH / RBPREP_RETURN_THUMB_W;

                main_transition_thumbnail[y * RBPREP_RETURN_THUMB_W + x] =
                    *FBADDRBUF(main_viewport->buffer, sx, sy);
            }
        }
    }
}

static void clear_transition_ticker(void)
{
    activity_ticker_until = 0;
    activity_ticker_last_update = 0;
    activity_ticker_burst = 0;
}

static void draw_main_dissolve_mask(int reveal)
{
    /* This is the original menu fade pattern. On a display without alpha,
       the ordered 4x4 threshold matrix produces a crisp, deterministic
       dissolve without per-pixel color arithmetic or another framebuffer. */
    static const unsigned char bayer[4][4] = {
        { 0,  8,  2, 10 }, { 12, 4, 14, 6 },
        { 3, 11,  1,  9 }, { 15, 7, 13, 5 }
    };
    int x;
    int y;

    reveal = MAX(0, MIN(RBPREP_MAIN_DISSOLVE_STEPS, reveal));
    rb->lcd_set_foreground(LCD_BLACK);
    for (y = 0; y < LCD_HEIGHT; y++)
        for (x = 0; x < LCD_WIDTH; x++)
            if (bayer[y & 3][x & 3] >= reveal)
                rb->lcd_drawpixel(x, y);
}

static void animate_main_dissolve(bool reveal)
{
    int frame;

    for (frame = 0; frame <= RBPREP_MAIN_DISSOLVE_STEPS; frame++) {
        int amount = reveal ? frame :
                     RBPREP_MAIN_DISSOLVE_STEPS - frame;

        /* Fade-out is monotonic, so preserve the exact live framebuffer and
           add black pixels to it. Fade-in must restore newly revealed pixels
           each step, so it redraws from the captured destination image. */
        if (reveal) {
            rb->lcd_clear_display();
            if (main_transition_thumbnail_valid)
                draw_scaled_transition_thumbnail(
                    0, 0, LCD_WIDTH, LCD_HEIGHT, 0, 0,
                    RBPREP_RETURN_THUMB_W, RBPREP_RETURN_THUMB_H);
        }
        draw_main_dissolve_mask(amount);
        rb->lcd_update();
        if (frame < RBPREP_MAIN_DISSOLVE_STEPS)
            rb->sleep(RBPREP_MAIN_TRANSITION_FRAME_TICKS);
    }
}

static void capture_transition_destination(void)
{
    /* Render the new mode into the framebuffer without submitting it first;
       the dissolve owns the first visible presentation of that screen. */
    clear_transition_ticker();
    transition_render_only = true;
    force_full_redraw = true;
    draw_screen();
    transition_render_only = false;
    capture_main_transition_thumbnail();
    force_full_redraw = true;
}

static void present_transition_destination(void)
{
    /* Replace the half-resolution dissolve source with the native render in
       the same turn, so the final phase cannot linger as a softened frame. */
    main_transition_thumbnail_valid = false;
    transition_render_only = false;
    force_full_redraw = true;
    draw_screen();
}

static void draw_main_menu(void)
{
    int glow = (*rb->current_tick / MAX(1, HZ / 5)) & 7;

    rb->lcd_set_foreground(LCD_RGBPACK(7, 8, 8));
    rb->lcd_fillrect(0, 0, LCD_WIDTH, LCD_HEIGHT);
    draw_main_booth_shell();
    draw_main_cdj_screen();
    draw_main_cdj_controls(glow);
    /* The jog artwork is deliberately isolated from the CDJ face revision.
       Its center, radius, touch marker and momentum rendering stay unchanged. */
    draw_main_menu_fx();
}

static void animate_main_launch_transition(void)
{
    clear_transition_ticker();
    animate_main_dissolve(false);
}

static void animate_main_return_transition(void)
{
    clear_transition_ticker();
    animate_main_dissolve(false);
    capture_transition_destination();
    animate_main_dissolve(true);
    present_transition_destination();
}

static void draw_pending_edits(void)
{
    char line[96];
    int row;

    draw_blade_shell("PENDING EDITS", RBPREP_GREEN);
    if (pending_summary_overflow || pending_journal_invalid) {
        rb->snprintf(line, sizeof(line), "BURN BLOCKED: %s",
                     pending_summary_overflow ? "TOO MANY CHANGES"
                                              : "JOURNAL INCOMPLETE");
        text(7, 34, line, LCD_RGBPACK(255, 90, 70));
    } else {
        rb->snprintf(line, sizeof(line),
                     "%d SAVED STATES   %d PLAYLIST OPS",
                     pending_snapshot_count, pending_playlist_count);
        text(7, 34, line, LCD_RGBPACK(145, 165, 151));
    }
    rb->lcd_set_foreground(LCD_RGBPACK(25, 31, 27));
    rb->lcd_hline(7, LCD_WIDTH - 8, 49);
    for (row = 0; row < RBPREP_PENDING_ROWS; row++) {
        int visible = pending_top + row;
        int y = 55 + row * 19;

        if (visible >= pending_snapshot_count + pending_playlist_count)
            break;
        if (visible == pending_selection)
            draw_blade_selection(y - 2, 19, RBPREP_GREEN);
        if (visible < pending_snapshot_count) {
            int ordinal = pending_snapshot_count - 1 - visible;
            struct rbprep_pending_entry *entry = &pending_entries[ordinal];

            rb->lcd_set_foreground(track_color_display(entry->color));
            if (normalize_track_color(entry->color) ==
                    RBPREP_TRACK_COLOR_NONE)
                rb->lcd_drawrect(7, y + 2, 6, 6);
            else
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
            rb->snprintf(line, sizeof(line), "%s  %.31s",
                         entry->operation == PLAYLIST_OP_ADD ? "ADD TO" :
                         entry->operation == PLAYLIST_OP_CREATE ? "CREATE" :
                         entry->operation == PLAYLIST_OP_RENAME ? "RENAME" :
                         entry->operation == PLAYLIST_OP_MOVE ? "MOVE" :
                         "DELETE", entry->name);
            text(19, y, line, LCD_WHITE);
            if (entry->operation == PLAYLIST_OP_ADD)
                rb->snprintf(line, sizeof(line), "TRACK %lu   PLAYLIST %lu",
                             (unsigned long)entry->track_id,
                             (unsigned long)entry->playlist_id);
            else
                rb->snprintf(line, sizeof(line), "PLAYLIST %lu   PARENT %lu",
                             (unsigned long)entry->playlist_id,
                             (unsigned long)entry->parent_id);
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
    int row;

    draw_blade_shell("INDEX STATUS", RBPREP_GREEN);
    text(9, 30, library_fd >= 0 ? "DEVICE INDEX ONLINE" : "INDEX NOT FOUND",
         library_fd >= 0 ? RBPREP_GREEN
                         : LCD_RGBPACK(255, 90, 70));
    for (row = 0; row < 4; row++) {
        int y = 49 + row * 25;
        rb->lcd_set_foreground(LCD_RGBPACK(13, 20, 16));
        rb->lcd_fillrect(8, y, LCD_WIDTH - 16, 20);
        rb->lcd_set_foreground(row == 0 ? RBPREP_GREEN
                                        : LCD_RGBPACK(45, 72, 55));
        rb->lcd_fillrect(8, y, 3, 20);
    }
    text(17, 54, "TRACKS IN INDEX", LCD_RGBPACK(145, 165, 151));
    rb->snprintf(line, sizeof(line), "%lu",
                 (unsigned long)library_track_count);
    text(275, 54, line, LCD_WHITE);
    text(17, 79, "ADDED IN LAST 30 DAYS", LCD_RGBPACK(145, 165, 151));
    rb->snprintf(line, sizeof(line), "%d", recent_tracks_30d);
    text(275, 79, line, LCD_WHITE);
    text(17, 104, "TOTAL PLAYS  (> 1 MIN)", LCD_RGBPACK(145, 165, 151));
    rb->snprintf(line, sizeof(line), "%d", total_plays);
    text(275, 104, line, LCD_WHITE);
    text(17, 129, "TRACKS BURNED THIS UPTIME",
         LCD_RGBPACK(145, 165, 151));
    rb->snprintf(line, sizeof(line), "%d", uptime_tracks_burned);
    text(275, 129, line, LCD_WHITE);
    rb->snprintf(line, sizeof(line), "%lu PLAYLIST / FOLDER NODES",
                 (unsigned long)library_node_count);
    text(9, 158, line, LCD_WHITE);
    rb->snprintf(line, sizeof(line), "%lu PLAYLIST MEMBERS",
                 (unsigned long)library_member_count);
    text(9, 178, line, LCD_WHITE);
    text(9, 204, "PLAYLIST CHANGES BURN LIVE", RBPREP_GREEN);
    text(7, 226, "MENU: BACK", LCD_LIGHTGRAY);
}

static void discard_staged_edit(void)
{
    if (staged_tool == TOOL_CUE_COLOR) {
        if (staged_cue_slot >= 0 && staged_cue_slot < 16)
            hotcue_colors[staged_cue_slot] = staged_original;
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
    } else if (staged_tool == TOOL_META_KEY) {
        rb->strlcpy(selected_key, staged_original_key,
                    sizeof(selected_key));
    }
    staged_tool = -1;
    staged_cue_slot = -1;
}

static void stage_active_edit(void)
{
    enum rbprep_tool tool = active_tool();

    if (staged_tool == tool &&
        (tool != TOOL_CUE_COLOR || staged_cue_slot == cue_slot))
        return;
    discard_staged_edit();
    staged_tool = tool;
    if (tool == TOOL_CUE_COLOR) {
        staged_cue_slot = cue_slot;
        staged_original = hotcue_colors[cue_slot];
    } else if (tool == TOOL_GRID_NUDGE)
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
    else if (tool == TOOL_META_KEY)
        rb->strlcpy(staged_original_key, selected_key,
                    sizeof(staged_original_key));
    else
        staged_tool = -1;
}

static void begin_confirmation(enum rbprep_confirm_action action)
{
    confirm_action = action;
    confirm_active = true;
    confirm_ok = true;
    confirm_choice = 1;
    confirm_wait_release = !!(rb->button_status() & BUTTON_SELECT);
    force_full_redraw = true;
}

static void begin_track_load_confirmation(int index, int row,
                                          enum rbprep_mode return_mode,
                                          bool force_reload)
{
    deferred_track_index = index;
    deferred_track_row = row;
    deferred_track_return_mode = return_mode;
    deferred_track_force_reload = force_reload;
    rb->snprintf(confirm_message, sizeof(confirm_message),
                 "CURRENT TRACK HAS CHANGES");
    confirm_action = CONFIRM_TRACK_LOAD;
    confirm_active = true;
    confirm_choice = 0;
    confirm_ok = true;
    confirm_wait_release = !!(rb->button_status() & BUTTON_SELECT);
    force_full_redraw = true;
}

static bool append_playlist_operation(unsigned char operation,
                                      uint32_t track_id,
                                      uint32_t playlist_id,
                                      uint32_t parent_id,
                                      int kind,
                                      const char *name)
{
    char line[192];
    off_t original_size;
    int fd;
    int length;
    bool ok;

    if (!playlist_id || !rb->strchr("ACRMD", operation))
        return false;
    length = rb->snprintf(line, sizeof(line),
                          "%c\t%lu\t%lu\t%lu\t%d\t%s\n",
                          operation, (unsigned long)track_id,
                          (unsigned long)playlist_id,
                          (unsigned long)parent_id, MAX(0, MIN(2, kind)),
                          name ? name : "");
    if (length <= 0 || length >= (int)sizeof(line))
        return false;
    if (!repair_playlist_journal_tail())
        return false;
    fd = rb->open(RBPREP_PLAYLIST_JOURNAL, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
        return false;
    original_size = rb->filesize(fd);
    ok = original_size >= 0 &&
         rb->lseek(fd, original_size, SEEK_SET) >= 0 &&
         write_exact(fd, line, length);
    if (!ok && original_size >= 0)
        rb->ftruncate(fd, original_size);
    if (rb->close(fd) < 0)
        ok = false;
    if (!ok)
        return false;
    refresh_pending_summary();
    return true;
}

static bool append_playlist_seed_operation(uint32_t playlist_id,
                                           uint32_t parent_id,
                                           const char *name)
{
    char data[384];
    off_t original_size;
    int fd;
    int first;
    int second;
    bool ok;

    if (!playlist_id || selected_track_id < 0 || !name || !name[0])
        return false;
    first = rb->snprintf(data, sizeof(data),
                         "%c\t0\t%lu\t%lu\t1\t%s\n",
                         PLAYLIST_OP_CREATE, (unsigned long)playlist_id,
                         (unsigned long)parent_id, name);
    if (first <= 0 || first >= (int)sizeof(data))
        return false;
    second = rb->snprintf(data + first, sizeof(data) - first,
                          "%c\t%lu\t%lu\t0\t1\t%s\n",
                          PLAYLIST_OP_ADD,
                          (unsigned long)selected_track_id,
                          (unsigned long)playlist_id, name);
    if (second <= 0 || first + second >= (int)sizeof(data))
        return false;
    if (!repair_playlist_journal_tail())
        return false;
    fd = rb->open(RBPREP_PLAYLIST_JOURNAL,
                  O_RDWR | O_CREAT, 0666);
    if (fd < 0)
        return false;
    original_size = rb->filesize(fd);
    ok = original_size >= 0 &&
         rb->lseek(fd, original_size, SEEK_SET) >= 0 &&
         write_exact(fd, data, first + second);
    if (!ok && original_size >= 0)
        rb->ftruncate(fd, original_size);
    if (rb->close(fd) < 0)
        ok = false;
    if (ok)
        refresh_pending_summary();
    return ok;
}

static bool append_playlist_journal(int node_index)
{
    struct rbprep_node_record node;
    char name[80];
    int i;
    uint32_t playlist_id;

    if (!read_node_record(node_index, &node) || node.kind == 0 ||
        !read_index_string(node.name_offset, name, sizeof(name)))
        return false;
    if (library_index_version >= 2 && !node.source_id)
        return false;
    playlist_id = node.source_id ? node.source_id : (uint32_t)node_index;
    for (i = 0; i < pending_playlist_count; i++) {
        if (pending_playlists[i].operation == PLAYLIST_OP_ADD &&
            pending_playlists[i].track_id == (uint32_t)selected_track_id &&
            pending_playlists[i].playlist_id == playlist_id)
            return true;
    }
    return append_playlist_operation(PLAYLIST_OP_ADD, selected_track_id,
                                     playlist_id, 0, 1, name);
}

static void add_track_to_favorite(int slot)
{
    struct rbprep_node_record node;
    char name[48];
    int node_index = slot >= 0 && slot < 2
                   ? favorite_playlist_nodes[slot] : -1;

    if (selected_track_id < 0 || node_index < 0 ||
        !read_node_record(node_index, &node) ||
        !read_index_string(node.name_offset, name, sizeof(name))) {
        rb->splash(HZ, slot ? "Favorite 2 is not set"
                            : "Favorite 1 is not set");
        restore_black_canvas();
        return;
    }
    playlist_add_mode = true;
    if (node.kind == 0) {
        tree_selection = tree_top = 0;
        refresh_tree_children(node_index);
        mode = MODE_PLAYLISTS;
        force_full_redraw = true;
        return;
    }
    confirm_playlist_node = node_index;
    rb->snprintf(confirm_message, sizeof(confirm_message),
                 "ADD TO %.28s?", name);
    begin_confirmation(CONFIRM_ADD_PLAYLIST);
}

static uint32_t playlist_parent_source_id(int node_index)
{
    struct rbprep_node_record node;

    if (node_index < 0 || (uint32_t)node_index == RBPREP_ROOT_NODE)
        return 0;
    if (!read_node_record(node_index, &node) || node.kind != 0)
        return 0;
    return node.source_id;
}

static uint32_t next_playlist_source_id(void)
{
    struct rbprep_node_record node;
    uint32_t maximum = 0;
    uint32_t index;
    int i;

    for (index = 0; index < library_node_count; index++)
        if (read_node_record(index, &node))
            maximum = MAX(maximum, node.source_id);
    for (i = 0; i < pending_playlist_count; i++)
        maximum = MAX(maximum, pending_playlists[i].playlist_id);
    return maximum + 1;
}

static void apply_grid_origin(int origin)
{
    int index = nearest_beat_index(origin);

    if (index >= 0) {
        int number;

        if (!source_beat_at(index, NULL, &number))
            number = (index & 3) + 1;

        grid_offset += origin - adjusted_beat_time(index);
        grid_beat_shift = (1 - number) & 3;
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
    if (action == CONFIRM_TRACK_LOAD) {
        bool success;
        bool resume_on_failure = false;

        if (!apply || confirm_choice == 2) {
            restore_black_canvas();
            return;
        }
        if (confirm_choice == 0) {
            if (!quiesce_audio_for_track_load(&resume_on_failure)) {
                track_edit_dirty = true;
                rb->splash(HZ * 2, "Audio pause timed out");
                restore_black_canvas();
                return;
            }
            if (!burn_loaded_track_now()) {
                track_edit_dirty = true;
                if (resume_on_failure &&
                    (rb->audio_status() & AUDIO_STATUS_PAUSE))
                    rb->audio_resume();
                restore_black_canvas();
                return;
            }
        } else {
            staged_tool = -1;
            track_edit_dirty = false;
        }
        force_track_reload = deferred_track_force_reload;
        success = play_track_index(deferred_track_index,
                                   deferred_track_row,
                                   deferred_track_return_mode);
        force_track_reload = false;
        if (!success) {
            if (confirm_choice == 1)
                track_edit_dirty = true;
            if (resume_on_failure &&
                (rb->audio_status() & AUDIO_STATUS_PAUSE))
                rb->audio_resume();
        }
        restore_black_canvas();
        return;
    }
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
    } else if (action == CONFIRM_CUE_DELETE) {
        hotcues[confirm_slot] = -1;
        set_cue_focus_slot(confirm_slot);
        overview_dirty = true;
        save_snapshot = true;
    } else if (action == CONFIRM_GRID_ORIGIN) {
        apply_grid_origin(confirm_time);
        save_snapshot = true;
    } else if (action == CONFIRM_KEEP_EDIT) {
        staged_tool = -1;
        staged_cue_slot = -1;
        save_snapshot = true;
    } else if (action == CONFIRM_ADD_PLAYLIST) {
        if (append_playlist_journal(confirm_playlist_node)) {
            playlist_add_mode = false;
            mode = MODE_DECK;
            refresh_pending_summary();
            burn_request = BURN_REQUEST_ALL;
        } else {
            rb->splash(HZ * 2, "Could not queue playlist add");
        }
    } else if (action == CONFIRM_PLAYLIST_SEED) {
        if (append_playlist_seed_operation(playlist_action_id,
                                           playlist_action_parent_id,
                                           playlist_action_name)) {
            playlist_add_mode = false;
            mode = MODE_DECK;
            burn_request = BURN_REQUEST_ALL;
            rb->splash(HZ, "Playlist seed queued");
        } else {
            rb->splash(HZ * 2, "Could not create playlist seed");
        }
    } else if (action == CONFIRM_GENRE) {
        rb->strlcpy(selected_genre, confirm_genre,
                    sizeof(selected_genre));
        mode = MODE_METADATA;
        save_snapshot = true;
    } else if (action == CONFIRM_BURN) {
        burn_all_now();
    } else if (action == CONFIRM_BURN_ALL_NOW) {
        burn_all_now();
    } else if (action == CONFIRM_MACRO_CLEAR) {
        clear_macro(confirm_slot);
    } else if (action == CONFIRM_MACRO_DELETE_STEP) {
        delete_macro_step(confirm_slot, confirm_time);
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
        if (!delete_pending_playlist(entry->operation, entry->track_id,
                                     entry->playlist_id)) {
            rb->splash(HZ * 2, "Could not delete playlist add");
            restore_black_canvas();
        }
        refresh_pending_summary();
    } else if (action == CONFIRM_PLAYLIST_CREATE ||
               action == CONFIRM_PLAYLIST_RENAME ||
               action == CONFIRM_PLAYLIST_MOVE ||
               action == CONFIRM_PLAYLIST_NODE_DELETE) {
        unsigned char operation =
            action == CONFIRM_PLAYLIST_CREATE ? PLAYLIST_OP_CREATE :
            action == CONFIRM_PLAYLIST_RENAME ? PLAYLIST_OP_RENAME :
            action == CONFIRM_PLAYLIST_MOVE ? PLAYLIST_OP_MOVE :
                                              PLAYLIST_OP_DELETE;
        if (append_playlist_operation(operation, 0, playlist_action_id,
                                      playlist_action_parent_id, 1,
                                      playlist_action_name)) {
            playlist_move_mode = false;
            mode = MODE_PLAYLISTS;
            tree_selection = tree_top = 0;
            refresh_tree_children(RBPREP_ROOT_NODE);
            if (!burn_playlist_changes_now())
                rb->splash(HZ * 2, "Playlist change saved; burn failed");
            tree_selection = tree_top = 0;
            refresh_tree_children(RBPREP_ROOT_NODE);
        } else {
            rb->splash(HZ * 2, "Could not queue playlist change");
        }
    }
    if (save_snapshot) {
        if (!record_edit_change())
            rb->splash(HZ * 2, "Edit applied, journal write failed");
        else if (auto_burn)
            auto_burn_pending_changes();
    }
    restore_black_canvas();
}

static void draw_confirmation(void)
{
    const int x = 25;
    const int y = confirm_action == CONFIRM_TRACK_LOAD ? 72 : 83;
    const int width = LCD_WIDTH - 50;
    const int height = confirm_action == CONFIRM_TRACK_LOAD ? 96 : 76;

    rb->lcd_set_foreground(LCD_RGBPACK(4, 4, 4));
    rb->lcd_fillrect(x, y, width, height);
    rb->lcd_set_foreground(LCD_RGBPACK(105, 115, 108));
    rb->lcd_drawrect(x, y, width, height);
    text(x + 10, y + 9, "CONFIRM", RBPREP_GREEN);
    text(x + 10, y + 29, confirm_message, LCD_WHITE);

    if (confirm_action == CONFIRM_TRACK_LOAD) {
        static const char * const choices[] = {
            "SAVE & LOAD", "DISCARD & LOAD", "STAY"
        };
        static const int widths[] = { 86, 104, 52 };
        int left = x + 8;
        int choice;

        for (choice = 0; choice < 3; choice++) {
            bool selected = confirm_choice == choice;

            rb->lcd_set_foreground(selected ? RBPREP_GREEN
                                             : LCD_RGBPACK(20, 26, 22));
            rb->lcd_fillrect(left, y + 53, widths[choice], 20);
            centered_text(left, widths[choice], y + 58, choices[choice],
                          selected ? LCD_BLACK : LCD_WHITE);
            left += widths[choice] + 4;
        }
        text(x + 10, y + 79, "LEFT/RIGHT: CHOOSE   SELECT: CONFIRM",
             LCD_LIGHTGRAY);
        return;
    }

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
    bool full_frame_update = false;
    long now = *rb->current_tick;

    rb->lcd_set_background(LCD_BLACK);
    rb->lcd_set_drawmode(DRMODE_SOLID);
    if (mode == MODE_PLAYLISTS || mode == MODE_TRACKS || mode == MODE_FILTER ||
        mode == MODE_LIBRARY || mode == MODE_USB || mode == MODE_SETTINGS ||
        mode == MODE_PENDING || mode == MODE_INDEX || mode == MODE_GENRES ||
        mode == MODE_PLAYLIST_ACTIONS || mode == MODE_ACCENT ||
        mode == MODE_MACRO_ACTIONS || mode == MODE_MACRO_EDITOR ||
        mode == MODE_MACRO_PICKER || mode == MODE_MACRO_VALUE) {
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
        else if (mode == MODE_PLAYLIST_ACTIONS)
            draw_playlist_actions();
        else if (mode == MODE_ACCENT)
            draw_accent_picker();
        else if (mode == MODE_MACRO_ACTIONS)
            draw_macro_actions();
        else if (mode == MODE_MACRO_EDITOR)
            draw_macro_editor();
        else if (mode == MODE_MACRO_PICKER)
            draw_macro_picker();
        else if (mode == MODE_MACRO_VALUE)
            draw_macro_value_editor();
        else
            draw_main_menu();
        if (confirm_active)
            draw_confirmation();
        draw_status_bar();
        if (!transition_render_only)
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
            !TIME_BEFORE(now, hud_scroll_deadline)) {
            draw_track_header(true);
            hud_scroll_deadline = now + RBPREP_HUD_SCROLL_TICKS;
            full_frame_update = true;
        }
        if (overview_dirty ||
            !TIME_BEFORE(now, overview_deadline)) {
            if (!TIME_BEFORE(now, overview_deadline))
                overview_playhead_white = !overview_playhead_white;
            draw_overview_waveform();
            draw_tool_row();
            overview_dirty = false;
            overview_deadline = now + RBPREP_OVERVIEW_TICKS;
            full_frame_update = true;
        }
        if (!TIME_BEFORE(now, status_deadline)) {
            draw_status_bar();
            status_deadline = now + RBPREP_STATUS_TICKS;
            full_frame_update = true;
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
        if (!transition_render_only)
            rb->lcd_update();
        force_full_redraw = false;
        overview_dirty = false;
        overview_deadline = now + RBPREP_OVERVIEW_TICKS;
        hud_scroll_deadline = now + RBPREP_HUD_SCROLL_TICKS;
        status_deadline = now + RBPREP_STATUS_TICKS;
    } else {
        rb->lcd_set_foreground(LCD_BLACK);
        rb->lcd_fillrect(232, RBPREP_TOOL_TOP,
                         LCD_WIDTH - 232, RBPREP_TOOL_HEIGHT);
        draw_tool_indicators();
        /* The S5L8702 panel has a single DMA staging buffer rather than
           framebuffer page flipping. Multiple update_rect calls serialize
           on that buffer and visibly shear adjacent HUD regions. Submit one
           coalesced transfer per animation frame; occasionally refresh the
           complete static HUD in that same transfer. */
        if (!transition_render_only) {
            if (full_frame_update)
                rb->lcd_update();
            else
                rb->lcd_update_rect(0, RBPREP_TOOL_TOP, LCD_WIDTH,
                                    RBPREP_WAVE_BOTTOM -
                                    RBPREP_TOOL_TOP + 1);
        }
    }
}

static void reset_play_clock(int anchor, long tick)
{
    play_clock_anchor = clamp_playhead(anchor);
    play_clock_tick = tick;
    reported_audio_elapsed = -1;
    audio_sync_after = tick + MAX(1, HZ / 4);
}

static bool play_clock_audio_running(int status)
{
    bool clock_state = seek_state == SEEK_IDLE ||
                       seek_state == SEEK_PREVIEW ||
                       seek_state == SEEK_CUE_HOLD;

    return clock_state && (status & AUDIO_STATUS_PLAY) &&
           !(status & AUDIO_STATUS_PAUSE);
}

static bool play_clock_tracks_cursor(int status)
{
    return play_clock_audio_running(status) &&
           (seek_state == SEEK_IDLE || seek_state == SEEK_CUE_HOLD);
}

static int interpolated_playhead(long now)
{
    long elapsed_ticks = now - play_clock_tick;

    if (elapsed_ticks < 0)
        elapsed_ticks = 0;
    return clamp_playhead(play_clock_anchor +
        (long long)elapsed_ticks * 1000 * deck_speed_x100() /
        (HZ * PITCH_SPEED_100));
}

static int live_playhead_now(void)
{
    int status = rb->audio_status();
    long now = *rb->current_tick;

    if (!play_clock_tracks_cursor(status))
        return clamp_playhead(playhead);
    return interpolated_playhead(now);
}

static int synchronized_audio_playhead(void)
{
    int status = rb->audio_status();
    int position;
    long now = *rb->current_tick;
    struct mp3entry *id3;

    if (!play_clock_audio_running(status))
        return clamp_playhead(playhead);
    position = interpolated_playhead(now);
    id3 = rb->audio_current_track();
    if (id3 && !TIME_BEFORE(now, audio_sync_after)) {
        int observed = clamp_playhead(id3->elapsed);

        if (observed != reported_audio_elapsed) {
            /* PCM publishes the source timestamp of each buffer exactly when
               that buffer reaches the output. Make that timestamp the clock
               anchor and interpolate only until the next one arrives. Never
               advance the anchor a frame at a time: doing so truncates a
               fractional millisecond on every draw and eventually lets the
               waveform drift away after a scrub or seek. */
            reported_audio_elapsed = observed;
            play_clock_anchor = observed;
            play_clock_tick = now;
            position = observed;
        }
    }
    return position;
}

static bool update_play_clock(void)
{
    int status = rb->audio_status();
    int position;

    /* During a short scrub preview the clickwheel owns playhead/seek_target.
       The audition audio has its own clock, but must not race that cursor and
       overwrite a newly requested position. Cue-hold is continuous playback,
       so it follows the audio clock just like ordinary deck playback. */
    if (!play_clock_tracks_cursor(status))
        return false;
    position = synchronized_audio_playhead();
    if (position == playhead)
        return false;
    playhead = position;
    return true;
}

static void stop_editor_audio(void)
{
    bool resume;

    if (seek_state == SEEK_IDLE) {
        return;
    }
    update_play_clock();
    resume = !seek_was_paused && !cue_audition_active;
    finish_transport_seek(playhead);
    /* Process the finishing seek before this finalizer returns. */
    rb->audio_pause();
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
        prepare_transport_seek();
        finish_transport_seek(target);
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
    bool continuing_silent = !preview && seek_state == SEEK_DEBOUNCE &&
                             !seek_preview;

    if (!(status & AUDIO_STATUS_PLAY))
        return;
    if (continuing_silent) {
        seek_target = playhead;
        seek_deadline = *rb->current_tick + RBPREP_SEEK_SETTLE;
        return;
    }
    if (seek_state != SEEK_IDLE)
        stop_editor_audio();
    status = rb->audio_status();
    original_pause = !!(status & AUDIO_STATUS_PAUSE);
    seek_target = playhead;
    seek_preview = preview;
    seek_was_paused = original_pause;

    if (preview) {
        prepare_transport_seek();
        seek_deadline = *rb->current_tick + RBPREP_SEEK_DEBOUNCE;
    } else {
        /* Continuous navigation is deliberately silent. Pause exactly once,
           keep all wheel/inertia movement in RAM, and let the service routine
           commit one decoder seek after the gesture settles. */
        if (!original_pause) {
            rb->pcmbuf_fade(true, false);
            rb->audio_pause();
        }
        seek_deadline = *rb->current_tick + RBPREP_SEEK_SETTLE;
    }
    seek_state = SEEK_DEBOUNCE;
    reset_play_clock(playhead, *rb->current_tick);
}

static bool service_audio_seek(void)
{
    long now = *rb->current_tick;

    /* Preview and held-cue playback use the same authoritative transport
       clock as ordinary playback; update_play_clock() runs immediately after
       this service call in the main loop. */
    if (seek_state == SEEK_CUE_HOLD)
        return false;

    if (seek_state == SEEK_IDLE ||
        (TIME_BEFORE(now, seek_deadline) && now != seek_deadline))
        return false;

    if (seek_state == SEEK_DEBOUNCE) {
        if (!seek_preview) {
            prepare_transport_seek();
            finish_transport_seek(seek_target);
            seek_applied_target = seek_target;
            seek_applied_tick = now;
            playhead = seek_target;
            seek_state = SEEK_IDLE;
            if (!seek_was_paused) {
                rb->pcmbuf_fade(true, true);
                rb->audio_resume();
            }
            reset_play_clock(playhead, now);
            overview_dirty = true;
            return true;
        }
        finish_transport_seek(seek_target);
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
            finish_transport_seek(seek_target);
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
        prepare_transport_seek();
        finish_transport_seek(seek_target);
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
    prepare_transport_seek();
    finish_transport_seek(cue_audition_position);
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
    prepare_transport_seek();
    finish_transport_seek(cue_audition_position);
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
    request_audio_seek(audition);
}

#ifdef HAVE_WHEEL_POSITION
static bool seek_wheel_physics_enabled(void)
{
    return platter_wheel_mode && !display_locked && !confirm_active &&
           !tool_menu_active && mode >= MODE_DECK &&
           active_tool() == TOOL_SEEK;
}

static bool seek_wheel_event_owned(void)
{
    return seek_wheel_physics_enabled() && seek_wheel_gesture_tracked;
}

static bool service_seek_wheel_physics(void)
{
    const int maximum_velocity = 120000 * 256;
    const int cardinal_buttons = BUTTON_LEFT | BUTTON_RIGHT |
                                 BUTTON_MENU | BUTTON_PLAY;
    long now = *rb->current_tick;
    long elapsed = now - seek_wheel_motion_tick;
    int button_state = rb->button_status();
    int position = rb->wheel_status();
    int movement_fp = 0;
    bool changed = false;

    if (!seek_wheel_physics_enabled()) {
        seek_wheel_touch_position = -1;
        seek_wheel_velocity_fp = 0;
        seek_wheel_delta_fp = 0;
        seek_wheel_arm_delta = 0;
        seek_wheel_gesture_tracked = false;
        seek_wheel_blocked_until_release = false;
        seek_wheel_motion_tick = now;
        return false;
    }
    /* A cardinal click necessarily touches the capacitive ring.  Do not let
       that contact arm the platter, and keep it blocked until the finger has
       actually left the wheel; otherwise the click's release can become the
       first sample of an unintended scrub gesture. */
    if ((button_state & cardinal_buttons) && !seek_wheel_gesture_tracked)
        seek_wheel_blocked_until_release = true;
    if (seek_wheel_blocked_until_release) {
        seek_wheel_touch_position = -1;
        seek_wheel_velocity_fp = 0;
        seek_wheel_delta_fp = 0;
        seek_wheel_arm_delta = 0;
        seek_wheel_gesture_tracked = false;
        if (position < 0 && !(button_state & cardinal_buttons))
            seek_wheel_blocked_until_release = false;
        seek_wheel_motion_tick = now;
        return false;
    }
    if (elapsed <= 0)
        return false;
    elapsed = MIN(elapsed, MAX(1, HZ / 4));
    if (position >= 0) {
        if (seek_wheel_touch_position >= 0) {
            int delta = position - seek_wheel_touch_position;

            if (delta > 48)
                delta -= 96;
            else if (delta < -48)
                delta += 96;
            if (!seek_wheel_gesture_tracked) {
                seek_wheel_arm_delta += delta;
                if (ABS(seek_wheel_arm_delta) < RBPREP_WHEEL_ARM_UNITS) {
                    delta = 0;
                } else {
                    delta = seek_wheel_arm_delta;
                    seek_wheel_arm_delta = 0;
                    seek_wheel_gesture_tracked = true;
                }
            }
            /* Four capacitive position units are one legacy scroll detent.
               A short movement dead zone filters stationary finger jitter;
               keep fractional milliseconds after arming until they add up to
               a real seek. */
            movement_fp = delta * scrub_step_ms() * 256 / 4;
            if (delta) {
                int instantaneous = (long long)movement_fp * HZ / elapsed;

                instantaneous = MAX(-maximum_velocity,
                                    MIN(maximum_velocity, instantaneous));
                seek_wheel_velocity_fp =
                    (seek_wheel_velocity_fp + instantaneous * 2) / 3;
            } else {
                int damping = MIN(128,
                    (int)(elapsed * 700 / MAX(1, HZ)));

                seek_wheel_velocity_fp =
                    (long long)seek_wheel_velocity_fp *
                    (256 - damping) / 256;
            }
        }
        seek_wheel_touch_position = position;
    } else {
        seek_wheel_touch_position = -1;
        seek_wheel_arm_delta = 0;
        if (ABS(seek_wheel_velocity_fp) >= 20 * 256) {
            int damping = MIN(224,
                (int)(elapsed * 700 / MAX(1, HZ)));

            movement_fp =
                (long long)seek_wheel_velocity_fp * elapsed / HZ;
            seek_wheel_velocity_fp =
                (long long)seek_wheel_velocity_fp *
                (256 - damping) / 256;
        } else {
            seek_wheel_velocity_fp = 0;
            seek_wheel_gesture_tracked = false;
        }
    }
    if (movement_fp) {
        int before = playhead;
        int delta;

        seek_wheel_delta_fp += movement_fp;
        delta = seek_wheel_delta_fp / 256;
        seek_wheel_delta_fp -= delta * 256;
        if (delta) {
            seek_by(delta, false);
            changed = playhead != before;
            if (!changed)
                seek_wheel_velocity_fp = 0;
        }
    }
    seek_wheel_motion_tick = now;
    return changed;
}
#else
static bool seek_wheel_physics_enabled(void)
{
    return false;
}

static bool seek_wheel_event_owned(void)
{
    return false;
}

static bool service_seek_wheel_physics(void)
{
    return false;
}
#endif

static void beat_jump(int direction)
{
    int index = current_beat_index(playhead);
    int target;

    if (imported_grid_resident() && index >= 0) {
        index = MAX(0, MIN(beat_count - 1, index + direction));
        target = adjusted_beat_time(index);
    } else {
        int period = beat_period_ms();
        int snapped = quantized_time(playhead);
        target = snapped + direction * period;
    }
    seek_by(clamp_playhead(target) - playhead, false);
}

static void toggle_playback(void)
{
    int status = rb->audio_status();

    if (cue_audition_active) {
        latch_cue_audition();
        return;
    }
    if (seek_state == SEEK_PREVIEW) {
        playhead = synchronized_audio_playhead();
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

static void keyboard_key_name(int key, char *label, size_t size)
{
    static const char characters[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.&'/#";
    int character_count = sizeof(characters) - 1;

    if (key < character_count)
        rb->snprintf(label, size, "%c", characters[key]);
    else if (key == character_count)
        rb->strlcpy(label, "SPACE", size);
    else if (key == character_count + 1)
        rb->strlcpy(label, "BACKSPACE", size);
    else
        rb->strlcpy(label, "DONE", size);
}

static void keyboard_remove_last(char *value)
{
    size_t length = rb->strlen(value);

    if (!length)
        return;
    do {
        length--;
    } while (length > 0 && ((unsigned char)value[length] & 0xc0) == 0x80);
    value[length] = '\0';
}

static void draw_rbprep_keyboard(const char *title, const char *value,
                                 int selected_key)
{
    static const char characters[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.&'/#";
    int key_count = sizeof(characters) - 1 + 3;
    int previous = (selected_key + key_count - 1) % key_count;
    int next = (selected_key + 1) % key_count;
    char active[20];
    char neighbor[20];
    char shown[48];
    const char *visible = value;
    size_t value_length = rb->strlen(value);
    int width;
    int start = 0;

    draw_blade_shell(title, RBPREP_GREEN);
    rb->lcd_set_foreground(LCD_RGBPACK(14, 24, 18));
    rb->lcd_fillrect(8, 36, LCD_WIDTH - 16, 36);
    rb->lcd_set_foreground(RBPREP_GREEN_DIM);
    rb->lcd_drawrect(8, 36, LCD_WIDTH - 16, 36);

    if (value_length >= sizeof(shown)) {
        visible = value + value_length - sizeof(shown) + 1;
        while (*visible && ((unsigned char)*visible & 0xc0) == 0x80)
            visible++;
    }
    rb->strlcpy(shown, visible, sizeof(shown));
    rb->lcd_getstringsize(shown, &width, NULL);
    while (shown[start] && width > LCD_WIDTH - 34) {
        start++;
        while (shown[start] &&
               ((unsigned char)shown[start] & 0xc0) == 0x80)
            start++;
        rb->lcd_getstringsize(shown + start, &width, NULL);
    }
    text(15, 48, shown + start, LCD_WHITE);
    rb->lcd_set_foreground(RBPREP_GREEN);
    rb->lcd_vline(MIN(LCD_WIDTH - 17, 17 + width), 46, 62);

    keyboard_key_name(previous, neighbor, sizeof(neighbor));
    centered_text(8, 82, 88, neighbor, LCD_RGBPACK(72, 91, 79));
    keyboard_key_name(next, neighbor, sizeof(neighbor));
    centered_text(LCD_WIDTH - 90, 82, 88, neighbor,
                  LCD_RGBPACK(72, 91, 79));
    draw_blade_selection(104, 30, RBPREP_GREEN);
    rb->lcd_set_foreground(LCD_RGBPACK(240, 100, 45));
    xlcd_drawcircle(LCD_WIDTH / 2, 119, 17);
    rb->lcd_set_foreground(LCD_RGBPACK(80, 180, 245));
    xlcd_drawcircle(LCD_WIDTH / 2, 119, 14);
    keyboard_key_name(selected_key, active, sizeof(active));
    centered_text(7, LCD_WIDTH - 14, 113, active, LCD_WHITE);

    rb->lcd_set_foreground(LCD_RGBPACK(8, 16, 11));
    rb->lcd_fillrect(0, 169, LCD_WIDTH, LCD_HEIGHT - 169);
    text(10, 176, "WHEEL: PICK CHARACTER", RBPREP_GREEN);
    text(10, 191, "SELECT: TYPE     LEFT: BACKSPACE", LCD_WHITE);
    text(10, 206, "RIGHT: SPACE     PLAY: DONE", LCD_WHITE);
    text(10, 221, "MENU: CANCEL", LCD_RGBPACK(155, 174, 161));
    draw_status_bar();
    rb->lcd_update();
}

static bool rbprep_keyboard(char *value, size_t size, const char *title)
{
    static const char characters[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.&'/#";
    int character_count = sizeof(characters) - 1;
    int key_count = character_count + 3;
    int selected_key = 0;

    while (true) {
        int button;
        int base;
        size_t length;

        draw_rbprep_keyboard(title, value, selected_key);
        button = rb->button_get(true);
        if (button & BUTTON_REL)
            continue;
        base = button & ~BUTTON_REPEAT;
        if (base == BUTTON_SCROLL_FWD) {
            selected_key = (selected_key + 1) % key_count;
        } else if (base == BUTTON_SCROLL_BACK) {
            selected_key = (selected_key + key_count - 1) % key_count;
        } else if (base == BUTTON_LEFT) {
            keyboard_remove_last(value);
        } else if (base == BUTTON_RIGHT) {
            length = rb->strlen(value);
            if (length + 1 < size) {
                value[length] = ' ';
                value[length + 1] = '\0';
            }
        } else if (base == BUTTON_PLAY) {
            return true;
        } else if (base == BUTTON_MENU) {
            return false;
        } else if (base == BUTTON_SELECT && !(button & BUTTON_REPEAT)) {
            if (selected_key < character_count) {
                length = rb->strlen(value);
                if (length + 1 < size) {
                    value[length] = characters[selected_key];
                    value[length + 1] = '\0';
                }
            } else if (selected_key == character_count) {
                length = rb->strlen(value);
                if (length + 1 < size) {
                    value[length] = ' ';
                    value[length + 1] = '\0';
                }
            } else if (selected_key == character_count + 1) {
                keyboard_remove_last(value);
            } else {
                return true;
            }
        } else if (handle_usb_system_event(button)) {
            return false;
        }
    }
}

static bool playlist_name_with_keyboard(char *name, size_t size,
                                        const char *title)
{
    int start = 0;
    int end;

    if (!rbprep_keyboard(name, size, title)) {
        rb->lcd_setfont(FONT_SYSFIXED);
        restore_black_canvas();
        return false;
    }
    rb->lcd_setfont(FONT_SYSFIXED);
    while (name[start] == ' ' || name[start] == '\t')
        start++;
    if (start)
        rb->memmove(name, name + start, rb->strlen(name + start) + 1);
    end = rb->strlen(name);
    while (end > 0 && (name[end - 1] == ' ' || name[end - 1] == '\t'))
        name[--end] = '\0';
    if (!name[0]) {
        rb->splash(HZ, "Playlist name is empty");
        restore_black_canvas();
        return false;
    }
    return true;
}

static void choose_playlist_action(void)
{
    struct rbprep_node_record node;
    bool has_target = playlist_action_node >= 0 &&
                      read_node_record(playlist_action_node, &node) &&
                      node.source_id;
    bool editable = has_target && node.kind != 0;

    if (playlist_action_selection == 0) {
        rb->strlcpy(playlist_action_name, "NEW PLAYLIST",
                    sizeof(playlist_action_name));
        if (!playlist_name_with_keyboard(playlist_action_name,
                                         sizeof(playlist_action_name),
                                         "CREATE PLAYLIST"))
            return;
        playlist_action_id = next_playlist_source_id();
        playlist_action_parent_id =
            playlist_parent_source_id(playlist_action_parent);
        if (playlist_action_parent >= 0 && !playlist_action_parent_id) {
            rb->splash(HZ * 2, "Rebuild cache for stable folder IDs");
            restore_black_canvas();
            return;
        }
        rb->snprintf(confirm_message, sizeof(confirm_message),
                     "CREATE %.30s?", playlist_action_name);
        begin_confirmation(CONFIRM_PLAYLIST_CREATE);
    } else if (playlist_action_selection >= 5) {
        int slot;

        if (!has_target) {
            rb->splash(HZ, "Select a playlist or folder first");
            restore_black_canvas();
            return;
        }
        if (playlist_action_selection == 5 ||
            playlist_action_selection == 6) {
            slot = playlist_action_selection - 5;
            favorite_playlist_ids[slot] = node.source_id;
            mark_rbprep_config_dirty();
            save_rbprep_config();
            rb->splash(HZ, slot ? "Favorite 2 set" : "Favorite 1 set");
        } else {
            bool cleared = false;

            for (slot = 0; slot < 2; slot++) {
                if (favorite_playlist_ids[slot] == (int)node.source_id) {
                    favorite_playlist_ids[slot] = 0;
                    cleared = true;
                }
            }
            if (cleared) {
                mark_rbprep_config_dirty();
                save_rbprep_config();
            }
            rb->splash(HZ, cleared ? "Favorite cleared"
                                   : "Target is not a favorite");
        }
        refresh_tree_children(tree_parent);
        restore_black_canvas();
    } else if (!editable) {
        rb->splash(HZ, "Select a playlist first");
        restore_black_canvas();
    } else if (playlist_action_selection == 1) {
        read_index_string(node.name_offset, playlist_action_name,
                          sizeof(playlist_action_name));
        if (!playlist_name_with_keyboard(playlist_action_name,
                                         sizeof(playlist_action_name),
                                         "RENAME PLAYLIST"))
            return;
        playlist_action_id = node.source_id;
        rb->snprintf(confirm_message, sizeof(confirm_message),
                     "RENAME TO %.27s?", playlist_action_name);
        begin_confirmation(CONFIRM_PLAYLIST_RENAME);
    } else if (playlist_action_selection == 2) {
        playlist_action_id = node.source_id;
        read_index_string(node.name_offset, playlist_action_name,
                          sizeof(playlist_action_name));
        playlist_move_mode = true;
        tree_selection = tree_top = 0;
        refresh_tree_children(RBPREP_ROOT_NODE);
        mode = MODE_PLAYLISTS;
        force_full_redraw = true;
    } else if (playlist_action_selection == 3) {
        playlist_action_id = node.source_id;
        read_index_string(node.name_offset, playlist_action_name,
                          sizeof(playlist_action_name));
        rb->snprintf(confirm_message, sizeof(confirm_message),
                     "DELETE %.30s?", playlist_action_name);
        begin_confirmation(CONFIRM_PLAYLIST_NODE_DELETE);
    } else {
        int slot;
        if (!cycle_macro_link(node.source_id)) {
            rb->splash(HZ, "Could not save workflow link");
            restore_black_canvas();
            return;
        }
        slot = macro_link_for(node.source_id);
        rb->splash(HZ, slot < 0 ? "Workflow link off" :
                   slot == 0 ? "Workflow M1 linked" :
                               "Workflow M2 linked");
        restore_black_canvas();
    }
}

static void add_genre_with_keyboard(void)
{
    char name[32] = "";
    int start;
    int end;

    if (!rbprep_keyboard(name, sizeof(name), "ADD GENRE")) {
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
    if (!rbprep_keyboard(query, sizeof(query), "SEARCH COLLECTION")) {
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
    collection_shuffle_active = false;
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

static void activate_main_selection(int item)
{
    if (item == 0 && library_fd >= 0) {
        collection_shuffle_active = false;
        open_track_browser(-1);
    } else if (item == 1 && library_fd >= 0) {
        start_shuffled_collection();
    } else if (item == 2 && library_fd >= 0) {
        tree_selection = tree_top = 0;
        refresh_tree_children(RBPREP_ROOT_NODE);
        mode = MODE_PLAYLISTS;
        force_full_redraw = true;
    } else if (item == 3) {
        if (library_fd >= 0)
            open_loaded_track();
        else {
            rb->splash(HZ * 2, "RBPrep index is offline");
            restore_black_canvas();
        }
    } else if (item == 4) {
        mode = MODE_USB;
        force_full_redraw = true;
    } else if (item == 5) {
        settings_selection = 0;
        mode = MODE_SETTINGS;
        force_full_redraw = true;
    } else if (item == 6) {
        refresh_pending_summary();
        mode = MODE_INDEX;
        force_full_redraw = true;
    } else if (item == 7) {
        rb->snprintf(confirm_message, sizeof(confirm_message),
                     "EXIT TO ROCKBOX?");
        begin_confirmation(CONFIRM_EXIT);
    }
}

static void begin_main_launch(int item)
{
    item = MAX(0, MIN((int)ARRAYLEN(main_menu_items) - 1, item));
    animate_main_launch_transition();
    activate_main_selection(item);
    capture_transition_destination();
    animate_main_dissolve(true);
    present_transition_destination();
}

static void short_select(void)
{
    if (mode == MODE_LIBRARY) {
        begin_main_launch(selection);
    } else if (mode == MODE_PLAYLISTS) {
        struct rbprep_node_record node;
        int index;

        if (playlist_add_mode && tree_selection == 0) {
            rb->strlcpy(playlist_action_name, "NEW PLAYLIST",
                        sizeof(playlist_action_name));
            if (!playlist_name_with_keyboard(playlist_action_name,
                                             sizeof(playlist_action_name),
                                             "CREATE + ADD TRACK"))
                return;
            playlist_action_id = next_playlist_source_id();
            playlist_action_parent_id =
                playlist_parent_source_id(
                    tree_parent == RBPREP_ROOT_NODE ? -1
                                                    : (int)tree_parent);
            if (tree_parent != RBPREP_ROOT_NODE &&
                !playlist_action_parent_id) {
                rb->splash(HZ * 2,
                           "Rebuild cache for stable folder IDs");
                restore_black_canvas();
                return;
            }
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "CREATE %.22s + ADD?", playlist_action_name);
            begin_confirmation(CONFIRM_PLAYLIST_SEED);
            return;
        }
        index = playlist_node_at_visible(tree_selection, &node, NULL);
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
        } else if (index >= 0 && playlist_move_mode) {
            rb->splash(HZ, "PLAY chooses this folder");
            restore_black_canvas();
        } else if (index >= 0) {
            open_track_browser(index);
        }
    } else if (mode == MODE_PLAYLIST_ACTIONS) {
        choose_playlist_action();
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
        int choice = usb_selection;

        if (choice != 0 && !persist_usb_arm_state()) {
            apply_usb_choice(0);
            rb->splash(HZ * 2, "USB arm failed; power only");
        } else {
            apply_usb_choice(choice);
            rb->splash(HZ, choice == 2 ? "USB DAC armed" :
                           choice == 1 ? "Data transfer armed" :
                                         "USB power only");
        }
        restore_black_canvas();
    } else if (mode == MODE_SETTINGS) {
        if (settings_selection == 0) {
            autoboot_enabled = !autoboot_enabled;
        } else if (settings_selection == 1) {
            autoplay_enabled = !autoplay_enabled;
            playlist_playback = autoplay_enabled;
        } else if (settings_selection == 2)
            waveform_half = !waveform_half;
        else if (settings_selection == 3) {
            enum rbprep_tool previous = tool_pages[0].tools[
                MAX(0, MIN(tool_pages[0].count - 1, deck_tool))];

            ipod_seek_first = !ipod_seek_first;
            update_player_tool_order();
            deck_tool = player_tool_index(previous);
        } else if (settings_selection == 4) {
            key_notation = (key_notation + 1) %
                           ARRAYLEN(key_notation_styles);
        } else if (settings_selection == 5) {
            rb->splash(HZ, "Track exit always asks");
            restore_black_canvas();
        } else if (settings_selection == 6) {
            rb->splash(HZ, "Save & Load commits safely");
            restore_black_canvas();
        } else if (settings_selection == 7) {
            click_sound = !click_sound;
        } else if (settings_selection <= 10) {
            color_picker_target = settings_selection - 8;
            accent_component = 0;
            mode = MODE_ACCENT;
        } else if (settings_selection == 11) {
            turntable_arm_style = !turntable_arm_style;
        } else if (settings_selection == 12) {
            turntable_headshell_style =
                (turntable_headshell_style + 1) %
                ARRAYLEN(turntable_headshell_styles);
        } else {
            platter_wheel_mode = !platter_wheel_mode;
#ifdef HAVE_WHEEL_POSITION
            main_wheel_touch_position = -1;
            main_wheel_velocity_fp = 0;
            seek_wheel_touch_position = -1;
            seek_wheel_velocity_fp = 0;
            seek_wheel_delta_fp = 0;
            seek_wheel_arm_delta = 0;
            seek_wheel_gesture_tracked = false;
            seek_wheel_blocked_until_release = false;
#endif
        }
        rebuild_waveform_height_lut();
        mark_rbprep_config_dirty();
        force_full_redraw = true;
    } else if (mode == MODE_ACCENT) {
        mark_rbprep_config_dirty();
        mode = MODE_SETTINGS;
        force_full_redraw = true;
    } else if (mode == MODE_PENDING) {
        int index = -1;
        if (pending_selection < pending_snapshot_count) {
            int ordinal = pending_snapshot_count - 1 - pending_selection;
            index = find_track_index_by_id(pending_entries[ordinal].track_id);
        } else if (pending_selection < pending_snapshot_count +
                                            pending_playlist_count) {
            int ordinal = pending_selection - pending_snapshot_count;
            if (pending_playlists[ordinal].operation == PLAYLIST_OP_ADD)
                index = find_track_index_by_id(
                    pending_playlists[ordinal].track_id);
            else {
                rb->splashf(HZ, "%c: %s",
                            pending_playlists[ordinal].operation,
                            pending_playlists[ordinal].name);
                restore_black_canvas();
                return;
            }
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
    } else if (mode == MODE_MACRO_ACTIONS) {
        if (macro_action_selection == 0) {
            rename_macro(macro_manage_slot);
        } else if (macro_action_selection == 1) {
            open_macro_editor(macro_manage_slot);
        } else {
            confirm_slot = macro_manage_slot;
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "CLEAR M%d %.22s?", macro_manage_slot + 1,
                         tool_macros[macro_manage_slot].name);
            begin_confirmation(CONFIRM_MACRO_CLEAR);
        }
    } else if (mode == MODE_MACRO_EDITOR) {
        struct rbprep_tool_macro *macro = &tool_macros[macro_manage_slot];

        if (macro_edit_operation == 2) {
            if (macro->count <= 0) {
                rb->splash(HZ, "Workflow is empty");
                restore_black_canvas();
            } else {
                confirm_slot = macro_manage_slot;
                confirm_time = macro_edit_position;
                rb->snprintf(confirm_message, sizeof(confirm_message),
                             "DELETE STEP %02d?", macro_edit_position + 1);
                begin_confirmation(CONFIRM_MACRO_DELETE_STEP);
            }
        } else if (macro_edit_operation == 0 &&
                   macro->count >= RBPREP_MACRO_STEPS) {
            rb->splash(HZ, "Workflow is full");
            restore_black_canvas();
        } else if ((macro_edit_operation == 1 ||
                    macro_edit_operation == 3) && macro->count <= 0) {
            rb->splash(HZ, "Nothing to replace");
            restore_black_canvas();
        } else if (macro_edit_operation == 3) {
            struct rbprep_macro_step *step =
                &macro->steps[macro_edit_position];

            macro_value_tool = step->tool;
            macro_value_draft = step->value == RBPREP_MACRO_CHOOSE
                              ? macro_current_tool_value(step->tool)
                              : step->value;
            macro_value_choose = step->value == RBPREP_MACRO_CHOOSE;
            macro_value_lock = !!(step->flags & RBPREP_MACRO_LOCK_WHEEL);
            macro_value_new_step = false;
            mode = MODE_MACRO_VALUE;
            force_full_redraw = true;
        } else {
            int page;
            int item;

            macro_picker_page = 0;
            macro_picker_tool = 0;
            if (macro_edit_operation == 1) {
                enum rbprep_tool current =
                    macro->steps[macro_edit_position].tool;
                for (page = 0; page < (int)ARRAYLEN(tool_pages); page++) {
                    int count = macro_picker_count_for_page(page);
                    for (item = 0; item < count; item++) {
                        if (macro_picker_tool_at(page, item) == current) {
                            macro_picker_page = page;
                            macro_picker_tool = item;
                        }
                    }
                }
            }
            mode = MODE_MACRO_PICKER;
            force_full_redraw = true;
        }
    } else if (mode == MODE_MACRO_PICKER) {
        apply_macro_picker_tool();
    } else if (mode == MODE_MACRO_VALUE) {
        save_macro_value();
    } else if (mode == MODE_DECK) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_SEEK) {
            if (deck_cue >= 0)
                playhead = deck_cue;
            audition_playhead();
        } else if (tool == TOOL_SCRUB_STEP) {
            mark_rbprep_config_dirty();
        } else if (tool == TOOL_ZOOM) {
            zoom = 1;
        }
    } else if (mode == MODE_TEMPO) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_HOST_RPM) {
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
            mark_rbprep_config_dirty();
        }
    } else if (mode == MODE_CUES) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_CUE_SLOT && hotcues[cue_slot] < 0) {
            int start_slot = cue_slot;
            hotcues[cue_slot] = quantized_time(select_pressed_time);
            hotcue_colors[cue_slot] = 3;
            overview_dirty = true;
            if (!record_edit_change()) {
                rb->splash(HZ, "Cue set; journal failed");
                restore_black_canvas();
            } else if (auto_burn) {
                auto_burn_pending_changes();
            }
            do {
                step_cue_focus_slot(1);
            } while (cue_slot != start_slot && hotcues[cue_slot] >= 0);
        } else if (tool == TOOL_CUE_DELETE) {
            if (cue_slot_for_delete(cue_slot) >= 0) {
                confirm_slot = cue_slot;
                rb->snprintf(confirm_message, sizeof(confirm_message),
                             "DELETE CUE %02d?", cue_slot + 1);
                begin_confirmation(CONFIRM_CUE_DELETE);
            } else {
                rb->splashf(HZ, "CUE %02d IS EMPTY", cue_slot + 1);
                restore_black_canvas();
            }
        } else if (tool == TOOL_CUE_MOVE && hotcues[cue_slot] >= 0) {
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
    } else if (mode == MODE_PITCH) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_KEYLOCK) {
            keylock_enabled = !keylock_enabled;
            apply_playback_rate();
            mark_rbprep_config_dirty();
            force_full_redraw = true;
        } else if (tool == TOOL_KEY_NOTATION) {
            key_notation = (key_notation + 1) %
                           ARRAYLEN(key_notation_styles);
            mark_rbprep_config_dirty();
            force_full_redraw = true;
        } else if (tool == TOOL_META_KEY && staged_tool == tool) {
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "KEEP KEY CHANGE?");
            begin_confirmation(CONFIRM_KEEP_EDIT);
        }
    } else if (mode == MODE_LIST) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_PLAYLIST_MODE) {
            playlist_playback = !playlist_playback;
        } else if (tool == TOOL_ADD_PLAYLIST) {
            if (selected_track_id >= 0) {
                playlist_add_mode = true;
                tree_selection = tree_top = 0;
                refresh_tree_children(RBPREP_ROOT_NODE);
                mode = MODE_PLAYLISTS;
                force_full_redraw = true;
            }
        } else if (tool == TOOL_FAVORITE_ONE) {
            add_track_to_favorite(0);
        } else if (tool == TOOL_FAVORITE_TWO) {
            add_track_to_favorite(1);
        } else if (tool == TOOL_BURN_SONG) {
            if (selected_track_id >= 0)
                burn_loaded_track_now();
        } else if (tool == TOOL_BURN_ALL) {
            rb->snprintf(confirm_message, sizeof(confirm_message),
                         "INSTANT BURN ALL?");
            begin_confirmation(CONFIRM_BURN_ALL_NOW);
        }
    } else if (mode == MODE_PNAV) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_PLAYLIST_PREVIOUS && pnav_tool_available(tool)) {
            navigate_playlist_track(-1);
        } else if (tool == TOOL_PLAYLIST_NEXT &&
                   pnav_tool_available(tool)) {
            navigate_playlist_track(1);
        } else if (tool == TOOL_RESTART_PLAYBACK &&
                   pnav_tool_available(tool)) {
            int status = rb->audio_status();
            if (!(status & AUDIO_STATUS_PLAY)) {
                reload_current_track();
            } else {
                jump_to_time(0);
                if (status & AUDIO_STATUS_PAUSE) {
                    rb->audio_resume();
                    reset_play_clock(0, *rb->current_tick);
                }
                audio_was_running = true;
            }
        } else if (tool == TOOL_RELOAD_TRACK &&
                   pnav_tool_available(tool)) {
            reload_current_track();
        }
    } else if (mode == MODE_VISUALIZER || mode == MODE_VISUALIZER_TWO) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_WAVEFORM_STYLE)
            visualizer_mode = 0;
        else if (tool == TOOL_VIS_BOOMBOX)
            visualizer_mode = 1;
        else if (tool == TOOL_VIS_EQ)
            visualizer_mode = 2;
        else if (tool == TOOL_VIS_TURNTABLE)
            visualizer_mode = 3;
        else if (tool == TOOL_VIS_CANYON)
            visualizer_mode = 4;
        else if (tool == TOOL_VIS_ORBIT)
            visualizer_mode = 5;
        else if (tool == TOOL_VIS_PHRASE)
            visualizer_mode = 6;
        else if (tool == TOOL_VIS_HARMONIC)
            visualizer_mode = 7;
        if (visualizer_mode == 0)
            stop_spectrum_capture();
        else
            start_spectrum_capture();
        mark_rbprep_config_dirty();
        force_full_redraw = true;
    } else if (mode == MODE_MACRO) {
        enum rbprep_tool tool = active_tool();
        if (tool == TOOL_KEYLOCK) {
            keylock_enabled = !keylock_enabled;
            apply_playback_rate();
            mark_rbprep_config_dirty();
        } else if (tool == TOOL_GRID_QUANTIZE) {
            quantize = !quantize;
        } else if (tool == TOOL_KEY_NOTATION) {
            key_notation = (key_notation + 1) %
                           ARRAYLEN(key_notation_styles);
            mark_rbprep_config_dirty();
        } else if (tool == TOOL_MACRO_ONE) {
            activate_macro(0);
        } else if (tool == TOOL_MACRO_TWO) {
            activate_macro(1);
        }
    }
}

static void long_select(void)
{
    if (mode >= MODE_DECK &&
        (active_tool() == TOOL_MACRO_ONE ||
         active_tool() == TOOL_MACRO_TWO)) {
        int slot = active_tool() == TOOL_MACRO_ONE ? 0 : 1;
        macro_manage_slot = slot;
        macro_action_selection = tool_macros[slot].count > 0 ? 1 : 0;
        mode = MODE_MACRO_ACTIONS;
        force_full_redraw = true;
    } else if (mode == MODE_PLAYLISTS && !playlist_add_mode &&
        !playlist_move_mode) {
        struct rbprep_node_record node;
        int index = playlist_node_at_visible(tree_selection, &node, NULL);

        playlist_action_selection = 0;
        playlist_action_node = -1;
        playlist_action_parent = tree_parent == RBPREP_ROOT_NODE
                               ? -1 : (int)tree_parent;
        if (index >= 0) {
            if (node.kind == 0)
                playlist_action_parent = index;
            playlist_action_node = index;
        }
        mode = MODE_PLAYLIST_ACTIONS;
        force_full_redraw = true;
    } else if (mode == MODE_PENDING &&
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
                         "REMOVE %c %.24s?",
                         pending_playlists[ordinal].operation,
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
            int delete_slot = cue_slot_for_delete(cue_slot);
            if (delete_slot >= 0) {
                set_cue_focus_slot(delete_slot);
                confirm_slot = cue_slot;
                rb->snprintf(confirm_message, sizeof(confirm_message),
                             "DELETE CUE %02d?", cue_slot + 1);
                begin_confirmation(CONFIRM_CUE_DELETE);
            } else {
                rb->splashf(HZ, "CUE %02d IS EMPTY", cue_slot + 1);
                restore_black_canvas();
            }
        } else if (tool == TOOL_CUE_SLOT || tool == TOOL_CUE_MOVE) {
            hotcues[cue_slot] = quantized_time(live_playhead_now());
            hotcue_colors[cue_slot] = 3;
            overview_dirty = true;
            if (!record_edit_change()) {
                rb->splash(HZ, "Cue set; journal failed");
                restore_black_canvas();
            } else if (auto_burn) {
                auto_burn_pending_changes();
            }
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
        set_output_gain(volume);
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

    if (macro_wheel_locked) {
        /* A fixed workflow value locks the tool parameter, not navigation.
           Repurpose the wheel as the deck's precise scrub control. */
        seek_by(direction * scrub_step_ms(), false);
        return;
    }
    if (tool == TOOL_SEEK || tool == TOOL_CUE_MOVE ||
        tool == TOOL_GRID_ORIGIN)
        seek_by(direction * scrub_step_ms(), false);
    else if (tool == TOOL_SCRUB_STEP) {
        scrub_step_index = (scrub_step_index +
            (direction > 0 ? 1 : ARRAYLEN(scrub_steps) - 1)) %
            ARRAYLEN(scrub_steps);
        mark_rbprep_config_dirty();
    }
    else if (tool == TOOL_ZOOM)
        change_zoom(direction > 0);
    else if (tool == TOOL_GAIN || tool == TOOL_IPOD_SEEK)
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
    else if (tool == TOOL_KEYLOCK) {
        keylock_enabled = direction > 0;
        apply_playback_rate();
        mark_rbprep_config_dirty();
        force_full_redraw = true;
    }
    else if (tool == TOOL_META_KEY) {
        int index = key_index_from_name(selected_key);

        stage_active_edit();
        if (index < 0)
            index = direction > 0 ? -1 : 0;
        set_selected_key_index(index + (direction > 0 ? 1 : -1));
        force_full_redraw = true;
    }
    else if (tool == TOOL_KEY_NOTATION) {
        key_notation = (key_notation +
            (direction > 0 ? 1 : ARRAYLEN(key_notation_styles) - 1)) %
            ARRAYLEN(key_notation_styles);
        mark_rbprep_config_dirty();
        force_full_redraw = true;
    }
    else if (tool == TOOL_PLAYLIST_MODE)
        playlist_playback = direction > 0;
    else if (tool == TOOL_WAVEFORM_STYLE || tool == TOOL_VIS_BOOMBOX ||
             tool == TOOL_VIS_EQ || tool == TOOL_VIS_TURNTABLE ||
             tool == TOOL_VIS_CANYON || tool == TOOL_VIS_ORBIT ||
             tool == TOOL_VIS_PHRASE || tool == TOOL_VIS_HARMONIC) {
        visualizer_mode = tool == TOOL_WAVEFORM_STYLE ? 0 :
                          tool == TOOL_VIS_BOOMBOX ? 1 :
                          tool == TOOL_VIS_EQ ? 2 :
                          tool == TOOL_VIS_TURNTABLE ? 3 :
                          tool == TOOL_VIS_CANYON ? 4 :
                          tool == TOOL_VIS_ORBIT ? 5 :
                          tool == TOOL_VIS_PHRASE ? 6 : 7;
        if (visualizer_mode == 0)
            stop_spectrum_capture();
        else
            start_spectrum_capture();
        mark_rbprep_config_dirty();
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
        step_cue_focus_slot(direction);
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
        grid_bpm_x100 = MAX(2000, MIN(25000,
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
        color_index = (normalize_track_color(color_index) +
            (direction > 0 ? 1 : RBPREP_TRACK_COLOR_COUNT - 1)) %
            RBPREP_TRACK_COLOR_COUNT;
        force_full_redraw = true;
    } else if (tool == TOOL_META_YEAR) {
        stage_active_edit();
        if (track_year == 0)
            track_year = 2000;
        else
            track_year = MAX(0, MIN(9999, track_year + direction));
        force_full_redraw = true;
    }
}

static void adjust_active_tool_coarse(int direction)
{
    enum rbprep_tool tool = active_tool();

    if (macro_wheel_locked) {
        if (quantize)
            beat_jump(direction);
        else
            seek_by(direction * 1000, false);
        return;
    }
    if (tool == TOOL_IPOD_SEEK)
        seek_by(direction * 5000, false);
    else if (tool == TOOL_SEEK && quantize)
        beat_jump(direction);
    else if (tool == TOOL_SEEK || tool == TOOL_CUE_MOVE ||
             tool == TOOL_GRID_ORIGIN)
        seek_by(direction * 1000, false);
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
        grid_bpm_x100 = MAX(2000, MIN(25000,
                                     grid_bpm_x100 + direction * 100));
    } else if (tool == TOOL_LOOP_IN || tool == TOOL_LOOP_OUT) {
        adjust_loop_marker(tool, direction * 1000);
    } else if (tool == TOOL_META_YEAR) {
        stage_active_edit();
        if (track_year == 0)
            track_year = 2000;
        else
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

static void normalize_macro_editor_cursor(void)
{
    int count = tool_macros[macro_manage_slot].count;
    int maximum;

    if (macro_edit_operation == 0 && count < RBPREP_MACRO_STEPS)
        maximum = count;
    else
        maximum = MAX(0, count - 1);
    macro_edit_position = MAX(0, MIN(maximum, macro_edit_position));
}

static void move_macro_editor_cursor(int direction)
{
    int count = tool_macros[macro_manage_slot].count;
    int positions = macro_edit_operation == 0 && count < RBPREP_MACRO_STEPS
                  ? count + 1 : MAX(1, count);

    macro_edit_position = (macro_edit_position +
        (direction > 0 ? 1 : positions - 1)) % positions;
    force_full_redraw = true;
}

static void cycle_macro_operation(int direction)
{
    macro_edit_operation = (macro_edit_operation +
        (direction > 0 ? 1 : 3)) % 4;
    normalize_macro_editor_cursor();
    force_full_redraw = true;
}

static void bank_macro_picker(int direction)
{
    macro_picker_page = (macro_picker_page +
        (direction > 0 ? 1 : ARRAYLEN(tool_pages) - 1)) %
        ARRAYLEN(tool_pages);
    macro_picker_tool = MIN(macro_picker_tool,
                            macro_picker_page_count() - 1);
    force_full_redraw = true;
}

static void move_macro_picker(int direction)
{
    int count = macro_picker_page_count();

    macro_picker_tool = (macro_picker_tool +
        (direction > 0 ? 1 : count - 1)) % count;
    force_full_redraw = true;
}

static void rename_macro(int slot)
{
    char name[RBPREP_MACRO_NAME];
    int start = 0;
    int end;

    if (slot < 0 || slot >= RBPREP_MACRO_COUNT)
        return;
    rb->strlcpy(name, tool_macros[slot].name, sizeof(name));
    if (!rbprep_keyboard(name, sizeof(name), "RENAME WORKFLOW")) {
        rb->lcd_setfont(FONT_SYSFIXED);
        restore_black_canvas();
        return;
    }
    rb->lcd_setfont(FONT_SYSFIXED);
    while (name[start] == ' ' || name[start] == '\t')
        start++;
    if (start)
        rb->memmove(name, name + start, rb->strlen(name + start) + 1);
    end = rb->strlen(name);
    while (end > 0 && (name[end - 1] == ' ' || name[end - 1] == '\t'))
        name[--end] = '\0';
    if (!name[0])
        rb->snprintf(name, sizeof(name), "WORKFLOW %c", 'A' + slot);
    rb->strlcpy(tool_macros[slot].name, name,
                sizeof(tool_macros[slot].name));
    macro_dirty = true;
    if (!save_tool_macros())
        rb->splash(HZ, "Workflow rename save failed");
    restore_black_canvas();
    force_full_redraw = true;
}

static void open_macro_editor(int slot)
{
    if (slot < 0 || slot >= RBPREP_MACRO_COUNT)
        return;
    macro_manage_slot = slot;
    macro_edit_position = 0;
    macro_edit_operation = tool_macros[slot].count > 0 ? 1 : 0;
    mode = MODE_MACRO_EDITOR;
    force_full_redraw = true;
}

static void store_macro_tool(enum rbprep_tool tool, int value,
                             unsigned char flags)
{
    struct rbprep_tool_macro *macro = &tool_macros[macro_manage_slot];
    int i;

    if (macro_edit_operation == 0) {
        if (macro->count >= RBPREP_MACRO_STEPS) {
            rb->splash(HZ, "Workflow is full");
            restore_black_canvas();
            return;
        }
        macro_edit_position = MAX(0, MIN(macro->count,
                                         macro_edit_position));
        for (i = macro->count; i > macro_edit_position; i--)
            macro->steps[i] = macro->steps[i - 1];
        macro->steps[macro_edit_position].tool = tool;
        macro->steps[macro_edit_position].value = value;
        macro->steps[macro_edit_position].flags = flags;
        macro->count++;
        macro_edit_position = MIN((int)macro->count,
                                  macro_edit_position + 1);
    } else {
        if (macro->count <= 0) {
            rb->splash(HZ, "Nothing to replace");
            restore_black_canvas();
            return;
        }
        macro_edit_position = MAX(0, MIN((int)macro->count - 1,
                                         macro_edit_position));
        macro->steps[macro_edit_position].tool = tool;
        macro->steps[macro_edit_position].value = value;
        macro->steps[macro_edit_position].flags = flags;
    }
    if (macro_active == macro_manage_slot)
        macro_position = MIN(macro_position, macro->count - 1);
    macro_dirty = true;
    if (!save_tool_macros())
        rb->splash(HZ, "Workflow save failed");
    mode = MODE_MACRO_EDITOR;
    normalize_macro_editor_cursor();
    overview_dirty = true;
    restore_black_canvas();
    force_full_redraw = true;
}

static void apply_macro_picker_tool(void)
{
    int count = macro_picker_page_count();
    enum rbprep_tool tool;

    if (macro_picker_tool < 0 || macro_picker_tool >= count)
        return;
    tool = macro_picker_tool_at(macro_picker_page, macro_picker_tool);
    macro_value_tool = tool;
    macro_value_new_step = true;
    macro_value_draft = macro_current_tool_value(tool);
    macro_value_choose = !macro_tool_supports_fixed_value(tool);
    macro_value_lock = !macro_value_choose;
    mode = MODE_MACRO_VALUE;
    force_full_redraw = true;
}

static void adjust_macro_value(int direction, bool coarse)
{
    enum rbprep_tool tool = macro_value_tool;
    int amount = coarse ? 10 : 1;

    if (!macro_tool_supports_fixed_value(tool))
        return;
    macro_value_choose = false;
    if (tool == TOOL_SCRUB_STEP) {
        macro_value_draft = (macro_value_draft +
            (direction > 0 ? 1 : ARRAYLEN(scrub_steps) - 1)) %
            ARRAYLEN(scrub_steps);
    } else if (tool == TOOL_ZOOM) {
        if (direction > 0)
            macro_value_draft = macro_value_draft >= RBPREP_MAX_ZOOM
                              ? 1 : MAX(1, macro_value_draft * 2);
        else
            macro_value_draft = macro_value_draft <= 1
                              ? RBPREP_MAX_ZOOM : macro_value_draft / 2;
    } else if (tool == TOOL_GAIN) {
        macro_value_draft = MAX(rb->sound_min(SOUND_VOLUME),
            MIN(rb->sound_max(SOUND_VOLUME),
                macro_value_draft + direction * amount));
    } else if (tool == TOOL_HOST_RPM || tool == TOOL_PLAY_RPM) {
        macro_value_draft = (macro_value_draft +
            (direction > 0 ? 1 : ARRAYLEN(rpm_styles) - 1)) %
            ARRAYLEN(rpm_styles);
    } else if (tool == TOOL_PITCH_BEND) {
        macro_value_draft = MAX(-1600, MIN(1600,
            macro_value_draft + direction * (coarse ? 100 : 10)));
    } else if (tool == TOOL_TEMPO) {
        macro_value_draft = MAX(5000, MIN(20000,
            macro_value_draft + direction * (coarse ? 100 : 10)));
    } else if (tool == TOOL_GRID_BPM) {
        macro_value_draft = MAX(2000, MIN(25000,
            macro_value_draft + direction * (coarse ? 100 : 1)));
    } else if (tool == TOOL_META_KEY) {
        macro_value_draft = (macro_value_draft +
            (direction > 0 ? 1 : ARRAYLEN(chromatic_key_names) - 1)) %
            ARRAYLEN(chromatic_key_names);
    } else if (tool == TOOL_CUE_SLOT || tool == TOOL_CUE_MOVE ||
               tool == TOOL_CUE_COLOR || tool == TOOL_CUE_DELETE) {
        macro_value_draft = (macro_value_draft +
            (direction > 0 ? 1 : 15)) & 15;
    } else if (tool == TOOL_LOOP_LENGTH) {
        macro_value_draft = (macro_value_draft +
            (direction > 0 ? 1 : ARRAYLEN(loop_length_names) - 1)) %
            ARRAYLEN(loop_length_names);
    } else {
        macro_value_draft = direction > 0;
    }
    force_full_redraw = true;
}

static void save_macro_value(void)
{
    struct rbprep_tool_macro *macro = &tool_macros[macro_manage_slot];
    int value = macro_value_choose ? RBPREP_MACRO_CHOOSE
                                   : macro_value_draft;
    unsigned char flags = !macro_value_choose && macro_value_lock
                        ? RBPREP_MACRO_LOCK_WHEEL : 0;

    if (macro_value_new_step) {
        store_macro_tool(macro_value_tool, value, flags);
        return;
    }
    if (macro_edit_position < 0 || macro_edit_position >= macro->count)
        return;
    macro->steps[macro_edit_position].value = value;
    macro->steps[macro_edit_position].flags = flags;
    macro_dirty = true;
    if (!save_tool_macros())
        rb->splash(HZ, "Workflow default save failed");
    mode = MODE_MACRO_EDITOR;
    overview_dirty = true;
    restore_black_canvas();
    force_full_redraw = true;
}

static void delete_macro_step(int slot, int position)
{
    struct rbprep_tool_macro *macro;
    int i;

    if (slot < 0 || slot >= RBPREP_MACRO_COUNT)
        return;
    macro = &tool_macros[slot];
    if (position < 0 || position >= macro->count)
        return;
    for (i = position; i + 1 < macro->count; i++)
        macro->steps[i] = macro->steps[i + 1];
    macro->count--;
    macro->steps[macro->count].tool = 0;
    macro->steps[macro->count].flags = 0;
    macro->steps[macro->count].value = RBPREP_MACRO_CHOOSE;
    if (macro_active == slot) {
        if (macro->count <= 0)
            macro_active = -1;
        else
            macro_position = MIN(macro_position, macro->count - 1);
    }
    normalize_macro_editor_cursor();
    macro_dirty = true;
    if (!save_tool_macros())
        rb->splash(HZ, "Workflow save failed");
    overview_dirty = true;
    force_full_redraw = true;
}

static void clear_macro(int slot)
{
    int i;

    if (slot < 0 || slot >= RBPREP_MACRO_COUNT)
        return;
    tool_macros[slot].count = 0;
    for (i = 0; i < RBPREP_MACRO_STEPS; i++) {
        tool_macros[slot].steps[i].tool = 0;
        tool_macros[slot].steps[i].flags = 0;
        tool_macros[slot].steps[i].value = RBPREP_MACRO_CHOOSE;
    }
    if (macro_active == slot)
        macro_active = -1;
    macro_edit_position = 0;
    macro_edit_operation = 0;
    macro_dirty = true;
    if (!save_tool_macros())
        rb->splash(HZ, "Workflow clear save failed");
    overview_dirty = true;
    force_full_redraw = true;
}

static void apply_macro_step(const struct rbprep_macro_step *step)
{
    enum rbprep_tool tool;
    int value;

    if (!step || step->tool >= TOOL_COUNT)
        return;
    tool = step->tool;
    value = step->value;
    tool_menu_active = false;
    select_tool(tool);
    macro_wheel_locked = value != RBPREP_MACRO_CHOOSE &&
                         !!(step->flags & RBPREP_MACRO_LOCK_WHEEL);
    if (value == RBPREP_MACRO_CHOOSE)
        goto done;

    if (tool == TOOL_SCRUB_STEP) {
        scrub_step_index = MAX(0, MIN((int)ARRAYLEN(scrub_steps) - 1,
                                     value));
    } else if (tool == TOOL_ZOOM) {
        zoom = MAX(1, MIN(RBPREP_MAX_ZOOM, value));
    } else if (tool == TOOL_GAIN) {
        set_output_gain(
            MAX(rb->sound_min(SOUND_VOLUME),
                MIN(rb->sound_max(SOUND_VOLUME), value)));
    } else if (tool == TOOL_HOST_RPM) {
        host_rpm_index = MAX(0, MIN((int)ARRAYLEN(rpm_styles) - 1, value));
        apply_playback_rate();
    } else if (tool == TOOL_PLAY_RPM) {
        played_rpm_index = MAX(0, MIN((int)ARRAYLEN(rpm_styles) - 1, value));
        apply_playback_rate();
    } else if (tool == TOOL_PITCH_BEND) {
        pitch_bend_x100 = MAX(-1600, MIN(1600, value));
        apply_playback_rate();
    } else if (tool == TOOL_TEMPO) {
        tempo_x100 = MAX(5000, MIN(20000, value));
        apply_playback_rate();
    } else if (tool == TOOL_PLAYLIST_MODE) {
        playlist_playback = !!value;
    } else if (tool == TOOL_KEYLOCK) {
        keylock_enabled = !!value;
        apply_playback_rate();
        mark_rbprep_config_dirty();
    } else if (tool == TOOL_META_KEY) {
        stage_active_edit();
        set_selected_key_index(value);
    } else if (tool == TOOL_KEY_NOTATION) {
        key_notation = MAX(0,
            MIN((int)ARRAYLEN(key_notation_styles) - 1, value));
        mark_rbprep_config_dirty();
    } else if (tool == TOOL_GRID_BPM) {
        stage_active_edit();
        grid_bpm_x100 = MAX(2000, MIN(25000, value));
    } else if (tool == TOOL_GRID_QUANTIZE) {
        stage_active_edit();
        quantize = !!value;
    } else if (tool == TOOL_CUE_SLOT || tool == TOOL_CUE_MOVE ||
               tool == TOOL_CUE_COLOR || tool == TOOL_CUE_DELETE) {
        set_cue_focus_slot(value);
    } else if (tool == TOOL_LOOP_LENGTH) {
        loop_length_index = MAX(0,
            MIN((int)ARRAYLEN(loop_length_names) - 1, value));
    } else if (tool == TOOL_LOOP_ACTIVE) {
        loop_active = !!value && loop_in >= 0 && loop_out > loop_in;
    }

done:
    /* Workflow playback is a real-time navigation path. The macro file owns
       stored defaults, so do not synchronously rewrite rbprep.cfg for every
       cell transition; that disk write was long enough to drop input edges. */
    overview_dirty = true;
    force_full_redraw = true;
}

static void activate_macro(int slot)
{
    if (slot < 0 || slot >= RBPREP_MACRO_COUNT ||
        tool_macros[slot].count <= 0) {
        rb->splash(HZ, "Hold SELECT to program this workflow");
        restore_black_canvas();
        return;
    }
    macro_active = slot;
    macro_position = 0;
    apply_macro_step(&tool_macros[slot].steps[0]);
}

static void step_macro(int direction)
{
    int count;

    if (macro_active < 0 || macro_active >= RBPREP_MACRO_COUNT)
        return;
    count = tool_macros[macro_active].count;
    if (count <= 0)
        return;
    macro_position = (macro_position +
        (direction > 0 ? 1 : count - 1)) % count;
    apply_macro_step(&tool_macros[macro_active].steps[macro_position]);
}

static void swap_macro(int direction)
{
    int next;

    if (macro_active < 0)
        return;
    next = (macro_active +
            (direction > 0 ? 1 : RBPREP_MACRO_COUNT - 1)) %
           RBPREP_MACRO_COUNT;
    if (tool_macros[next].count <= 0) {
        rb->splash(HZ / 2, "Other workflow is empty");
        restore_black_canvas();
        return;
    }
    macro_active = next;
    macro_position = MIN(macro_position, tool_macros[next].count - 1);
    apply_macro_step(&tool_macros[next].steps[macro_position]);
}

static void select_tool(enum rbprep_tool tool)
{
    int page;
    int index;

    for (page = 0; page < (int)ARRAYLEN(tool_pages); page++) {
        for (index = 0; index < tool_pages[page].count; index++) {
            if (tool_pages[page].tools[index] != tool)
                continue;
            mode = tool_pages[page].mode;
            if (mode == MODE_DECK) deck_tool = index;
            else if (mode == MODE_TEMPO) tempo_tool = index;
            else if (mode == MODE_GRID) grid_tool = index;
            else if (mode == MODE_CUES) cue_tool = index;
            else if (mode == MODE_LOOP) loop_tool = index;
            else if (mode == MODE_METADATA) metadata_tool = index;
            else if (mode == MODE_PITCH) pitch_tool = index;
            else if (mode == MODE_LIST) list_tool = index;
            else if (mode == MODE_PNAV) pnav_tool = index;
            else if (mode == MODE_VISUALIZER) visualizer_tool = index;
            else if (mode == MODE_VISUALIZER_TWO)
                visualizer_two_tool = index;
            else macro_tool = index;
            macro_tool_override = -1;
            return;
        }
    }

    /* Old saved workflows may still contain tools removed from the visible
       page layout. Keep those cells executable without putting legacy orbs
       back into the streamlined public-beta pages. */
    if (tool == TOOL_BURN_SONG || tool == TOOL_BURN_ALL)
        mode = MODE_LIST;
    else if (tool == TOOL_RELOAD_TRACK)
        mode = MODE_PNAV;
    else if (tool == TOOL_KEY_NOTATION)
        mode = MODE_MACRO;
    else
        return;
    macro_tool_override = tool;
}

static void toggle_seek_portal(void)
{
    if (!seek_portal_active) {
        seek_return_mode = mode;
        seek_return_tool = *tool_selection();
        seek_return_macro_active = macro_active;
        seek_return_macro_position = macro_position;
        seek_return_macro_lock = macro_wheel_locked;
        seek_return_macro_override = macro_tool_override;
        seek_portal_active = true;
        macro_active = -1;
        macro_wheel_locked = false;
        macro_tool_override = -1;
        select_tool(TOOL_SEEK);
    } else {
        seek_portal_active = false;
        mode = seek_return_mode;
        *tool_selection() = MAX(0, MIN(tool_count() - 1,
                                      seek_return_tool));
        macro_active = seek_return_macro_active;
        macro_position = seek_return_macro_position;
        macro_wheel_locked = seek_return_macro_lock;
        macro_tool_override = seek_return_macro_override;
    }
    finish_cue_audition();
    overview_dirty = true;
    force_full_redraw = true;
}

static void service_pending_select_click(void)
{
    if (!select_click_pending ||
        TIME_BEFORE(*rb->current_tick, select_click_deadline))
        return;
    select_click_pending = false;
    short_select();
    force_full_redraw = true;
}

static void browse_tool_menu(int direction)
{
    int *selected = tool_selection();
    int count = tool_count();

    discard_staged_edit();
    finish_cue_audition();
    macro_tool_override = -1;
    *selected = (*selected + (direction > 0 ? 1 : count - 1)) % count;
    force_full_redraw = true;
}

static void bank_tool_page(int direction)
{
    int page = tool_page_index();
    int selected = *tool_selection();
    int next = (page + (direction > 0 ? 1 : ARRAYLEN(tool_pages) - 1)) %
               ARRAYLEN(tool_pages);

    discard_staged_edit();
    finish_cue_audition();
    macro_tool_override = -1;
    mode = tool_pages[next].mode;
    *tool_selection() = MIN(selected, tool_pages[next].count - 1);
    force_full_redraw = true;
}

static void handle_escape_once(void)
{
    enum rbprep_mode old_mode = mode;

    if (tool_menu_active) {
        select_tool(tool_menu_original);
        tool_menu_active = false;
        force_full_redraw = true;
        return;
    }
    if (seek_portal_active) {
        toggle_seek_portal();
        return;
    }
    if (mode == MODE_MACRO_PICKER) {
        mode = MODE_MACRO_EDITOR;
        force_full_redraw = true;
        return;
    }
    if (mode == MODE_MACRO_VALUE) {
        mode = macro_value_new_step ? MODE_MACRO_PICKER
                                    : MODE_MACRO_EDITOR;
        force_full_redraw = true;
        return;
    }
    if (mode == MODE_MACRO_EDITOR) {
        if (macro_dirty && !save_tool_macros()) {
            rb->splash(HZ, "Workflow save retry failed");
            restore_black_canvas();
        }
        mode = MODE_MACRO_ACTIONS;
        force_full_redraw = true;
        return;
    }
    if (mode == MODE_MACRO_ACTIONS) {
        if (macro_dirty && !save_tool_macros()) {
            rb->splash(HZ, "Workflow save retry failed");
            restore_black_canvas();
        }
        if (tool_macros[macro_manage_slot].count > 0) {
            macro_active = macro_manage_slot;
            macro_position = MIN(macro_position,
                tool_macros[macro_manage_slot].count - 1);
            apply_macro_step(
                &tool_macros[macro_manage_slot].steps[macro_position]);
        } else {
            select_tool(macro_manage_slot == 0 ? TOOL_MACRO_ONE
                                               : TOOL_MACRO_TWO);
        }
        force_full_redraw = true;
        return;
    }
    if (macro_active >= 0) {
        macro_active = -1;
        overview_dirty = true;
        force_full_redraw = true;
        return;
    }

    discard_staged_edit();
    stop_editor_audio();
    if (mode == MODE_LIBRARY) {
        force_full_redraw = true;
    } else if (mode == MODE_FILTER) {
        mode = MODE_TRACKS;
    } else if (mode == MODE_USB) {
        /* Leaving this menu is a safety boundary, not merely navigation:
           always disarm data/audio, persist Power Only, then go back. */
        apply_usb_choice(0);
        mode = MODE_LIBRARY;
    } else if (mode == MODE_SETTINGS || mode == MODE_PENDING ||
               mode == MODE_INDEX) {
        mode = MODE_LIBRARY;
    } else if (mode == MODE_ACCENT) {
        mode = MODE_SETTINGS;
    } else if (mode == MODE_GENRES) {
        mode = MODE_METADATA;
    } else if (mode == MODE_PLAYLIST_ACTIONS) {
        mode = MODE_PLAYLISTS;
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
    } else if (mode == MODE_PLAYLISTS && playlist_move_mode) {
        playlist_move_mode = false;
        mode = MODE_PLAYLIST_ACTIONS;
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
    if (old_mode != MODE_LIBRARY && mode == MODE_LIBRARY)
        animate_main_return_transition();
    force_full_redraw = true;
}

static void boot_round_box(int x, int y, int width, int height, int radius)
{
    rb->lcd_fillrect(x + radius, y, width - radius * 2, height);
    rb->lcd_fillrect(x, y + radius, width, height - radius * 2);
    xlcd_fillcircle(x + radius, y + radius, radius);
    xlcd_fillcircle(x + width - radius - 1, y + radius, radius);
    xlcd_fillcircle(x + radius, y + height - radius - 1, radius);
    xlcd_fillcircle(x + width - radius - 1,
                    y + height - radius - 1, radius);
}

static void boot_fill_clipped_rect(int x, int y, int width, int height)
{
    int left = MAX(0, x);
    int top = MAX(0, y);
    int right = MIN(LCD_WIDTH, x + width);
    int bottom = MIN(LCD_HEIGHT, y + height);

    if (right > left && bottom > top)
        rb->lcd_fillrect(left, top, right - left, bottom - top);
}

static void draw_rekordpod_boot_splash(void)
{
    static const int8_t dot_x[12] = {
        18, 16, 9, 0, -9, -16, -18, -16, -9, 0, 9, 16
    };
    static const int8_t dot_y[12] = {
        0, 9, 16, 18, 16, 9, 0, -9, -16, -18, -16, -9
    };
    int frame;

    rb->lcd_set_background(LCD_BLACK);

    for (frame = 0; frame < 96; frame++) {
        int record_y;
        int sink_step;
        int morph = MAX(0, MIN(20, frame - 64));
        int record_radius = 24 + morph * 15 / 20;
        int label_radius = 7 + morph * 6 / 20;
        int phase = frame % 12;
        int progress = frame * 100 / 95;

        if (frame < 20) {
            record_y = 60;
        } else if (frame < 68) {
            sink_step = frame - 20;
            record_y = 60 + 91 * sink_step * sink_step / (47 * 47);
        } else {
            record_y = 151;
        }

        rb->lcd_clear_display();
        rb->lcd_set_foreground(theme_body_shadow);
        boot_round_box(103, 8, 114, 200, 12);
        rb->lcd_set_foreground(theme_body);
        boot_round_box(106, 11, 108, 194, 10);

        /* The iPod starts without controls: only its screen and the record
           exist. The record then falls through the body and becomes both the
           click wheel and its expanding SELECT button. */
        rb->lcd_set_foreground(LCD_RGBPACK(72, 78, 74));
        boot_round_box(116, 23, 88, 74, 5);
        rb->lcd_set_foreground(LCD_RGBPACK(8, 11, 9));
        rb->lcd_fillrect(119, 26, 82, 68);

        rb->lcd_set_foreground(morph > 10 ? theme_wheel : LCD_BLACK);
        xlcd_fillcircle(LCD_WIDTH / 2, record_y, record_radius);
        rb->lcd_set_foreground(theme_wheel_outline);
        xlcd_drawcircle(LCD_WIDTH / 2, record_y, record_radius);
        if (morph < 16) {
            rb->lcd_set_foreground(LCD_RGBPACK(48, 54, 50));
            xlcd_drawcircle(LCD_WIDTH / 2, record_y,
                            MAX(label_radius + 3, record_radius - 5));
            if (morph < 10)
                xlcd_drawcircle(LCD_WIDTH / 2, record_y,
                                MAX(label_radius + 2, record_radius - 10));
        }
        rb->lcd_set_foreground(LCD_WHITE);
        xlcd_fillcircle(LCD_WIDTH / 2, record_y, label_radius);
        rb->lcd_set_foreground(LCD_RGBPACK(115, 122, 118));
        xlcd_drawcircle(LCD_WIDTH / 2, record_y, label_radius);
        if (morph < 10) {
            rb->lcd_set_foreground(LCD_RGBPACK(105, 112, 108));
            xlcd_fillcircle(LCD_WIDTH / 2, record_y, 2);
            rb->lcd_set_foreground(RBPREP_GREEN);
            xlcd_fillcircle(LCD_WIDTH / 2 + dot_x[phase],
                            record_y + dot_y[phase], 2);
        }

        /* Once vinyl has completed its click-wheel transformation, name the
           instrument on its own screen before the camera dives into it. */
        if (morph == 20)
            centered_text(119, 82, 55, "rekordpod", LCD_WHITE);

        rb->lcd_set_foreground(LCD_RGBPACK(30, 38, 33));
        rb->lcd_fillrect(109, 218, 102, 5);
        rb->lcd_set_foreground(RBPREP_GREEN);
        rb->lcd_fillrect(110, 219, MAX(1, progress), 3);
        rb->lcd_update();
        rb->sleep(MAX(1, HZ / 24));
    }

    /* Push into the completed iPod's screen. The screen rectangle expands
       to the physical LCD while the body, wheel and SELECT control leave the
       frame, so the animation resolves into the actual Rekordpod UI. */
    for (frame = 0; frame <= 32; frame++) {
        int t = frame * 1024 / 32;
        int eased = (long long)t * t * (3072 - 2 * t) /
                    (1024 * 1024);
        int sx = 119 - 119 * eased / 1024;
        int sy = 26 - 26 * eased / 1024;
        int sw = 82 + (LCD_WIDTH - 82) * eased / 1024;
        int sh = 68 + (LCD_HEIGHT - 68) * eased / 1024;
        int scale = sw * 1024 / 82;
        int bx = sx - 13 * scale / 1024;
        int by = sy - 15 * scale / 1024;
        int bw = sw + 26 * scale / 1024;
        int bh = sh + 126 * scale / 1024;
        int inset = MAX(2, 3 * scale / 1024);
        int wheel_cy = sy + 125 * scale / 1024;
        int wheel_radius = 39 * scale / 1024;

        rb->lcd_clear_display();
        rb->lcd_set_foreground(theme_body_shadow);
        boot_fill_clipped_rect(bx, by, bw, bh);
        rb->lcd_set_foreground(theme_body);
        boot_fill_clipped_rect(bx + inset, by + inset,
                               bw - inset * 2, bh - inset * 2);
        if (wheel_radius < 110 && wheel_cy - wheel_radius < LCD_HEIGHT) {
            rb->lcd_set_foreground(theme_wheel);
            xlcd_fillcircle(LCD_WIDTH / 2, wheel_cy, wheel_radius);
            rb->lcd_set_foreground(theme_wheel_outline);
            xlcd_drawcircle(LCD_WIDTH / 2, wheel_cy, wheel_radius);
            rb->lcd_set_foreground(theme_body);
            xlcd_fillcircle(LCD_WIDTH / 2, wheel_cy,
                            MAX(5, 13 * scale / 1024));
        }
        rb->lcd_set_foreground(LCD_BLACK);
        boot_fill_clipped_rect(sx, sy, sw, sh);
        if (frame < 12)
            centered_text(sx, sw, sy + sh / 2 - 4,
                          "rekordpod", LCD_WHITE);
        rb->lcd_update();
        rb->sleep(MAX(1, HZ / 24));
    }
}

static bool write_usb_status_snapshot(void)
{
    unsigned char data[48];
    unsigned char verify[48];

    if (!recent_tracks_ready)
        refresh_recent_track_count();
    refresh_pending_summary();
    rb->memset(data, 0, sizeof(data));
    rb->memcpy(data, "RUS1", 4);
    write_u32(data + 4, 1);
    write_u32(data + 8, library_track_count);
    write_u32(data + 12, library_node_count);
    write_u32(data + 16, library_member_count);
    write_u32(data + 20, recent_tracks_30d);
    write_u32(data + 24, total_plays);
    write_u32(data + 28, uptime_tracks_burned);
    write_u32(data + 32, pending_snapshot_count + pending_playlist_count);
    write_u32(data + 36, theme_accent);
    write_u32(data + 40, theme_body);
    write_u32(data + 44, theme_wheel);

    rbprep_store_ensure_state_dir(rb);
    return rbprep_store_write_verified(rb, RBPREP_USB_STATUS,
                                       data, sizeof(data),
                                       verify, sizeof(verify));
}

static bool persist_usb_arm_state(void)
{
    if (!flush_deferred_edit())
        return false;
    if (macro_dirty && !save_tool_macros())
        return false;
    if (!save_rbprep_config())
        return false;
    return write_usb_status_snapshot();
}

static void capture_usb_library_context(void)
{
    struct rbprep_node_record node;

    usb_library_context_captured = library_fd >= 0;
    usb_active_playlist_source_id = 0;
    usb_tree_parent_source_id = 0;
    if (!usb_library_context_captured)
        return;
    if (active_playlist_node >= 0 &&
        read_node_record(active_playlist_node, &node))
        usb_active_playlist_source_id = node.source_id;
    if (tree_parent != RBPREP_ROOT_NODE &&
        read_node_record(tree_parent, &node))
        usb_tree_parent_source_id = node.source_id;
}

static bool refresh_selected_metadata_after_usb(void)
{
    struct rbprep_track_record track;
    char path[MAX_PATH];
    const char *extension;

    if (!read_track_record(selected_track_index, &track) ||
        !read_index_string(track.path_offset, path, sizeof(path)))
        return false;
    extension = rb->strrchr(path, '.');
    rb->strlcpy(selected_extension, extension ? extension : "",
                sizeof(selected_extension));
    if (!read_index_string(track.title_offset, selected_title,
                           sizeof(selected_title)) ||
        !read_index_string(track.artist_offset, selected_artist,
                           sizeof(selected_artist)) ||
        !read_index_string(track.genre_offset, selected_genre,
                           sizeof(selected_genre)))
        return false;
    if (!track.key_offset ||
        !read_index_string(track.key_offset, selected_key,
                           sizeof(selected_key)))
        selected_key[0] = '\0';
    if (!track.comments_offset ||
        !read_index_string(track.comments_offset, selected_comments,
                           sizeof(selected_comments)))
        selected_comments[0] = '\0';
    track_year = track.year;
    return true;
}

static void restore_usb_library_context(bool library_ready)
{
    int parent = -1;

    /* Search rows and shuffle permutations contain physical RBI row numbers;
       never reuse them after the host may have replaced the index. */
    if (search_result_fd >= 0) {
        rb->close(search_result_fd);
        search_result_fd = -1;
    }
    search_active = false;
    collection_shuffle_active = false;
    search_result_count = 0;
    active_playlist_node = -1;
    selected_track_index = -1;
    playing_track_row = -1;
    track_selection = track_top = 0;

    if (!library_ready) {
        tree_parent = RBPREP_ROOT_NODE;
        tree_child_count = 0;
        tree_selection = tree_top = 0;
        track_row_count = 0;
        invalidate_playlist_cache();
        return;
    }

    if (selected_track_id >= 0)
        selected_track_index = find_track_index_by_id(selected_track_id);
    if (usb_library_context_captured && usb_tree_parent_source_id)
        parent = find_node_index_by_source_id(usb_tree_parent_source_id);
    refresh_tree_children(parent >= 0 ? (uint32_t)parent
                                      : RBPREP_ROOT_NODE);
    tree_selection = tree_top = 0;

    if (usb_library_context_captured && usb_active_playlist_source_id)
        refresh_active_playlist_context(usb_active_playlist_source_id);
    else {
        int row = find_track_row_in_collection(selected_track_index);

        track_row_count = library_track_count;
        if (row >= 0) {
            playing_track_row = row;
            track_selection = row;
            track_top = MAX(0, MIN(row,
                        MAX(0, track_row_count - RBPREP_LIST_ROWS)));
        }
    }
}

static void prepare_rekordpod_for_usb(void *parameter)
{
    (void)parameter;

    capture_usb_library_context();

    /* USB DAC owns the clickwheel while connected. Start from a quiet,
       deterministic gain without permanently replacing the user's normal
       playback volume. Rekordpod restores this saved value on disconnect. */
    if (capabilities.usb_audio && usb_armed_selection == 2 &&
        !dac_gain_override) {
        dac_saved_volume = rb->global_status->volume;
        dac_gain_override = true;
        set_output_gain(RBPREP_DAC_INITIAL_GAIN_DB);
    }

    stop_editor_audio();
    usb_persistence_warning = !persist_usb_arm_state();
    usb_unsaved_edit_valid = usb_persistence_warning && track_edit_dirty &&
                             pack_current_edit_snapshot(usb_unsaved_edit);
    stop_spectrum_capture();
    restore_playback_rate();
    rbprep_wave_close(&wave_reader);
    rbprep_grid_close(&grid_reader);
    rbprep_wave_index_close(&wave_index);
#ifdef HAVE_ADJUSTABLE_CPU_FREQ
    if (deck_cpu_boosted) {
        rb->cpu_boost(false);
        deck_cpu_boosted = false;
    }
#endif
    set_storage_performance_mode(false);
    if (library_fd >= 0) {
        rb->close(library_fd);
        library_fd = -1;
    }
    if (genre_fd >= 0) {
        rb->close(genre_fd);
        genre_fd = -1;
    }
    if (search_result_fd >= 0) {
        rb->close(search_result_fd);
        search_result_fd = -1;
    }
}

static void resume_rekordpod_after_usb(void)
{
    struct mp3entry *id3;
    bool library_ready;

    /* The stock USB screen blocks until disconnect. Resume this plugin in
       place afterwards: restarting through PLUGIN_GOTO_PLUGIN is dependent
       on the caller's menu context and can fall through to Rockbox. */
    rb->lcd_setfont(FONT_SYSFIXED);
    main_viewport = rb->lcd_set_viewport(NULL);
    restore_dac_gain();
    apply_usb_choice(0);
    recent_tracks_ready = false;
    library_ready = open_library_index();
    load_smart_query_flags();
    restore_usb_library_context(library_ready);
    load_genre_rollup();
    if (!library_ready) {
        rb->splash(HZ * 2, "Library index unavailable");
        restore_black_canvas();
    }
    if (library_ready && selected_track_id >= 0 &&
        (selected_track_index < 0 ||
         !refresh_selected_metadata_after_usb())) {
        selected_track_id = -1;
        selected_title[0] = selected_artist[0] = selected_genre[0] = '\0';
        selected_key[0] = selected_comments[0] = selected_extension[0] = '\0';
        clear_analysis();
        rb->splash(HZ * 2, "Loaded track left the library");
        restore_black_canvas();
    } else if (selected_track_id >= 0) {
        bool resume_after_prepare = false;

        if (!quiesce_audio_for_track_load(&resume_after_prepare)) {
            clear_analysis();
            rb->splash(HZ * 2, "Audio pause timed out");
        } else {
            bool waveform_loaded = load_waveform(selected_track_id);

            if (waveform_loaded) {
                load_latest_edit(selected_track_id);
                if (usb_unsaved_edit_valid &&
                    (int)read_u32(usb_unsaved_edit + 8) ==
                    selected_track_id) {
                    apply_edit_record(usb_unsaved_edit);
                    track_edit_dirty = true;
                }
            }
            if (resume_after_prepare &&
                (rb->audio_status() & AUDIO_STATUS_PAUSE))
                rb->audio_resume();
            if (!waveform_loaded) {
                rb->splash(HZ * 2, "Waveform preparation failed");
                clear_analysis();
                if (usb_unsaved_edit_valid &&
                    (int)read_u32(usb_unsaved_edit + 8) ==
                    selected_track_id) {
                    apply_edit_record(usb_unsaved_edit);
                    track_edit_dirty = true;
                }
            }
        }
    }
    /* Never replace an unsaved in-memory workflow after a failed media
       write. A successful pre-USB save clears macro_dirty; otherwise keep
       the user's sequence intact and retry at the next save boundary. */
    if (!macro_dirty) {
        load_tool_macros();
        restore_saved_macro_selection();
    } else {
        save_tool_macros();
    }
    load_macro_links();
    refresh_pending_summary();
    set_storage_performance_mode(!display_locked);
    update_deck_cpu_boost();
    storage_keepalive_deadline = *rb->current_tick + HZ * 30;
    apply_playback_rate();
    if (!display_locked) {
        backlight_ignore_timeout();
#ifdef HAVE_BACKLIGHT
        rb->backlight_on();
#endif
        start_spectrum_capture();
    } else {
        backlight_use_settings();
    }
    id3 = rb->audio_current_track();
    if (selected_track_id >= 0 && id3)
        playhead = clamp_playhead(id3->elapsed);
    reset_play_clock(playhead, *rb->current_tick);
    usb_selection = usb_armed_selection = 0;
    usb_library_context_captured = false;
    usb_unsaved_edit_valid = false;
    overview_dirty = true;
    force_full_redraw = true;
    restore_black_canvas();
    if (usb_persistence_warning) {
        rb->splash(HZ * 2, "State save needs retry");
        usb_persistence_warning = false;
        restore_black_canvas();
    }
    rb->button_clear_queue();
}

static bool handle_usb_system_event(int button)
{
    if (rb->default_event_handler_ex(button, prepare_rekordpod_for_usb,
                                     NULL) != SYS_USB_CONNECTED)
        return false;
    resume_rekordpod_after_usb();
    return true;
}

static int next_frame_interval(int rate, int *remainder)
{
    int ticks;

    rate = MAX(1, rate);
    *remainder += HZ;
    ticks = *remainder / rate;
    *remainder %= rate;
    return MAX(1, ticks);
}

enum plugin_status plugin_start(const void *parameter)
{
    int button;
    int pressed = BUTTON_NONE;
    bool select_hold_fired = false;
    bool redraw = true;
    bool autoboot_launch = parameter &&
        !rb->strcmp((const char *)parameter, "autoboot");
    long frame_deadline;
    int frame_rate = 0;
    int frame_remainder = 0;
    void *beat_workspace;
    void *index_workspace;

    rbprep_caps_detect(&capabilities, rb);
    rbprep_wave_init(&wave_reader, rb, capabilities.workspace,
                     capabilities.waveform_cache_bytes,
                     capabilities.io_slice_bytes);
    beat_workspace = capabilities.workspace
        ? (unsigned char *)capabilities.workspace +
          capabilities.waveform_cache_bytes : NULL;
    rbprep_grid_init(&grid_reader, rb, beat_workspace,
                     capabilities.beat_cache_bytes);
    index_workspace = beat_workspace
        ? (unsigned char *)beat_workspace + capabilities.beat_cache_bytes
        : NULL;
    rbprep_wave_index_init(&wave_index, rb, index_workspace,
                           capabilities.waveform_index_bytes,
                           capabilities.io_slice_bytes);
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
    configfile_load(RBPREP_CONFIG_FILE, rbprep_config,
                    ARRAYLEN(rbprep_config), RBPREP_CONFIG_VERSION);
    config_dirty = (!!autoboot_enabled ==
                    rb->file_exists(RBPREP_AUTOBOOT_OFF));
    accent_hue = MAX(0, MIN(359, accent_hue));
    accent_saturation = MAX(0, MIN(100, accent_saturation));
    accent_brightness = MAX(10, MIN(100, accent_brightness));
    body_hue = MAX(0, MIN(359, body_hue));
    body_saturation = MAX(0, MIN(100, body_saturation));
    body_brightness = MAX(10, MIN(100, body_brightness));
    wheel_hue = MAX(0, MIN(359, wheel_hue));
    wheel_saturation = MAX(0, MIN(100, wheel_saturation));
    wheel_brightness = MAX(5, MIN(100, wheel_brightness));
    platter_wheel_mode = !!platter_wheel_mode;
    turntable_arm_style = !!turntable_arm_style;
    turntable_headshell_style = MAX(0,
        MIN((int)ARRAYLEN(turntable_headshell_styles) - 1,
            turntable_headshell_style));
    load_device_usb_name();
    update_theme_colors();
    mode = MODE_LIBRARY;
    selection = 0;
    main_wheel_phase_fp = 0;
    main_wheel_velocity_fp = 0;
    main_wheel_touch_position = -1;
    main_wheel_motion_tick = *rb->current_tick;
    main_name_scroll_deadline = *rb->current_tick;
    main_transition_thumbnail_valid = false;
    transition_render_only = false;
    activity_ticker_progress = 0;
    activity_ticker_until = 0;
    activity_ticker_last_update = 0;
    activity_ticker_burst = 0;
    playhead = 0;
    zoom = 1;
    grid_offset = 0;
    grid_beat_shift = 0;
    cue_slot = 0;
    deck_tool = tempo_tool = grid_tool = cue_tool = loop_tool = 0;
    metadata_tool = pitch_tool = visualizer_tool = visualizer_two_tool = 0;
    list_tool = pnav_tool = macro_tool = 0;
    rating = 0;
    color_index = RBPREP_TRACK_COLOR_NONE;
    track_year = 0;
    quantize = true;
    playlist_playback = !!autoplay_enabled;
    playlist_add_mode = false;
    playlist_move_mode = false;
    force_track_reload = false;
    playlist_action_selection = 0;
    playlist_action_node = playlist_action_parent = -1;
    playing_track_row = -1;
    audio_was_running = !!(rb->audio_status() & AUDIO_STATUS_PLAY);
    confirm_active = false;
    exit_requested = false;
    staged_tool = -1;
    staged_cue_slot = -1;
    cue_audition_active = false;
    cue_audition_latched = false;
    overview_playhead_white = true;
    seek_state = SEEK_IDLE;
    seek_prepared = false;
#ifdef HAVE_WHEEL_POSITION
    seek_wheel_touch_position = -1;
    seek_wheel_velocity_fp = 0;
    seek_wheel_delta_fp = 0;
    seek_wheel_arm_delta = 0;
    seek_wheel_gesture_tracked = false;
    seek_wheel_blocked_until_release = false;
    seek_wheel_motion_tick = *rb->current_tick;
#endif
    suppress_menu = suppress_play = false;
    suppress_left = suppress_right = false;
    macro_chord_button = BUTTON_NONE;
    tool_menu_active = false;
    macro_active = -1;
    macro_position = 0;
    macro_manage_slot = 0;
    macro_action_selection = 0;
    macro_edit_position = 0;
    macro_edit_operation = 0;
    macro_picker_page = 0;
    macro_picker_tool = 0;
    macro_wheel_locked = false;
    macro_tool_override = -1;
    burn_request = BURN_REQUEST_NONE;
    select_pressed_time = 0;
    select_click_pending = false;
    select_double_consumed = false;
    seek_portal_active = false;
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
    collection_shuffle_active = false;
    search_result_count = 0;
    pending_selection = pending_top = 0;
    recent_tracks_ready = false;
    uptime_tracks_burned = 0;
    play_stat_track_id = -1;
    play_stat_last_tick = *rb->current_tick;
    play_stat_ticks = 0;
    play_stat_counted = false;
    track_edit_dirty = false;
    usb_selection = 0;
    dac_saved_volume = rb->global_status->volume;
    dac_gain_override = false;
    host_rpm_index = played_rpm_index = 0;
    pitch_bend_x100 = 0;
    tempo_x100 = PITCH_SPEED_100;
    original_pitch = rb->sound_get_pitch();
    original_stretch = rb->dsp_get_timestretch();
    original_timestretch_enabled =
        rb->global_settings->timestretch_enabled;
    playback_rate_changed = false;
    waveform_half = !!waveform_half;
    visualizer_mode = MAX(0, MIN((int)ARRAYLEN(visualizer_styles) - 1,
                                 visualizer_mode));
    if (!save_on_track_load || auto_burn)
        mark_rbprep_config_dirty();
    save_on_track_load = true;
    auto_burn = false;
    click_sound = !!click_sound;
    keylock_enabled = !!keylock_enabled;
    key_notation = MAX(0,
        MIN((int)ARRAYLEN(key_notation_styles) - 1, key_notation));
    ipod_seek_first = !!ipod_seek_first;
    update_player_tool_order();
    scrub_step_index = MAX(0, MIN((int)ARRAYLEN(scrub_steps) - 1,
                                  scrub_step_index));
    host_rpm_index = MAX(0, MIN((int)ARRAYLEN(rpm_styles) - 1,
                                host_rpm_index));
    played_rpm_index = MAX(0, MIN((int)ARRAYLEN(rpm_styles) - 1,
                                  played_rpm_index));
    tempo_x100 = MAX(5000, MIN(20000, tempo_x100));
    apply_usb_choice(0);
    usb_event_registered = rb->add_event(SYS_EVENT_USB_INSERTED,
                                         rbprep_usb_inserted);
    usb_extract_event_registered = rb->add_event(SYS_EVENT_USB_EXTRACTED,
                                                 rbprep_usb_extracted);
    apply_playback_rate();
    if (!display_locked)
        start_spectrum_capture();
    rebuild_waveform_height_lut();
    clear_analysis();
    load_smart_query_flags();
    open_library_index();
    load_genre_rollup();
    load_tool_macros();
    restore_saved_macro_selection();
    load_macro_links();
    refresh_pending_summary();
    if (autoboot_launch)
        draw_rekordpod_boot_splash();
    if (auto_burn && (pending_snapshot_count > 0 ||
                      pending_playlist_count > 0))
        burn_request = BURN_REQUEST_ALL;
    reset_play_clock(playhead, *rb->current_tick);
    overview_deadline = *rb->current_tick;
    hud_scroll_deadline = *rb->current_tick;
    status_deadline = *rb->current_tick;
    frame_deadline = *rb->current_tick;
    storage_keepalive_deadline = *rb->current_tick + HZ * 30;

    /* Discard the release of SELECT used to launch the plugin. */
    rb->button_clear_queue();
    while (true) {
        struct mp3entry *id3 = rb->audio_current_track();
        if (service_display_lock())
            redraw = true;
        update_deck_cpu_boost();
        update_spectrum_capture_state();
        service_storage_keepalive();
        if (selected_track_id >= 0 && id3 && id3->length > 0) {
            track_length = id3->length;
            if (track_year == 0 && id3->year > 0) {
                track_year = id3->year;
                force_full_redraw = true;
            }
        }
        if (service_seek_wheel_physics())
            redraw = true;
        if (service_audio_seek())
            redraw = true;
        if (update_play_clock())
            redraw = true;
        service_play_statistics();
        service_config_persistence();
        service_pending_select_click();
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

        /* Rendering never owns the input rate. Coalesce rapid wheel/chord
           edges into the next frame so the queue is drained before drawing;
           menus remain event-driven while the deck keeps its timecode clock. */
        if (!display_locked &&
            !TIME_BEFORE(*rb->current_tick, frame_deadline) &&
            (force_full_redraw ||
             (mode == MODE_LIBRARY &&
              (!TIME_BEFORE(*rb->current_tick, main_name_scroll_deadline) ||
               main_wheel_motion_active())) ||
             mode >= MODE_DECK || redraw)) {
            long frame_start = *rb->current_tick;
            int wanted_rate = mode >= MODE_DECK
                            ? MAX(1, capabilities.deck_fps)
                            : HZ / RBPREP_MENU_FRAME_TICKS;
            int interval;

            if (wanted_rate != frame_rate) {
                frame_rate = wanted_rate;
                frame_remainder = 0;
            }
            interval = next_frame_interval(frame_rate, &frame_remainder);
            draw_screen();
            {
                unsigned long duration = *rb->current_tick - frame_start;

                maximum_frame_ticks = MAX(maximum_frame_ticks, duration);
                if (duration > (unsigned long)interval)
                    late_frame_count++;
            }
            redraw = false;
            frame_deadline += interval;
            if (TIME_BEFORE(frame_deadline, *rb->current_tick + 1))
                frame_deadline = *rb->current_tick + 1;
        }
        button = rb->button_get_w_tmo(display_locked ? MAX(1, HZ / 20) : 1);
        if (button != BUTTON_NONE)
            redraw = true;
        if (select_click_pending && button != BUTTON_NONE &&
            button != BUTTON_SELECT) {
            /* Keep a single click attached to the tool on which it began.
               A different control commits it before that control is handled. */
            select_click_pending = false;
            short_select();
            if (confirm_active)
                continue;
        }
        if (macro_chord_button != BUTTON_NONE) {
            int chord_status = rb->button_status();
            int chord_button = macro_chord_button;

            /* A macro chord only arms another workflow cell. Release the
               latch as soon as its companion direction is physically up so
               SELECT may remain held while LEFT/RIGHT is tapped repeatedly.
               `pressed` stays empty for the whole hold, so the eventual
               SELECT release cannot execute the armed cell; execution still
               requires a deliberate new SELECT press. */
            if (!(chord_status & chord_button)) {
                macro_chord_button = BUTTON_NONE;
                pressed = BUTTON_NONE;
                if (chord_button == BUTTON_LEFT)
                    suppress_left = false;
                else if (chord_button == BUTTON_RIGHT)
                    suppress_right = false;
                else if (chord_button == BUTTON_MENU) {
                    suppress_menu = false;
                    menu_button_down = false;
                    menu_hold_fired = false;
                } else if (chord_button == BUTTON_PLAY) {
                    suppress_play = false;
                }
                select_hold_fired = !!(chord_status & BUTTON_SELECT);
                suppress_menu = suppress_play = false;
                suppress_left = suppress_right = false;
                menu_button_down = false;
                menu_hold_fired = false;
            }
            continue;
        }
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
                if (confirm_action == CONFIRM_TRACK_LOAD)
                    confirm_choice = MAX(0, confirm_choice - 1);
                else {
                    confirm_ok = false;
                    confirm_choice = 0;
                }
                force_full_redraw = true;
            } else if (button == BUTTON_RIGHT ||
                       button == BUTTON_SCROLL_FWD ||
                       button == (BUTTON_SCROLL_FWD | BUTTON_REPEAT)) {
                if (confirm_action == CONFIRM_TRACK_LOAD)
                    confirm_choice = MIN(2, confirm_choice + 1);
                else {
                    confirm_ok = true;
                    confirm_choice = 1;
                }
                force_full_redraw = true;
            } else if (button == (BUTTON_MENU | BUTTON_REL)) {
                finish_confirmation(false);
            } else if (button == (BUTTON_SELECT | BUTTON_REL)) {
                finish_confirmation(confirm_ok);
            } else if (handle_usb_system_event(button))
                force_full_redraw = true;
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
            if (select_click_pending && mode >= MODE_DECK &&
                !tool_menu_active &&
                TIME_BEFORE(*rb->current_tick, select_click_deadline)) {
                select_click_pending = false;
                select_double_consumed = true;
                pressed = BUTTON_NONE;
                if (cue_audition_active)
                    finish_cue_audition();
                toggle_seek_portal();
                break;
            }
            select_hold_fired = false;
            update_play_clock();
            select_pressed_time = live_playhead_now();
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
            } else if (mode == MODE_MACRO_VALUE) {
                if (macro_tool_supports_fixed_value(macro_value_tool))
                    macro_value_choose = !macro_value_choose;
                if (!macro_value_choose)
                    macro_value_lock = true;
                force_full_redraw = true;
            } else if (mode == MODE_MACRO_EDITOR) {
                cycle_macro_operation(-1);
            } else if (mode == MODE_MACRO_PICKER) {
                bank_macro_picker(-1);
            } else if (mode >= MODE_DECK) {
                if (tool_menu_active) {
                    bank_tool_page(-1);
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
                bank_tool_page(1);
                break;
            }
            if (pressed != BUTTON_PLAY)
                break;
            pressed = BUTTON_NONE;
            if (mode == MODE_MACRO_VALUE) {
                if (!macro_value_choose)
                    macro_value_lock = !macro_value_lock;
                force_full_redraw = true;
            } else if (mode == MODE_MACRO_EDITOR) {
                cycle_macro_operation(1);
            } else if (mode == MODE_MACRO_PICKER) {
                bank_macro_picker(1);
            } else if (mode == MODE_MACRO_ACTIONS) {
                /* Playback is deliberately inert inside workflow setup. */
            } else if (mode == MODE_TRACKS && active_playlist_node < 0) {
                filter_selection = 0;
                mode = MODE_FILTER;
                force_full_redraw = true;
            } else if (mode == MODE_PLAYLISTS && playlist_move_mode) {
                playlist_action_parent_id =
                    playlist_parent_source_id(
                        tree_parent == RBPREP_ROOT_NODE
                        ? -1 : (int)tree_parent);
                if (tree_parent != RBPREP_ROOT_NODE &&
                    !playlist_action_parent_id) {
                    rb->splash(HZ * 2,
                               "Rebuild cache for stable folder IDs");
                    restore_black_canvas();
                } else {
                    rb->snprintf(confirm_message, sizeof(confirm_message),
                                 "MOVE %.22s HERE?",
                                 playlist_action_name);
                    begin_confirmation(CONFIRM_PLAYLIST_MOVE);
                }
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
            if (select_double_consumed) {
                select_double_consumed = false;
                select_hold_fired = false;
                pressed = BUTTON_NONE;
                break;
            }
            if (pressed != BUTTON_SELECT)
                break;
            pressed = BUTTON_NONE;
            if (tool_menu_active) {
                enum rbprep_tool chosen = active_tool();
                tool_menu_active = false;
                if (chosen == TOOL_MACRO_ONE || chosen == TOOL_MACRO_TWO) {
                    activate_macro(chosen == TOOL_MACRO_ONE ? 0 : 1);
                } else {
                    macro_active = -1;
                    macro_wheel_locked = false;
                    overview_dirty = true;
                }
                force_full_redraw = true;
            } else if (cue_audition_active) {
                finish_cue_audition();
                if (!select_hold_fired && mode >= MODE_DECK) {
                    select_click_pending = true;
                    select_click_deadline = *rb->current_tick +
                                            RBPREP_SELECT_DOUBLE_TICKS;
                }
            } else if (!select_hold_fired) {
                if (mode >= MODE_DECK) {
                    select_click_pending = true;
                    select_click_deadline = *rb->current_tick +
                                            RBPREP_SELECT_DOUBLE_TICKS;
                } else {
                    short_select();
                }
            }
            select_hold_fired = false;
            break;
        case BUTTON_SELECT | BUTTON_REPEAT:
            if (pressed == BUTTON_SELECT && !select_hold_fired &&
                tool_menu_active &&
                (active_tool() == TOOL_MACRO_ONE ||
                 active_tool() == TOOL_MACRO_TWO)) {
                select_hold_fired = true;
                tool_menu_active = false;
                long_select();
            } else if (pressed == BUTTON_SELECT && !select_hold_fired &&
                       !tool_menu_active) {
                select_hold_fired = true;
                if (cue_audition_active)
                    latch_cue_audition();
                else
                    long_select();
            }
            break;
        case BUTTON_SELECT | BUTTON_LEFT:
            if (mode >= MODE_DECK && macro_active >= 0) {
                step_macro(-1);
                macro_chord_button = BUTTON_LEFT;
                suppress_left = true;
                select_hold_fired = true;
                pressed = BUTTON_NONE;
            }
            break;
        case BUTTON_SELECT | BUTTON_LEFT | BUTTON_REPEAT:
            /* One workflow step per physical chord press. */
            break;
        case BUTTON_SELECT | BUTTON_RIGHT:
            if (mode >= MODE_DECK && macro_active >= 0) {
                step_macro(1);
                macro_chord_button = BUTTON_RIGHT;
                suppress_right = true;
                select_hold_fired = true;
                pressed = BUTTON_NONE;
            }
            break;
        case BUTTON_SELECT | BUTTON_RIGHT | BUTTON_REPEAT:
            /* One workflow step per physical chord press. */
            break;
        case BUTTON_SELECT | BUTTON_MENU:
        case BUTTON_SELECT | BUTTON_MENU | BUTTON_REPEAT:
            if (mode >= MODE_DECK && macro_active >= 0) {
                swap_macro(-1);
                macro_chord_button = BUTTON_MENU;
                suppress_menu = true;
                menu_button_down = false;
                menu_hold_fired = false;
                select_hold_fired = true;
                pressed = BUTTON_NONE;
            }
            break;
        case BUTTON_SELECT | BUTTON_PLAY:
        case BUTTON_SELECT | BUTTON_PLAY | BUTTON_REPEAT:
            if (mode >= MODE_DECK && macro_active >= 0) {
                swap_macro(1);
                macro_chord_button = BUTTON_PLAY;
                suppress_play = true;
                select_hold_fired = true;
                pressed = BUTTON_NONE;
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
            if (click_sound)
                rb->system_sound_play(SOUND_KEYCLICK);
            if (seek_portal_active) {
                /* The double-SELECT portal borrows SEEK temporarily; never
                   route its wheel events through the remembered tool/page. */
                if (!seek_wheel_event_owned())
                    seek_by(scrub_step_ms(), false);
            } else if (tool_menu_active && mode >= MODE_DECK)
                browse_tool_menu(1);
            else if (mode == MODE_MACRO_VALUE)
                adjust_macro_value(1, false);
            else if (mode == MODE_LIBRARY)
                navigate_main_menu(1);
            else if (mode == MODE_PLAYLISTS &&
                     playlist_browser_count() > 0) {
                tree_selection = MIN(playlist_browser_count() - 1,
                                     tree_selection + 1);
                if (tree_selection >= tree_top + RBPREP_LIST_ROWS)
                    tree_top = tree_selection - RBPREP_LIST_ROWS + 1;
            } else if (mode == MODE_TRACKS && track_row_count > 0) {
                track_selection = MIN(track_row_count - 1,
                                      track_selection + 1);
                if (track_selection >= track_top + RBPREP_LIST_ROWS)
                    track_top = track_selection - RBPREP_LIST_ROWS + 1;
            } else if (mode == MODE_FILTER) {
                filter_selection = MIN(TRACK_SORT_COUNT + 1,
                                       filter_selection + 1);
            } else if (mode == MODE_PENDING &&
                       pending_snapshot_count + pending_playlist_count > 0) {
                pending_selection = MIN(pending_snapshot_count +
                                        pending_playlist_count - 1,
                                        pending_selection + 1);
                if (pending_selection >= pending_top + RBPREP_PENDING_ROWS)
                    pending_top = pending_selection - RBPREP_PENDING_ROWS + 1;
            }
            else if (mode == MODE_USB)
                usb_selection = MIN(capabilities.usb_audio ? 2 : 1,
                                    usb_selection + 1);
            else if (mode == MODE_SETTINGS)
                settings_selection = MIN(RBPREP_SETTINGS_LAST,
                                         settings_selection + 1);
            else if (mode == MODE_ACCENT)
                adjust_active_color(1);
            else if (mode == MODE_PLAYLIST_ACTIONS)
                playlist_action_selection =
                    MIN(7, playlist_action_selection + 1);
            else if (mode == MODE_MACRO_ACTIONS)
                macro_action_selection =
                    MIN(2, macro_action_selection + 1);
            else if (mode == MODE_MACRO_EDITOR)
                move_macro_editor_cursor(1);
            else if (mode == MODE_MACRO_PICKER)
                move_macro_picker(1);
            else if (mode == MODE_GENRES) {
                genre_selection = MIN(genre_count, genre_selection + 1);
                if (genre_selection >= genre_top + RBPREP_LIST_ROWS)
                    genre_top = genre_selection - RBPREP_LIST_ROWS + 1;
            } else if (mode != MODE_PENDING && mode != MODE_INDEX &&
                       !seek_wheel_event_owned())
                adjust_active_tool(1);
            break;
        case BUTTON_SCROLL_BACK:
        case BUTTON_SCROLL_BACK | BUTTON_REPEAT:
            if (click_sound)
                rb->system_sound_play(SOUND_KEYCLICK);
            if (seek_portal_active) {
                if (!seek_wheel_event_owned())
                    seek_by(-scrub_step_ms(), false);
            } else if (tool_menu_active && mode >= MODE_DECK)
                browse_tool_menu(-1);
            else if (mode == MODE_MACRO_VALUE)
                adjust_macro_value(-1, false);
            else if (mode == MODE_LIBRARY)
                navigate_main_menu(-1);
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
            else if (mode == MODE_ACCENT)
                adjust_active_color(-1);
            else if (mode == MODE_PLAYLIST_ACTIONS)
                playlist_action_selection =
                    MAX(0, playlist_action_selection - 1);
            else if (mode == MODE_MACRO_ACTIONS)
                macro_action_selection =
                    MAX(0, macro_action_selection - 1);
            else if (mode == MODE_MACRO_EDITOR)
                move_macro_editor_cursor(-1);
            else if (mode == MODE_MACRO_PICKER)
                move_macro_picker(-1);
            else if (mode == MODE_GENRES) {
                genre_selection = MAX(0, genre_selection - 1);
                if (genre_selection < genre_top)
                    genre_top = genre_selection;
            } else if (mode != MODE_PENDING && mode != MODE_INDEX &&
                       !seek_wheel_event_owned())
                adjust_active_tool(-1);
            break;
        case BUTTON_LEFT:
            if (suppress_left)
                break;
            if (tool_menu_active && mode >= MODE_DECK) {
                browse_tool_menu(-1);
                pressed = BUTTON_NONE;
            } else if (mode == MODE_MACRO_VALUE) {
                adjust_macro_value(-1, true);
                pressed = BUTTON_NONE;
            } else if (mode == MODE_PLAYLIST_ACTIONS) {
                playlist_action_selection =
                    MAX(0, playlist_action_selection - 1);
                pressed = BUTTON_NONE;
            } else if (mode == MODE_MACRO_ACTIONS) {
                macro_action_selection =
                    MAX(0, macro_action_selection - 1);
                pressed = BUTTON_NONE;
            } else if (mode == MODE_MACRO_EDITOR) {
                move_macro_editor_cursor(-1);
                pressed = BUTTON_NONE;
            } else if (mode == MODE_MACRO_PICKER) {
                move_macro_picker(-1);
                pressed = BUTTON_NONE;
            } else if (mode == MODE_ACCENT) {
                accent_component = MAX(0, accent_component - 1);
                pressed = BUTTON_NONE;
            } else if (mode >= MODE_DECK)
                pressed = BUTTON_LEFT;
            break;
        case BUTTON_LEFT | BUTTON_REPEAT:
            if (tool_menu_active && mode >= MODE_DECK)
                browse_tool_menu(-1);
            else if (mode == MODE_MACRO_VALUE)
                adjust_macro_value(-1, true);
            else if (mode == MODE_PLAYLIST_ACTIONS)
                playlist_action_selection =
                    MAX(0, playlist_action_selection - 1);
            else if (mode == MODE_MACRO_ACTIONS)
                macro_action_selection =
                    MAX(0, macro_action_selection - 1);
            else if (mode == MODE_MACRO_EDITOR)
                move_macro_editor_cursor(-1);
            else if (mode == MODE_MACRO_PICKER)
                move_macro_picker(-1);
            else if (mode == MODE_ACCENT)
                accent_component = MAX(0, accent_component - 1);
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
            } else if (mode == MODE_MACRO_VALUE) {
                adjust_macro_value(1, true);
                pressed = BUTTON_NONE;
            } else if (mode == MODE_PLAYLIST_ACTIONS) {
                playlist_action_selection =
                    MIN(7, playlist_action_selection + 1);
                pressed = BUTTON_NONE;
            } else if (mode == MODE_MACRO_ACTIONS) {
                macro_action_selection =
                    MIN(2, macro_action_selection + 1);
                pressed = BUTTON_NONE;
            } else if (mode == MODE_MACRO_EDITOR) {
                move_macro_editor_cursor(1);
                pressed = BUTTON_NONE;
            } else if (mode == MODE_MACRO_PICKER) {
                move_macro_picker(1);
                pressed = BUTTON_NONE;
            } else if (mode == MODE_ACCENT) {
                accent_component = MIN(2, accent_component + 1);
                pressed = BUTTON_NONE;
            } else if (mode >= MODE_DECK)
                pressed = BUTTON_RIGHT;
            break;
        case BUTTON_RIGHT | BUTTON_REPEAT:
            if (tool_menu_active && mode >= MODE_DECK)
                browse_tool_menu(1);
            else if (mode == MODE_MACRO_VALUE)
                adjust_macro_value(1, true);
            else if (mode == MODE_PLAYLIST_ACTIONS)
                playlist_action_selection =
                    MIN(7, playlist_action_selection + 1);
            else if (mode == MODE_MACRO_ACTIONS)
                macro_action_selection =
                    MIN(2, macro_action_selection + 1);
            else if (mode == MODE_MACRO_EDITOR)
                move_macro_editor_cursor(1);
            else if (mode == MODE_MACRO_PICKER)
                move_macro_picker(1);
            else if (mode == MODE_ACCENT)
                accent_component = MIN(2, accent_component + 1);
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
            if (handle_usb_system_event(button))
                force_full_redraw = true;
            break;
        }
    }
}
