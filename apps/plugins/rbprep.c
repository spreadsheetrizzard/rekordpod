#include "plugin.h"

#if CONFIG_KEYPAD != IPOD_4G_PAD
#error "RBPrep currently targets the iPod click wheel"
#endif

#define RBPREP_PDB "/PIONEER/rekordbox/export.pdb"
#define RBPREP_WAVEFORM "/.rockbox/rbprep/waveform.rgb"
#define RBPREP_POINTS 1200

enum rbprep_mode { MODE_LIBRARY, MODE_DECK, MODE_GRID, MODE_CUES, MODE_METADATA };

static enum rbprep_mode mode;
static int selection;
static int playhead;
static unsigned char waveform[RBPREP_POINTS][4];
static int waveform_points;
static int zoom = 1, grid_offset, beat_shift, cue_slot;
static int hotcues[16];
static int rating, color_index;
static const char *color_labels[] = { "SAMPLE", "OPENER", "BUILDER", "PIVOTER", "MAINTAINER", "PEAK", "RESET", "TOOL" };
static const char *mode_names[] = { "LIBRARY", "PREP DECK", "BEATGRID", "CUES", "METADATA" };

static void text(int x, int y, const char *s, int color)
{
    rb->lcd_set_foreground(color);
    rb->lcd_putsxy(x, y, s);
}

static void draw_waveform(void)
{
    int x, mid = 119;
    for (x = 0; x < LCD_WIDTH; x++) {
        int center = waveform_points ? (playhead / 1000) * waveform_points / 240 : 0;
        int span = waveform_points / zoom;
        int first = MAX(0, MIN(waveform_points - span, center - span / 2));
        int index = waveform_points ? first + x * span / LCD_WIDTH : 0;
        int h = waveform_points ? 4 + waveform[index][0] * 43 / 127 : 4;
        int color = waveform_points
            ? LCD_RGBPACK(waveform[index][1], waveform[index][2], waveform[index][3])
            : LCD_DARKGRAY;
        rb->lcd_set_foreground(color);
        rb->lcd_vline(x, mid - h, mid + h);
    }
    rb->lcd_set_foreground(LCD_WHITE);
    for (x = 12; x < LCD_WIDTH; x += 28)
        rb->lcd_vline(x, 76, 163);
    rb->lcd_set_foreground(LCD_RGBPACK(240, 55, 55));
    rb->lcd_vline(LCD_WIDTH / 2, 70, 169);
}

static void load_waveform(void)
{
    int fd, count;
    unsigned char header[8];
    waveform_points = 0;
    fd = rb->open(RBPREP_WAVEFORM, O_RDONLY);
    if (fd < 0) return;
    if (rb->read(fd, header, sizeof(header)) == sizeof(header) &&
        !rb->memcmp(header, "RBWF", 4)) {
        count = header[4] | (header[5] << 8);
        count = MIN(count, RBPREP_POINTS);
        if (rb->read(fd, waveform, count * 4) == count * 4)
            waveform_points = count;
    }
    rb->close(fd);
}

static void draw_screen(void)
{
    char line[80];
    struct mp3entry *id3 = rb->audio_current_track();
    if (id3 && (rb->audio_status() & AUDIO_STATUS_PLAY))
        playhead = id3->elapsed;
    rb->lcd_set_background(LCD_RGBPACK(8, 12, 9));
    rb->lcd_clear_display();
    rb->lcd_set_drawmode(DRMODE_SOLID);
    rb->lcd_set_foreground(LCD_RGBPACK(18, 27, 20));
    rb->lcd_fillrect(0, 0, LCD_WIDTH, 26);
    text(6, 6, "RBPREP", LCD_RGBPACK(70, 235, 125));
    text(75, 6, mode_names[mode], LCD_WHITE);
    rb->snprintf(line, sizeof(line), "%s", rb->file_exists(RBPREP_PDB) ? "DEVICE LIBRARY ONLINE" : "EXPORT.PDB NOT FOUND");
    text(6, 34, line, LCD_LIGHTGRAY);

    if (mode == MODE_LIBRARY) {
        const char *items[] = { "LIBRARY", "PLAYLISTS", "PREP DECK", "PENDING EDITS", "INDEX STATUS" };
        int i;
        for (i = 0; i < 5; i++) {
            if (i == selection) {
                rb->lcd_set_foreground(LCD_RGBPACK(25, 105, 175));
                rb->lcd_fillrect(0, 58 + i * 27, LCD_WIDTH, 25);
            }
            text(10, 64 + i * 27, items[i], LCD_WHITE);
        }
    } else {
        text(6, 51, waveform_points ? "REAL REKORDBOX RGB WAVEFORM" : "WAVEFORM CACHE NOT FOUND", LCD_LIGHTGRAY);
        draw_waveform();
        rb->snprintf(line, sizeof(line), "%02d:%02d.%03d  145.00  %s", playhead/60000, (playhead/1000)%60, playhead%1000, color_labels[color_index]);
        text(6, 178, line, LCD_WHITE);
        if (mode == MODE_CUES) {
            rb->snprintf(line, sizeof(line), "HOTCUE %02d  %s", cue_slot + 1, hotcues[cue_slot] >= 0 ? "SET" : "EMPTY");
            text(6, 196, line, LCD_RGBPACK(255, 190, 65));
        } else if (mode == MODE_GRID) {
            rb->snprintf(line, sizeof(line), "GRID %+dms  DOWNBEAT %d  ZOOM %dx", grid_offset, beat_shift + 1, zoom);
            text(6, 196, line, LCD_RGBPACK(80, 210, 255));
        } else if (mode == MODE_METADATA) {
            rb->snprintf(line, sizeof(line), "RATING %d/5  COLOR %s", rating, color_labels[color_index]);
            text(6, 196, line, LCD_RGBPACK(255, 200, 70));
        }
        text(6, 214, "SELECT ACTION   PLAY PLAY/PAUSE", LCD_LIGHTGRAY);
        text(6, 228, mode == MODE_GRID ? "< -1ms          +1ms >" : mode == MODE_CUES ? "< PREV SLOT  NEXT SLOT >" : "< SEEK          SEEK >", LCD_LIGHTGRAY);
    }
    rb->lcd_update();
}

