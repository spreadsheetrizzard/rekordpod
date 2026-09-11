/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2002 Björn Stenberg
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "action.h"
#include "file.h"
#include "font.h"
#ifdef HAVE_REMOTE_LCD
#include "lcd-remote.h"
#endif
#include "lang.h"
#include "usb.h"
#if defined(HAVE_USBSTACK)
#include "usb_core.h"
#ifdef USB_ENABLE_HID
#include "usb_keymaps.h"
#endif
#endif
#include "settings.h"
#include "led.h"
#include "appevents.h"
#include "usb_screen.h"
#include "skin_engine/skin_engine.h"
#include "playlist.h"
#include "misc.h"
#include "icons.h"
#include "sound.h"
#include "powermgmt.h"
#include "timefuncs.h"
#include "pcm_mixer.h"

#include "bitmaps/usblogo.h"

#ifdef HAVE_REMOTE_LCD
#include "bitmaps/remote_usblogo.h"
#endif

#if (CONFIG_STORAGE & STORAGE_MMC)
#include "ata_mmc.h"
#endif

struct usb_screen_vps_t
{
    struct viewport parent;
    struct viewport logo;
#ifndef USB_ENABLE_HID
};
#else
    struct viewport title;
};

int usb_keypad_mode;
static void draw_usb_keypad_mode(struct viewport *title)
{
    struct screen *screen = &screens[SCREEN_MAIN];
    struct viewport *last_vp = screen->set_viewport(title);
    screen->clear_viewport();
    title->flags |= VP_FLAG_ALIGN_CENTER;
    if (title->width > 1)
        screen->puts_scroll(0, 0, str(keypad_mode_name_get(usb_keypad_mode)));
    screen->set_viewport(last_vp);
}
#endif /* USB_ENABLE_HID */

#if defined(IPOD_6G) && defined(HAVE_LCD_COLOR)
#define REKORDPOD_USB_STATUS "/.rockbox/rbprep/usb-status.rbs"
#define REKORDPOD_DAC_SAMPLES 512
#define REKORDPOD_DAC_BANDS 20

struct rekordpod_usb_snapshot {
    uint32_t tracks;
    uint32_t nodes;
    uint32_t members;
    uint32_t recent;
    uint32_t plays;
    uint32_t burned;
    uint32_t pending;
};

static struct rekordpod_usb_snapshot rekordpod_usb_stats;
static int rekordpod_accent = LCD_RGBPACK(70, 235, 125);
static int rekordpod_body = LCD_RGBPACK(220, 224, 221);
static int rekordpod_wheel = LCD_RGBPACK(25, 28, 26);
static void draw_rekordpod_dac_gain(struct screen *screen);
static void draw_rekordpod_dac_meters(struct screen *screen);
static void draw_rekordpod_status(struct screen *screen);
static int rekordpod_dac_screen_volume;
static int16_t rekordpod_dac_samples[REKORDPOD_DAC_SAMPLES];
static volatile unsigned int rekordpod_dac_sample_pos;
static volatile unsigned int rekordpod_dac_generation;
static unsigned int rekordpod_dac_drawn_generation;
static unsigned long rekordpod_dac_sample_rate = 44100;
static unsigned char rekordpod_dac_levels[REKORDPOD_DAC_BANDS];
static long rekordpod_dac_update_deadline;

