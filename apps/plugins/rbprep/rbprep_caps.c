#include "rbprep_caps.h"

#define KIBIBYTE 1024u

static size_t bounded_fraction(size_t available, size_t requested,
                               size_t reserve)
{
    size_t usable = available > reserve ? available - reserve : 0;

    return MIN(requested, usable);
}

void rbprep_caps_detect(struct rbprep_caps *caps,
                        const struct plugin_api *api)
{
    size_t available = 0;

    caps->workspace = api->plugin_get_buffer(&available);
    caps->free_plugin_bytes = available;

#if defined(IPOD_6G) || CONFIG_CPU == S5L8702
    caps->device_class = RBPREP_DEVICE_CLASSIC;
    caps->waveform_cache_bytes = bounded_fraction(
        available, 192u * KIBIBYTE, 64u * KIBIBYTE);
    caps->beat_cache_bytes = 8u * KIBIBYTE;
    /* A five-level, two-point-base peak pyramid for the maximum 131072-point
       analysis occupies about 343 KiB. It is resident while drawing, which
       restores high-definition 64x/128x views without live storage reads. */
    caps->waveform_index_bytes = 384u * KIBIBYTE;
    /* Keep synchronous high-zoom cache fills below a single visible frame.
       The larger workspace remains an LRU window; only each I/O slice is
       reduced for flash/HDD adapters with poor long-read latency. */
    caps->io_slice_bytes = 8u * KIBIBYTE;
    caps->deck_fps = 30;
    caps->visualizer_fps = 20;
#if defined(USB_ENABLE_AUDIO)
    caps->usb_audio = true;
#else
    caps->usb_audio = false;
#endif
#else
    caps->device_class = RBPREP_DEVICE_VIDEO;
    caps->waveform_cache_bytes = bounded_fraction(
        available, 32u * KIBIBYTE, 48u * KIBIBYTE);
    caps->beat_cache_bytes = 4u * KIBIBYTE;
    caps->waveform_index_bytes = 48u * KIBIBYTE;
    caps->io_slice_bytes = 4u * KIBIBYTE;
    caps->deck_fps = 20;
    caps->visualizer_fps = 12;
    caps->usb_audio = false;
#endif

    caps->waveform_cache_bytes &= ~(size_t)3;
    if (caps->waveform_cache_bytes + caps->beat_cache_bytes > available)
        caps->beat_cache_bytes = available - caps->waveform_cache_bytes;
    caps->beat_cache_bytes &= ~(size_t)7;
    if (caps->waveform_cache_bytes + caps->beat_cache_bytes +
        caps->waveform_index_bytes > available)
        caps->waveform_index_bytes = available -
            caps->waveform_cache_bytes - caps->beat_cache_bytes;
    caps->waveform_index_bytes &= ~(size_t)3;
    caps->io_slice_bytes = MIN(caps->io_slice_bytes,
                               caps->waveform_cache_bytes);
}