static void toggle_playback(void)
{
    int status = rb->audio_status();
    if (status & AUDIO_STATUS_PAUSE) {
        rb->audio_resume();
    } else if (status & AUDIO_STATUS_PLAY) {
        rb->audio_pause();
    } else if (rb->global_status->resume_index != -1 &&
               rb->playlist_resume() != -1) {
        rb->playlist_resume_track(rb->global_status->resume_index,
                                  rb->global_status->resume_crc32,
                                  rb->global_status->resume_elapsed,
                                  rb->global_status->resume_offset);
    } else {
        rb->splash(HZ, "No Rockbox track loaded");
    }
}

enum plugin_status plugin_start(const void *parameter)
{
    int button;
    int pressed = BUTTON_NONE;
    (void)parameter;
    rb->lcd_setfont(FONT_SYSFIXED);
    load_waveform();
    mode = MODE_LIBRARY; selection = 0; playhead = 0;
    grid_offset = beat_shift = cue_slot = rating = color_index = 0;
    for (button = 0; button < 16; button++) hotcues[button] = -1;
    /* Discard the release of SELECT used to launch the plugin. */
    rb->button_clear_queue();
    while (true) {
        draw_screen();
        button = rb->button_get_w_tmo(HZ / 10);
        switch (button) {
        case BUTTON_MENU:
        case BUTTON_PLAY:
        case BUTTON_SELECT:
            pressed = button;
            break;
        case BUTTON_MENU | BUTTON_REL:
            if (pressed != BUTTON_MENU) break;
            pressed = BUTTON_NONE;
            if (mode == MODE_LIBRARY) return PLUGIN_OK;
            mode = MODE_LIBRARY;
            break;
        case BUTTON_PLAY | BUTTON_REL:
            if (pressed != BUTTON_PLAY) break;
            pressed = BUTTON_NONE;
            toggle_playback();
            break;
        case BUTTON_SELECT | BUTTON_REL:
            if (pressed != BUTTON_SELECT) break;
            pressed = BUTTON_NONE;
            if (mode == MODE_LIBRARY) mode = selection == 2 ? MODE_DECK : MODE_LIBRARY;
            else if (mode == MODE_DECK) mode = MODE_GRID;
            else if (mode == MODE_GRID) beat_shift = (beat_shift + 1) & 3;
            else if (mode == MODE_CUES) hotcues[cue_slot] = playhead;
            else rating = (rating + 1) % 6;
            break;
        case BUTTON_SELECT | BUTTON_REPEAT:
            pressed = BUTTON_NONE; /* do not also fire SELECT on release */
            if (mode != MODE_LIBRARY) {
                mode++;
                if (mode > MODE_METADATA) mode = MODE_DECK;
            }
            break;
        case BUTTON_SCROLL_FWD:
        case BUTTON_SCROLL_FWD | BUTTON_REPEAT:
            if (mode == MODE_LIBRARY) selection = MIN(4, selection + 1);
            else if (mode == MODE_GRID) zoom = MIN(16, zoom * 2);
            else if (mode == MODE_METADATA) color_index = (color_index + 1) & 7;
            else playhead += 10;
            break;
        case BUTTON_SCROLL_BACK:
        case BUTTON_SCROLL_BACK | BUTTON_REPEAT:
            if (mode == MODE_LIBRARY) selection = MAX(0, selection - 1);
            else if (mode == MODE_GRID) zoom = MAX(1, zoom / 2);
            else if (mode == MODE_METADATA) color_index = (color_index + 7) & 7;
            else playhead = MAX(0, playhead - 10);
            break;
        case BUTTON_LEFT:
        case BUTTON_LEFT | BUTTON_REPEAT:
            if (mode == MODE_GRID) grid_offset--;
            else if (mode == MODE_CUES) cue_slot = (cue_slot + 15) & 15;
            else { playhead = MAX(0, playhead - 1000); rb->audio_ff_rewind(playhead); }
            break;
        case BUTTON_RIGHT:
        case BUTTON_RIGHT | BUTTON_REPEAT:
            if (mode == MODE_GRID) grid_offset++;
            else if (mode == MODE_CUES) cue_slot = (cue_slot + 1) & 15;
            else { playhead += 1000; rb->audio_ff_rewind(playhead); }
            break;
        default:
            if (rb->default_event_handler(button) == SYS_USB_CONNECTED)
                return PLUGIN_USB_CONNECTED;
            break;
        }
    }
}