static uint32_t rekordpod_read_u32(const unsigned char *data)
{
    return data[0] | (data[1] << 8) | (data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void rekordpod_load_usb_snapshot(void)
{
    unsigned char data[48];
    int fd;

    memset(&rekordpod_usb_stats, 0, sizeof(rekordpod_usb_stats));
    fd = open(REKORDPOD_USB_STATUS, O_RDONLY);
    if (fd < 0)
        return;
    if (read(fd, data, sizeof(data)) == (ssize_t)sizeof(data) &&
        !memcmp(data, "RUS1", 4) && rekordpod_read_u32(data + 4) == 1) {
        rekordpod_usb_stats.tracks = rekordpod_read_u32(data + 8);
        rekordpod_usb_stats.nodes = rekordpod_read_u32(data + 12);
        rekordpod_usb_stats.members = rekordpod_read_u32(data + 16);
        rekordpod_usb_stats.recent = rekordpod_read_u32(data + 20);
        rekordpod_usb_stats.plays = rekordpod_read_u32(data + 24);
        rekordpod_usb_stats.burned = rekordpod_read_u32(data + 28);
        rekordpod_usb_stats.pending = rekordpod_read_u32(data + 32);
        rekordpod_accent = rekordpod_read_u32(data + 36);
        rekordpod_body = rekordpod_read_u32(data + 40);
        rekordpod_wheel = rekordpod_read_u32(data + 44);
    }
    close(fd);
}

#ifdef USB_ENABLE_AUDIO
static void rekordpod_dac_sample_rate_changed(uint32_t samplerate)
{
    rekordpod_dac_sample_rate = samplerate ? samplerate : 44100;
}

static void rekordpod_dac_buffer_callback(const void *start, size_t size)
{
    const int16_t *samples = start;
    unsigned int frames = size / (sizeof(int16_t) * 2);
    unsigned int position = rekordpod_dac_sample_pos;
    unsigned int frame;

    for (frame = 0; frame < frames; frame++) {
        rekordpod_dac_samples[position] =
            ((int)samples[frame * 2] + (int)samples[frame * 2 + 1]) >> 1;
        position = (position + 1) & (REKORDPOD_DAC_SAMPLES - 1);
    }
    rekordpod_dac_sample_pos = position;
    rekordpod_dac_generation++;
}

static const struct mixer_buffer_cbs rekordpod_dac_buffer_cbs = {
    .next_buffer = rekordpod_dac_buffer_callback,
    .sampr_changed = rekordpod_dac_sample_rate_changed,
};

static void rekordpod_dac_capture_start(void)
{
    memset(rekordpod_dac_samples, 0, sizeof(rekordpod_dac_samples));
    memset(rekordpod_dac_levels, 0, sizeof(rekordpod_dac_levels));
    rekordpod_dac_sample_pos = 0;
    rekordpod_dac_generation = rekordpod_dac_drawn_generation = 0;
    rekordpod_dac_sample_rate = mixer_get_frequency();
    rekordpod_dac_update_deadline = current_tick;
    mixer_channel_set_buffer_hook(PCM_MIXER_CHAN_USBAUDIO,
                                  &rekordpod_dac_buffer_cbs);
}

static void rekordpod_dac_capture_stop(void)
{
    mixer_channel_set_buffer_hook(PCM_MIXER_CHAN_USBAUDIO, NULL);
}

static void rekordpod_update_dac_levels(void)
{
    static const unsigned short bins[REKORDPOD_DAC_BANDS] = {
        1, 2, 3, 4, 6, 8, 11, 15, 20, 27,
        36, 48, 64, 84, 108, 136, 168, 202, 238, 250
    };
    unsigned int write_pos = rekordpod_dac_sample_pos;
    unsigned int generation = rekordpod_dac_generation;
    int band;

    if (generation == rekordpod_dac_drawn_generation) {
        for (band = 0; band < REKORDPOD_DAC_BANDS; band++)
            rekordpod_dac_levels[band] =
                rekordpod_dac_levels[band] * 7 / 8;
        return;
    }
    rekordpod_dac_drawn_generation = generation;
    for (band = 0; band < REKORDPOD_DAC_BANDS; band++) {
        long real = 0;
        long imaginary = 0;
        unsigned int sample;

        for (sample = 0; sample < REKORDPOD_DAC_SAMPLES; sample++) {
            int value = rekordpod_dac_samples[
                (write_pos + sample) & (REKORDPOD_DAC_SAMPLES - 1)] >> 8;
            int window = sample <= REKORDPOD_DAC_SAMPLES / 2
                       ? sample : REKORDPOD_DAC_SAMPLES - 1 - sample;
            unsigned int phase = (sample * bins[band]) &
                                 (REKORDPOD_DAC_SAMPLES - 1);

            value = value * window / (REKORDPOD_DAC_SAMPLES / 2);
            real += (phase < REKORDPOD_DAC_SAMPLES / 4 ||
                     phase >= REKORDPOD_DAC_SAMPLES * 3 / 4)
                  ? value : -value;
            imaginary += phase < REKORDPOD_DAC_SAMPLES / 2
                       ? value : -value;
        }
        {
            int target = MIN(72, (ABS(real) + ABS(imaginary)) / 96);
            int old = rekordpod_dac_levels[band];

            rekordpod_dac_levels[band] = target > old
                ? (old + target * 3) / 4 : (old * 7 + target) / 8;
        }
    }
}
#endif

static bool handle_rekordpod_dac_button(int button)
{
    int volume;
    int direction = 0;

    if (global_settings.usb_audio != 1)
        return false;

    switch (button) {
    case BUTTON_SCROLL_FWD:
    case BUTTON_SCROLL_FWD | BUTTON_REPEAT:
        direction = 1;
        break;
    case BUTTON_SCROLL_BACK:
    case BUTTON_SCROLL_BACK | BUTTON_REPEAT:
        direction = -1;
        break;
    default:
        break;
    }

    if (direction != 0) {
        volume = sound_current(SOUND_VOLUME) +
                 direction * sound_steps(SOUND_VOLUME);
        volume = MAX(sound_min(SOUND_VOLUME),
                     MIN(sound_max(SOUND_VOLUME), volume));
        if (volume != sound_current(SOUND_VOLUME)) {
            /* Keep wheel gain live and cheap: set the codec directly instead
               of writing Rockbox status to disk on every clickwheel detent. */
            global_status.volume = volume;
            sound_set(SOUND_VOLUME, volume);
        }
    }

    if (sound_current(SOUND_VOLUME) != rekordpod_dac_screen_volume) {
        draw_rekordpod_dac_gain(&screens[SCREEN_MAIN]);
        screens[SCREEN_MAIN].update_rect(0,
            screens[SCREEN_MAIN].getheight() - 29,
            screens[SCREEN_MAIN].getwidth(), 29);
    }

    return direction != 0;
}
#endif

static void handle_usb_events(struct viewport *title)
{
#if (CONFIG_STORAGE & STORAGE_MMC) && !defined(SIMULATOR)
    int next_update = 0;
#endif /* STORAGE_MMC */
    int button;
    long reconnect_deadline = 0;

    /* Stay in one USB-screen session across the short release/request cycle
       produced by some hosts while selecting and resetting a configuration.
       Returning to the caller on that transient disconnect makes Rekordpod
       reopen its menu, reset to power-only, and miss the host's second mass-
       storage request. A disconnect that is not followed by a reconnect in
       this grace window is a real eject and returns normally. */
    while(1)
    {
#ifndef USB_ENABLE_HID
        (void)title;
#else
        if (global_settings.usb_hid)
        {
            button = get_hid_usb_action();
            if (button == ACTION_USB_HID_MODE_SWITCH_NEXT ||
                button == ACTION_USB_HID_MODE_SWITCH_PREV)
                draw_usb_keypad_mode(title);
        }
        else
#endif
        {
            /* hid emits the event in get_action */
            send_event(GUI_EVENT_ACTIONUPDATE, NULL);
#if defined(IPOD_6G) && defined(HAVE_LCD_COLOR)
            button = button_get_w_tmo(global_settings.usb_audio == 1
                                      ? MAX(1, HZ/20) : HZ/2);
#else
            button = button_get_w_tmo(HZ/2);
#endif
        }
        if (reconnect_deadline != 0 &&
            (!usb_inserted() ||
             !TIME_BEFORE(current_tick, reconnect_deadline)))
            return;
        if (button == SYS_USB_DISCONNECTED) {
            if (!usb_inserted())
                return;
            reconnect_deadline = current_tick + HZ * 2;
            continue;
        }
        if (button == SYS_USB_CONNECTED) {
            /* This is a reconfiguration of the already-open session. All
               clients still need the new sequence acknowledgement before the
               storage driver may re-enter exclusive mode. */
            usb_acknowledge(SYS_USB_CONNECTED_ACK, button_get_data());
            reconnect_deadline = 0;
            continue;
        }
        if (button == SYS_CHARGER_DISCONNECTED)
            reset_runtime();

#if defined(IPOD_6G) && defined(HAVE_LCD_COLOR)
        handle_rekordpod_dac_button(button);
#ifdef USB_ENABLE_AUDIO
        if (global_settings.usb_audio == 1 &&
            !button_hold() &&
            !TIME_BEFORE(current_tick, rekordpod_dac_update_deadline)) {
            rekordpod_dac_update_deadline = current_tick + MAX(1, HZ / 12);
            rekordpod_update_dac_levels();
            draw_rekordpod_dac_meters(&screens[SCREEN_MAIN]);
            draw_rekordpod_status(&screens[SCREEN_MAIN]);
            screens[SCREEN_MAIN].update_rect(18, 38, 284, 132);
        }
#endif
#endif

/* USB-MMC bridge can report activity */
#if (CONFIG_STORAGE & STORAGE_MMC) && !defined(SIMULATOR)
        if(TIME_AFTER(current_tick,next_update))
        {
            if(usb_inserted()) {
                led(mmc_usb_active(HZ));
            }
            next_update=current_tick+HZ/2;
        }
#endif /* STORAGE_MMC */
    }
}

#if defined(IPOD_6G) && defined(HAVE_LCD_COLOR)
static void draw_rekordpod_status(struct screen *screen)
{
    char clock[12];
    struct tm *now = get_time();
    int width = screen->getwidth();
    int level = MAX(0, MIN(100, battery_level()));
    int filled = width * level / 100;
    bool low = level <= 10;
    int text_width;

    snprintf(clock, sizeof(clock), "%02d:%02d", now->tm_hour, now->tm_min);
    screen->set_drawmode(DRMODE_SOLID);
    screen->set_foreground(LCD_BLACK);
    screen->fillrect(0, 0, width, 8);
    screen->set_foreground(low ? LCD_RGBPACK(225, 30, 30) : LCD_WHITE);
    if (filled > 0)
        screen->fillrect(0, 0, filled, 7);
    screen->set_foreground(LCD_RGBPACK(70, 70, 70));
    screen->hline(0, width - 1, 7);
    text_width = screen->getstringsize((const unsigned char *)clock,
                                       NULL, NULL);
    if (low) {
        screen->set_foreground(LCD_WHITE);
        screen->putsxy((width - text_width) / 2, -1,
                       (const unsigned char *)clock);
    } else {
        screen->set_drawmode(DRMODE_COMPLEMENT);
        screen->putsxy((width - text_width) / 2, -1,
                       (const unsigned char *)clock);
        screen->set_drawmode(DRMODE_SOLID);
    }
}

static void rekordpod_usb_circle(struct screen *screen, int cx, int cy,
                                 int radius)
{
    int x = radius;
    int y = 0;
    int error = 1 - radius;

    while (x >= y) {
        screen->drawpixel(cx + x, cy + y);
        screen->drawpixel(cx + y, cy + x);
        screen->drawpixel(cx - y, cy + x);
        screen->drawpixel(cx - x, cy + y);
        screen->drawpixel(cx - x, cy - y);
        screen->drawpixel(cx - y, cy - x);
        screen->drawpixel(cx + y, cy - x);
        screen->drawpixel(cx + x, cy - y);
        y++;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            x--;
            error += 2 * (y - x) + 1;
        }
    }
}

static void draw_rekordpod_usb_screen(struct screen *screen)
{
    const int width = screen->getwidth();
    const int height = screen->getheight();
    const int cx = 54;
    const int cy = 105;
    static const char *labels[] = {
        "TRACKS", "LIST NODES", "MEMBERS", "NEW / 30D",
        "TOTAL PLAYS", "BURNED NOW", "PENDING"
    };
    uint32_t values[] = {
        rekordpod_usb_stats.tracks, rekordpod_usb_stats.nodes,
        rekordpod_usb_stats.members, rekordpod_usb_stats.recent,
        rekordpod_usb_stats.plays, rekordpod_usb_stats.burned,
        rekordpod_usb_stats.pending
    };
    char value[24];
    int ring;
    int i;

    screen->set_viewport(NULL);
    screen->setfont(FONT_SYSFIXED);
    screen->set_drawmode(DRMODE_SOLID);
    screen->set_background(LCD_BLACK);
    screen->set_foreground(LCD_BLACK);
    screen->fillrect(0, 0, width, height);

    screen->set_foreground(LCD_RGBPACK(18, 18, 18));
    screen->fillrect(0, 0, width, 25);
    screen->set_foreground(rekordpod_accent);
    screen->hline(0, width - 1, 25);
    screen->set_foreground(LCD_WHITE);
    screen->putsxy(9, 7, (const unsigned char *)"REKORDPOD // DATA LINK");

    /* Record/platter emblem keeps the transfer screen visually continuous
       with Prep Deck without loading a backdrop or external bitmap. */
    screen->set_foreground(rekordpod_wheel);
    for (ring = 39; ring > 11; ring -= 4)
        rekordpod_usb_circle(screen, cx, cy, ring);
    screen->set_foreground(rekordpod_body);
    rekordpod_usb_circle(screen, cx, cy, 42);
    rekordpod_usb_circle(screen, cx, cy, 38);
    for (i = -10; i <= 10; i++) {
        int half = 10 - ABS(i) / 3;
        screen->hline(cx - half, cx + half, cy + i);
    }
    screen->set_foreground(rekordpod_wheel);
    screen->fillrect(cx - 3, cy - 3, 6, 6);

    /* Small native USB trident below the record keeps the mode obvious
       without competing with the useful index-status readout. */
    screen->set_foreground(LCD_RGBPACK(175, 175, 175));
    screen->drawline(36, 166, 72, 166);
    screen->drawline(54, 166, 54, 147);
    screen->drawline(54, 147, 50, 152);
    screen->drawline(54, 147, 58, 152);
    screen->drawline(54, 166, 66, 154);
    screen->fillrect(64, 152, 5, 5);
    screen->set_foreground(LCD_WHITE);
    screen->fillrect(70, 163, 4, 7);
    screen->putsxy(21, 181, (const unsigned char *)"512B LINK LIVE");

    screen->set_foreground(LCD_RGBPACK(22, 22, 22));
    screen->fillrect(103, 37, 207, 164);
    screen->set_foreground(LCD_RGBPACK(100, 100, 100));
    screen->drawrect(103, 37, 207, 164);
    for (i = 0; i < 7; i++) {
        int y = 44 + i * 22;
        screen->set_foreground(i == 6 && values[i] != 0
                               ? LCD_RGBPACK(255, 155, 45)
                               : LCD_RGBPACK(155, 155, 155));
        screen->putsxy(111, y, (const unsigned char *)labels[i]);
        snprintf(value, sizeof(value), "%lu", (unsigned long)values[i]);
        screen->set_foreground(i == 6 && values[i] != 0
                               ? LCD_WHITE : rekordpod_accent);
        screen->putsxy(302 - screen->getstringsize(
                       (const unsigned char *)value, NULL, NULL), y,
                       (const unsigned char *)value);
        if (i < 6) {
            screen->set_foreground(LCD_RGBPACK(42, 42, 42));
            screen->hline(110, 301, y + 15);
        }
    }

    screen->set_foreground(LCD_RGBPACK(24, 24, 24));
    screen->fillrect(0, height - 28, width, 28);
    screen->set_foreground(rekordpod_accent);
    screen->hline(0, width - 1, height - 29);
    screen->set_foreground(LCD_WHITE);
    screen->putsxy(9, height - 20,
                   (const unsigned char *)"INDEX ONLINE // EJECT WHEN DONE");
    draw_rekordpod_status(screen);
    screen->update();
}

static void draw_rekordpod_dac_screen(struct screen *screen)
{
    const int width = screen->getwidth();
    const int height = screen->getheight();
    char rate[40];
    int row;

    screen->set_viewport(NULL);
    screen->setfont(FONT_SYSFIXED);
    screen->set_drawmode(DRMODE_SOLID);
    screen->set_background(LCD_BLACK);
    screen->set_foreground(LCD_BLACK);
    screen->fillrect(0, 0, width, height);

    screen->set_foreground(LCD_RGBPACK(18, 18, 18));
    screen->fillrect(0, 8, width, 25);
    screen->set_foreground(LCD_RGBPACK(108, 108, 108));
    screen->hline(0, width - 1, 33);
    screen->set_foreground(LCD_WHITE);
    screen->putsxy(9, 15, (const unsigned char *)"REKORDPOD // USB DAC");

    /* Ghosted DJ-menu blades stay behind the analyzer so DAC mode feels
       like a live overlay rather than a separate Rockbox screen. */
    for (row = 0; row < 5; row++) {
        screen->set_foreground(row == 1 ? LCD_RGBPACK(22, 42, 29)
                                        : LCD_RGBPACK(13, 17, 14));
        screen->fillrect(6, 43 + row * 29, width - 12, 23);
        screen->set_foreground(row == 1 ? rekordpod_accent
                                        : LCD_RGBPACK(38, 45, 40));
        screen->fillrect(6, 43 + row * 29, 3, 23);
    }

    screen->set_foreground(LCD_RGBPACK(8, 8, 8));
    screen->fillrect(18, 40, 284, 132);
    screen->set_foreground(LCD_RGBPACK(118, 118, 118));
    screen->drawrect(18, 40, 284, 132);
    screen->set_foreground(LCD_WHITE);
    screen->putsxy(26, 47, (const unsigned char *)"20-BAND LIVE EQ");
    snprintf(rate, sizeof(rate), "%lu.%lu KHZ / STEREO",
             rekordpod_dac_sample_rate / 1000,
             (rekordpod_dac_sample_rate % 1000) / 100);
    screen->set_foreground(LCD_RGBPACK(165, 165, 165));
    screen->putsxy(176, 47, (const unsigned char *)rate);

    draw_rekordpod_dac_meters(screen);
    draw_rekordpod_dac_gain(screen);
    draw_rekordpod_status(screen);
    screen->update();
}

static void draw_rekordpod_dac_meters(struct screen *screen)
{
    static const char *markers[] = { "60", "250", "1K", "4K", "16K" };
    static struct pcm_peaks peaks;
    int left_height = 0;
    int right_height = 0;
    int band;
    int segment;

#ifdef USB_ENABLE_AUDIO
    mixer_channel_calculate_peaks(PCM_MIXER_CHAN_USBAUDIO, &peaks);
    left_height = MIN(72, (uint64_t)peaks.left * 72 / 32767);
    right_height = MIN(72, (uint64_t)peaks.right * 72 / 32767);
#endif
    screen->set_foreground(LCD_RGBPACK(8, 8, 8));
    screen->fillrect(24, 65, 272, 100);
    screen->set_foreground(LCD_RGBPACK(45, 45, 45));
    screen->hline(27, 287, 150);
    screen->hline(27, 287, 114);

    for (band = 0; band < REKORDPOD_DAC_BANDS; band++) {
        int x = 28 + band * 10;
        int bar_height = rekordpod_dac_levels[band];
        int y = 150 - bar_height;

        screen->set_foreground(band < 17 ? rekordpod_accent : LCD_WHITE);
        if (bar_height > 0)
            screen->fillrect(x, y, 6, bar_height);
        screen->set_foreground(LCD_BLACK);
        for (segment = 6; segment < bar_height; segment += 6)
            screen->hline(x, x + 5, 150 - segment);
    }

    for (band = 0; band < 5; band++) {
        screen->set_foreground(LCD_RGBPACK(115, 115, 115));
        screen->putsxy(27 + band * 40, 154,
                       (const unsigned char *)markers[band]);
    }

    screen->set_foreground(LCD_RGBPACK(42, 42, 42));
    screen->drawrect(238, 72, 19, 79);
    screen->drawrect(266, 72, 19, 79);
    for (segment = 0; segment < 14; segment++) {
        int y = 146 - segment * 5;
        int threshold = (segment + 1) * 5;

        screen->set_foreground(threshold <= left_height
                               ? (segment >= 12 ? LCD_WHITE
                                                : rekordpod_accent)
                               : LCD_RGBPACK(24, 30, 26));
        screen->fillrect(243, y, 9, 3);
        screen->set_foreground(threshold <= right_height
                               ? (segment >= 12 ? LCD_WHITE
                                                : rekordpod_accent)
                               : LCD_RGBPACK(24, 30, 26));
        screen->fillrect(271, y, 9, 3);
    }
    screen->set_foreground(LCD_WHITE);
    screen->putsxy(244, 154, (const unsigned char *)"L");
    screen->putsxy(272, 154, (const unsigned char *)"R");
}

static void draw_rekordpod_dac_gain(struct screen *screen)
{
    char label[48];
    int width = screen->getwidth();
    int height = screen->getheight();

    rekordpod_dac_screen_volume = sound_current(SOUND_VOLUME);
    snprintf(label, sizeof(label), "GAIN %+d dB // WHEEL",
             rekordpod_dac_screen_volume);
    screen->set_viewport(NULL);
    screen->setfont(FONT_SYSFIXED);
    screen->set_drawmode(DRMODE_SOLID);
    screen->set_foreground(LCD_RGBPACK(24, 24, 24));
    screen->fillrect(0, height - 28, width, 28);
    screen->set_foreground(LCD_RGBPACK(128, 128, 128));
    screen->hline(0, width - 1, height - 29);
    screen->set_foreground(LCD_WHITE);
    screen->putsxy(9, height - 20, (const unsigned char *)label);
}
#endif

static void usb_screen_fix_viewports(struct screen *screen,
        struct usb_screen_vps_t *usb_screen_vps)
{
    int logo_width, logo_height;
    struct viewport *parent = &usb_screen_vps->parent;
    struct viewport *logo = &usb_screen_vps->logo;

#ifdef HAVE_REMOTE_LCD
    if (screen->screen_type == SCREEN_REMOTE)
    {
        logo_width = BMPWIDTH_remote_usblogo;
        logo_height = BMPHEIGHT_remote_usblogo;
    }
    else
#endif
    {
        logo_width = BMPWIDTH_usblogo;
        logo_height = BMPHEIGHT_usblogo;
    }

    viewportmanager_theme_enable(screen->screen_type, true, parent);

    if (logo_width  > parent->width)
        logo_width  = parent->width;
    if (logo_height > parent->height)
        logo_height = parent->height;

    *logo = *parent;
    logo->x = parent->x + parent->width - logo_width;
#ifdef HAVE_LCD_SPLIT
    switch (statusbar_position(screen))
    {
         /* start beyond split */
         case STATUSBAR_OFF:
             logo->y = parent->y + LCD_SPLIT_POS;
             break;
         case STATUSBAR_TOP:
             logo->y = parent->y + LCD_SPLIT_POS - STATUSBAR_HEIGHT;
             break;
         /* start at the top for maximum space */
         default:
             logo->y = parent->y;
             break;
    }
#else
    logo->y = parent->y + (parent->height - logo_height) / 2;
#endif
    logo->width = logo_width;
    logo->height = logo_height;

#ifdef USB_ENABLE_HID
    if (global_settings.usb_hid)
    {
        struct viewport *title = &usb_screen_vps->title;
        int char_height = font_get(parent->font)->height;
        *title = *parent;
        title->y = logo->y + logo->height + char_height;
        title->height = char_height;
        /* try to fit logo and title to parent */
        if (parent->y + parent->height < title->y + title->height)
        {
            logo->y = parent->y;
            title->y = parent->y + logo->height;
        }

        int i =0, langid = LANG_USB_KEYPAD_MODE;
        while (langid >= 0) /* ensure the USB mode strings get cached */
        {
            font_getstringsize(str(langid), NULL, NULL, title->font);
            langid = keypad_mode_name_get(i++);
        }
    }
#endif
}

static void usb_screens_draw(struct usb_screen_vps_t *usb_screen_vps_ar)
{
    struct viewport *last_vp;
    static const struct bitmap* logos[NB_SCREENS] = {
        &bm_usblogo,
#ifdef HAVE_REMOTE_LCD
        &bm_remote_usblogo,
#endif
    };
    FOR_NB_SCREENS(i)
    {
        struct screen *screen = &screens[i];
        struct usb_screen_vps_t *usb_screen_vps = &usb_screen_vps_ar[i];
        struct viewport *parent = &usb_screen_vps->parent;
        struct viewport *logo = &usb_screen_vps->logo;

        last_vp = screen->set_viewport(parent);
        screen->clear_viewport();
        screen->backlight_on();
#if defined(IPOD_6G) && defined(HAVE_LCD_COLOR)
        if (i == SCREEN_MAIN) {
            if (global_settings.usb_audio == 1)
                draw_rekordpod_dac_screen(screen);
            else
                draw_rekordpod_usb_screen(screen);
            screen->set_viewport(last_vp);
            continue;
        }
#endif
        screen->set_viewport(logo);
        screen->bmp(logos[i], 0, 0);
        screen->set_viewport(last_vp);
    }
#ifdef USB_ENABLE_HID
    if (global_settings.usb_hid)
        draw_usb_keypad_mode(&usb_screen_vps_ar[SCREEN_MAIN].title);
#endif
}

void gui_usb_screen_run(bool early_usb, intptr_t seqnum)
{
#ifdef SIMULATOR /* the sim allows toggling USB fast enough to overflow viewportmanagers stack */
    static bool in_usb_screen = false;
    if (in_usb_screen)
        return;
    in_usb_screen = true;
#endif

    struct usb_screen_vps_t usb_screen_vps_ar[NB_SCREENS];
    struct viewport *title = NULL;
#if defined HAVE_TOUCHSCREEN
    enum touchscreen_mode old_mode = touchscreen_get_mode();

    /* TODO: Paint buttons on screens OR switch to point mode and use
     * touchscreen as a touchpad to move the host's mouse cursor */
    touchscreen_set_mode(TOUCHSCREEN_BUTTON);
#endif

    push_current_activity(ACTIVITY_USBSCREEN);

#ifdef USB_ENABLE_HID
    usb_keypad_mode = global_settings.usb_keypad_mode;
    title = &usb_screen_vps_ar[SCREEN_MAIN].title;
#else
    title = NULL;
#endif

    FOR_NB_SCREENS(i)
    {
        struct screen *screen = &screens[i];
        /* we might be coming from anywhere, and the originating screen
         * can't be practically expected to cleanup the UI because
         * we're invoked via default_event_handler(), therefore we make a
         * generic cleanup here */
        screen->set_viewport(NULL);
        screen->scroll_stop();
        usb_screen_fix_viewports(screen, &usb_screen_vps_ar[i]);
    }

#if defined(IPOD_6G) && defined(HAVE_LCD_COLOR)
    /* Read Rekordpod's handoff snapshot while the filesystem still belongs
       to Rockbox. The USB screen uses only this RAM copy after MSC is acked. */
    rekordpod_load_usb_snapshot();
#ifdef USB_ENABLE_AUDIO
    if (global_settings.usb_audio == 1)
        rekordpod_dac_capture_start();
#endif
#endif

#if 0 /* handled in usb_screen_fix_viewports() */
    /* update the UI before disabling fonts, this maximizes the propability
     * that font cache lookups succeed during USB */
    send_event(GUI_EVENT_ACTIONUPDATE, NULL);
#endif

    if(!early_usb)
    {
        /* The font system leaves the .fnt fd's open, so we need for force close them all */
        font_disable_all();
    }
    usb_acknowledge(SYS_USB_CONNECTED_ACK, seqnum);
    usb_screens_draw(usb_screen_vps_ar);
    handle_usb_events(title);

#if defined(IPOD_6G) && defined(HAVE_LCD_COLOR) && defined(USB_ENABLE_AUDIO)
    if (global_settings.usb_audio == 1)
        rekordpod_dac_capture_stop();
#endif

#if defined(USB_ENABLE_HID)
    if (global_settings.usb_hid)
        screens[SCREEN_MAIN].scroll_stop_viewport(title);
    if (global_settings.usb_keypad_mode != usb_keypad_mode)
    {
        global_settings.usb_keypad_mode = usb_keypad_mode;
        settings_save();
    }
#endif

#ifdef HAVE_TOUCHSCREEN
    touchscreen_set_mode(old_mode);
#endif

    if(!early_usb)
    {
        font_enable_all();
        /* Not pretty, reload all settings so fonts are loaded again correctly */
        settings_apply(true);
        /* Reload playlist */
        playlist_resume();
    }

    FOR_NB_SCREENS(i)
    {
        screens[i].backlight_on();
        viewportmanager_theme_undo(i, false);
    }

    pop_current_activity();
#ifdef SIMULATOR
    in_usb_screen = false;
#endif
}
