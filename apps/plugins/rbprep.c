#include "plugin.h"

#if CONFIG_KEYPAD != IPOD_4G_PAD
#error "RBPrep currently targets the iPod click wheel"
#endif

#define RBPREP_PDB "/PIONEER/rekordbox/export.pdb"
#define RBPREP_WAVEFORM "/.rockbox/rbprep/waveform.rgb"
#define RBPREP_POINTS 32768
#define RBPREP_BEATS 2048
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
static int waveform_hz = 150;
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

static const char *color_labels[] = {
    "SAMPLE", "OPENER", "BUILDER", "PIVOTER",
    "MAINTAINER", "PEAK", "RESET", "TOOL"
};

static const char *mode_names[] = {
    "LIBRARY", "PLAYBACK", "HOT CUES", "BEATGRID", "METADATA"
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
    center = (long long)playhead * waveform_hz / 1000;
    *first = zoom == 1 ? 0 : center - *span / 2;
}

static int time_to_x(int time_ms, int first, int span)
{
    long long sample;

    if (waveform_points <= 0 || track_length <= 0)
        return -1;
    sample = (long long)time_ms * waveform_hz / 1000;
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

    view_start = MAX(0, (long long)first * 1000 / waveform_hz);
    view_end = MIN(track_length,
                   (long long)(first + span) * 1000 / waveform_hz);
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

static void load_waveform(void)
{
    int fd;
    int count;
    int cue_count;
    int declared_beat_count = 0;
    int extra_size;
    unsigned char header[16];
    unsigned char extension[12];

    waveform_points = 0;
    fd = rb->open(RBPREP_WAVEFORM, O_RDONLY);
    if (fd < 0)
        return;

    if (rb->read(fd, header, sizeof(header)) == sizeof(header) &&
        !rb->memcmp(header, "RBW2", 4)) {
        count = header[4] | (header[5] << 8);
        cue_count = header[6];
        track_length = header[8] | (header[9] << 8) |
                       (header[10] << 16) | (header[11] << 24);
        grid_bpm_x100 = header[12] | (header[13] << 8);
        color_index = header[14] & 7;
        rating = MIN(5, header[15]);
        rb->memset(extension, 0, sizeof(extension));
        extra_size = MAX(0, MIN((int)sizeof(extension), header[7] - 16));
        if (extra_size > 0 &&
            rb->read(fd, extension, extra_size) != extra_size) {
            rb->close(fd);
            return;
        }
        if (extra_size >= 4) {
            grid_phase_ms = extension[0] | (extension[1] << 8) |
                            (extension[2] << 16) | (extension[3] << 24);
        }
        if (extra_size >= 8) {
            declared_beat_count = extension[4] | (extension[5] << 8);
            waveform_hz = extension[6] | (extension[7] << 8);
            waveform_hz = MAX(1, waveform_hz);
        }

        count = MIN(count, RBPREP_POINTS);
        if (rb->read(fd, waveform, count * 4) == count * 4) {
            waveform_points = count;
            for (count = 0; count < cue_count; count++) {
                unsigned char cue[8];
                int slot;
                if (rb->read(fd, cue, 8) != 8)
                    break;
                slot = cue[5];
                if (count < 16 && slot > 0 && slot <= 16) {
                    hotcues[slot - 1] = cue[0] | (cue[1] << 8) |
                                         (cue[2] << 16) | (cue[3] << 24);
                    hotcue_colors[slot - 1] = cue[4] & 7;
                }
            }
            beat_count = MIN(declared_beat_count, RBPREP_BEATS);
            for (count = 0; count < beat_count; count++) {
                unsigned char beat[8];
                if (rb->read(fd, beat, sizeof(beat)) != sizeof(beat)) {
                    beat_count = count;
                    break;
                }
                beat_times[count] = beat[0] | (beat[1] << 8) |
                                    (beat[2] << 16) | (beat[3] << 24);
                beat_numbers[count] = MAX(1, MIN(4, beat[4]));
            }
        }
    }
    rb->close(fd);
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
    text(3, 3, "RB", LCD_RGBPACK(70, 235, 125));
    text(mode == MODE_LIBRARY ? 23 : RBPREP_DECK_X, 3,
         mode_names[mode], LCD_WHITE);
    rb->snprintf(line, sizeof(line), "Q %s", quantize ? "ON" : "OFF");
    text(281, 3, line, quantize ? LCD_RGBPACK(70, 235, 125)
                                : LCD_LIGHTGRAY);

    if (mode == MODE_LIBRARY) {
        const char *items[] = {
            "LIBRARY", "PLAYLISTS", "PREP DECK", "PENDING EDITS",
            "INDEX STATUS"
        };
        int i;
        rb->snprintf(line, sizeof(line), "%s",
                     rb->file_exists(RBPREP_PDB)
                     ? "DEVICE LIBRARY ONLINE" : "EXPORT.PDB NOT FOUND");
        text(10, 32, line, rb->file_exists(RBPREP_PDB)
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
        if (selection == 2) {
            mode = MODE_DECK;
            force_full_redraw = true;
        }
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
    } else {
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
    struct mp3entry *id3;

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
    waveform_hz = 150;
    beat_count = 0;
    seek_state = SEEK_IDLE;
    suppress_menu = suppress_play = false;
    suppress_left = suppress_right = false;
    force_full_redraw = true;
    for (button = 0; button < 16; button++) {
        hotcues[button] = -1;
        hotcue_colors[button] = 3;
    }
    load_waveform();
    id3 = rb->audio_current_track();
    if (id3) {
        if (id3->length > 0)
            track_length = id3->length;
        playhead = clamp_playhead(id3->elapsed);
    }
    reset_play_clock(playhead, *rb->current_tick);

    /* Discard the release of SELECT used to launch the plugin. */
    rb->button_clear_queue();
    while (true) {
        service_audio_seek();
        update_play_clock();

        draw_screen();
        button = rb->button_get_w_tmo(HZ / 20);
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
            if (mode == MODE_LIBRARY)
                return PLUGIN_OK;
            mode = MODE_LIBRARY;
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
                mode != MODE_LIBRARY) {
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
                mode != MODE_LIBRARY) {
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
                mode != MODE_LIBRARY) {
                select_hold_fired = true;
                pressed = BUTTON_NONE;
                suppress_menu = true;
                change_zoom(false);
            }
            break;
        case BUTTON_SELECT | BUTTON_PLAY:
        case BUTTON_SELECT | BUTTON_PLAY | BUTTON_REPEAT:
            if (pressed == BUTTON_SELECT && !select_hold_fired &&
                mode != MODE_LIBRARY) {
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
            else
                seek_by(scrub_step_ms(), true);
            break;
        case BUTTON_SCROLL_BACK:
        case BUTTON_SCROLL_BACK | BUTTON_REPEAT:
            if (mode == MODE_LIBRARY)
                selection = MAX(0, selection - 1);
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
            else if (mode != MODE_LIBRARY)
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
            else if (mode != MODE_LIBRARY)
                seek_by(1000, true);
            break;
        case BUTTON_RIGHT | BUTTON_REL:
            suppress_right = false;
            break;
        default:
            if (rb->default_event_handler(button) == SYS_USB_CONNECTED) {
                stop_editor_audio();
                return PLUGIN_USB_CONNECTED;
            }
            break;
        }
    }
}
