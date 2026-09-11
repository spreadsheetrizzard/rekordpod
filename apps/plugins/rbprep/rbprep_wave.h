#ifndef RBPREP_WAVE_H
#define RBPREP_WAVE_H

#include "plugin.h"

#define RBPREP_WAVE_SAMPLE_BYTES 4
#if defined(IPOD_6G) || CONFIG_CPU == S5L8702
#define RBPREP_WAVE_MAX_PAGES 64
#else
#define RBPREP_WAVE_MAX_PAGES 16
#endif

struct rbprep_wave_sample {
    unsigned char amplitude;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

struct rbprep_wave_page {
    uint32_t first;
    uint32_t count;
    uint32_t stamp;
    unsigned char *data;
    bool valid;
};

struct rbprep_wave_reader {
    const struct plugin_api *api;
    int fd;
    off_t data_offset;
    uint32_t point_count;
    uint32_t stamp;
    size_t page_bytes;
    int page_count;
    struct rbprep_wave_page pages[RBPREP_WAVE_MAX_PAGES];
    uint32_t cache_hits;
    uint32_t cache_misses;
    bool fully_cached;
};

void rbprep_wave_init(struct rbprep_wave_reader *reader,
                      const struct plugin_api *api, void *workspace,
                      size_t workspace_bytes, size_t io_slice_bytes);
bool rbprep_wave_open(struct rbprep_wave_reader *reader, const char *path,
                      uint32_t point_count, off_t data_offset);
void rbprep_wave_close(struct rbprep_wave_reader *reader);
/* Preload the complete open waveform.  This is the only new API here that
   may perform storage I/O. */
bool rbprep_wave_cache_all(struct rbprep_wave_reader *reader);
/* These two cache queries never seek or read from storage. */
bool rbprep_wave_range_cached(const struct rbprep_wave_reader *reader,
                              uint32_t first, uint32_t count);
bool rbprep_wave_sample_cached(struct rbprep_wave_reader *reader,
                               uint32_t index,
                               struct rbprep_wave_sample *sample);
bool rbprep_wave_sample_at(struct rbprep_wave_reader *reader, uint32_t index,
                           struct rbprep_wave_sample *sample);
uint32_t rbprep_wave_resident_points(const struct rbprep_wave_reader *reader);
bool rbprep_wave_build_overview(struct rbprep_wave_reader *reader,
                                unsigned char (*overview)[4], int columns);

#endif
