/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef RBPREP_CAPS_H
#define RBPREP_CAPS_H

#include "plugin.h"

enum rbprep_device_class {
    RBPREP_DEVICE_VIDEO,
    RBPREP_DEVICE_CLASSIC
};

struct rbprep_caps {
    enum rbprep_device_class device_class;
    void *workspace;
    size_t free_plugin_bytes;
    size_t waveform_cache_bytes;
    size_t beat_cache_bytes;
    size_t waveform_index_bytes;
    size_t io_slice_bytes;
    int deck_fps;
    int visualizer_fps;
    bool usb_audio;
};

void rbprep_caps_detect(struct rbprep_caps *caps,
                        const struct plugin_api *api);

#endif
