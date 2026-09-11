#ifndef RBPREP_WAVE_INDEX_H
#define RBPREP_WAVE_INDEX_H

#include "plugin.h"
#include "rbprep_wave.h"

#define RBPREP_WAVE_INDEX_OVERVIEW 320

/* The Classic has enough RAM to keep a substantially finer peak pyramid.
   Its two-point base level gives 64x/128x zoom near-source detail without
   touching the RBW file from the animation path. Keep the smaller four-level
   index on the Video so the plugin remains inside its tighter memory budget. */
#if defined(IPOD_6G) || CONFIG_CPU == S5L8702
#define RBPREP_WAVE_INDEX_FINE 1
#define RBPREP_WAVE_INDEX_LEVELS 5
#else
#define RBPREP_WAVE_INDEX_FINE 0
#define RBPREP_WAVE_INDEX_LEVELS 4
#endif

enum rbprep_wave_index_stage {
    RBPREP_WAVE_INDEX_IDLE,
    RBPREP_WAVE_INDEX_SCAN,
    RBPREP_WAVE_INDEX_OPEN_WRITE,
    RBPREP_WAVE_INDEX_WRITE,
    RBPREP_WAVE_INDEX_OPEN_VERIFY,
    RBPREP_WAVE_INDEX_VERIFY,
    RBPREP_WAVE_INDEX_PUBLISH,
    RBPREP_WAVE_INDEX_FAILED
};

struct rbprep_wave_index {
    const struct plugin_api *api;
    unsigned char *payload;
    size_t capacity;
    size_t payload_size;
    size_t io_slice_bytes;
    uint32_t track_id;
    uint32_t source_points;
    uint32_t source_size;
    uint32_t source_fingerprint;
    uint32_t level_counts[RBPREP_WAVE_INDEX_LEVELS];
    size_t level_offsets[RBPREP_WAVE_INDEX_LEVELS];
    uint32_t scan_position;
    size_t transfer_position;
    uint32_t payload_checksum;
    uint32_t verify_checksum;
    int fd;
    enum rbprep_wave_index_stage stage;
    off_t source_data_offset;
    unsigned char (*overview)[4];
    char path[MAX_PATH];
    char temporary[MAX_PATH];
    bool valid;
};

void rbprep_wave_index_init(struct rbprep_wave_index *index,
                            const struct plugin_api *api, void *workspace,
                            size_t workspace_bytes, size_t io_slice_bytes);
void rbprep_wave_index_close(struct rbprep_wave_index *index);
uint32_t rbprep_wave_index_fingerprint(const unsigned char *header,
                                       size_t header_size,
                                       uint32_t source_size);
bool rbprep_wave_index_open(struct rbprep_wave_index *index,
                            const char *path, uint32_t track_id,
                            uint32_t source_points, uint32_t source_size,
                            uint32_t source_fingerprint,
                            unsigned char (*overview)[4]);
bool rbprep_wave_index_start(struct rbprep_wave_index *index,
                             const char *path, uint32_t track_id,
                             uint32_t source_points, uint32_t source_size,
                             uint32_t source_fingerprint,
                             const char *source_path,
                             off_t source_data_offset,
                             unsigned char (*overview)[4]);
bool rbprep_wave_index_service(struct rbprep_wave_index *index);
bool rbprep_wave_index_range_peak(const struct rbprep_wave_index *index,
                                  uint32_t begin, uint32_t end,
                                  struct rbprep_wave_sample *peak);

#endif
