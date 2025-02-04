#include "libpsxav.h"
#include "wav.h"
#include <stdlib.h>

typedef struct {
    uint32_t sample_start;  // Offset (bytes) into sample data chunk. Can be written to SPU Sample Start Address
    uint32_t sample_rate;   // Sample rate (Hz) at MIDI key 60 (C5)
    uint32_t loop_start;    // Offset (bytes) relative to sample start to return to after the end of a sample
    uint16_t format;        // 0 = PSX SPU-ADPCM, 1 = Signed little-endian 16-bit PCM
    uint16_t reserved;      // (unused padding for now)
} SampleHeader;

typedef struct {
    uint16_t sample_index;  // Index into sample header array
    uint16_t delay;         // Delay stage length in milliseconds
    uint16_t attack;        // Attack stage length in milliseconds
    uint16_t hold;          // Hold stage length in milliseconds
    uint16_t decay;         // Decay stage length in milliseconds
    uint16_t sustain;       // Sustain volume where 0 = 0.0 and 65535 = 1.0
    uint16_t release;       // Release stage length in milliseconds
    uint16_t volume;        // Panning for this region, 0 = left, 127 = middle, 254 = right
    uint16_t panning;       // Panning for this region, 0 = left, 127 = middle, 254 = right
    uint8_t key_min;        // Minimum MIDI key for this instrument region
    uint8_t key_max;        // Maximum MIDI key for this instrument region         
} InstRegion;

typedef enum {
    FORMAT_PSX,
    FORMAT_PCM16,
} Format;

int main(int argc, char** argv) {
    // Validate input
    if (argc != 4) {
        printf("Usage: psx_soundfont_creator.exe <.csv> <.sbk> <format>\n");
        exit(1);
    }
    const char* path = argv[1];
    const char* out_path = argv[2];
    const char* format_str = argv[3];

    // Parse format
    size_t available_space = 0;
    Format format;
    if (strcmp(format_str, "psx") == 0) {
        // The PS1 has 512 KB of sound RAM, I allocate 380 KB for music instruments
        format = FORMAT_PSX;
        available_space = 380 * 1024;
    }
    else if (strcmp(format_str, "pcm16") == 0) {
        format = FORMAT_PCM16;
        available_space = 256 * 1024 * 1024;
    }

    uint8_t* scratch_buffer = malloc(available_space);
    uint8_t* sample_stack = malloc(available_space);
    uint8_t* sample_stack_cursor = &sample_stack[0];
    uint32_t sample_offsets[1024] = {0};
    char* sample_names[1024] = { 0 };
    InstRegion inst_regions[1024];
    SampleHeader sample_headers[1024];
    int size_left = available_space;
    uint32_t n_samples = 0;
    uint16_t region_indices_per_instrument[256][16] = { 0 };
    uint16_t region_count_per_instrument[256] = { 0 };

    // Open the soundbank definition file
    FILE* sbk_def_file = fopen(path, "r");
    if (sbk_def_file == NULL) {
        printf("Failed to open file '%s'\n", path);
    }

    // Find file path from input
    int last_slash_index = 0;
    int i = 0;
    while (path[i] != 0) {
        if (path[i] == '/' || path[i] == '\\') {
            last_slash_index = i;
        }
        i++;
    }
    
    char* folder = malloc(last_slash_index + 2);
    memcpy(folder, path, last_slash_index + 1);
    folder[last_slash_index + 1] = 0;

    // Loop over all the entries in the file
    while(1)
    {
        // Read a line
        char line[1024];
        if (fgets(line, sizeof(line), sbk_def_file) == NULL)
            break;

        // Ignore comments
        if (line[0] == '#')
            continue;

        // Parse data
        struct {
            unsigned int instrument_id;
            unsigned int key_min;
            unsigned int key_max;
            unsigned int delay;
            unsigned int attack;
            unsigned int hold;
            unsigned int decay;
            unsigned int sustain;
            unsigned int release;
            unsigned int volume;
            unsigned int panning;
            char sample_source[128];
        } instrument_info;
        sscanf(line, "%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;%s",
            &instrument_info.instrument_id,
            &instrument_info.key_min,
            &instrument_info.key_max,
            &instrument_info.delay,
            &instrument_info.attack,
            &instrument_info.hold,
            &instrument_info.decay,
            &instrument_info.sustain,
            &instrument_info.release,
            &instrument_info.volume,
            &instrument_info.panning,
            instrument_info.sample_source
        );

        // Align to 16 bytes - should be unnecessary but you never know
        while (size_left % 16 != 0) {
            sample_stack_cursor++;
            size_left--;
        }

        // Find wave sample path
        size_t length_sample_source = strlen(instrument_info.sample_source);
        char* sample_path = malloc(strlen(folder) + length_sample_source + 1);
        memcpy(sample_path, folder, last_slash_index + 1);
        memcpy(sample_path + last_slash_index + 1, instrument_info.sample_source, length_sample_source + 1);

        // Load and convert the wave file
        WaveFile wave = load_wav(sample_path);
        int sample_length;
        if (wave.loop_end != -1) sample_length = wave.loop_end + 1;
        else sample_length = wave.length;

        int spu_sample_length = -1;
        if (format == FORMAT_PSX) {
            spu_sample_length = psx_audio_spu_encode_simple(wave.samples, sample_length, scratch_buffer, wave.loop_start);
        }
        else if (format == FORMAT_PCM16) {
            spu_sample_length = wave.length;
            memcpy(scratch_buffer, wave.samples, wave.length * sizeof(int16_t));
        }

        // If the data fits, copy it over, and add it to the list
        if (spu_sample_length <= size_left) {
            // Sample data
            memcpy(sample_stack_cursor, scratch_buffer, spu_sample_length);

            // Sample name
            sample_names[n_samples] = malloc(length_sample_source + 1);
            memcpy(sample_names[n_samples], instrument_info.sample_source, length_sample_source + 1);

            // Sample offset
            sample_offsets[n_samples] = sample_stack_cursor - sample_stack;

            // Sample header
            sample_headers[n_samples] = (SampleHeader){
                .sample_start = sample_offsets[n_samples],
                .sample_rate = wave.sample_rate,
            };

            // Instrument region
            inst_regions[n_samples] = (InstRegion){
                .sample_index = n_samples,
                .key_min      = instrument_info.key_min,
                .key_max      = instrument_info.key_max,
                .delay        = instrument_info.delay,
                .attack       = instrument_info.attack,
                .hold         = instrument_info.hold,
                .decay        = instrument_info.decay,
                .sustain      = instrument_info.sustain,
                .release      = instrument_info.release,
                .volume       = instrument_info.volume,
                .panning      = instrument_info.panning,
            };

            // Update instrument
            uint16_t* curr_index = &region_count_per_instrument[instrument_info.instrument_id];
            region_indices_per_instrument[instrument_info.instrument_id][*curr_index] = n_samples;
            *curr_index += 1;

            // Move to next sample
            n_samples++;
        }

        // If out of memory, still keep track of how big it is. This way the user can figure out how much data to shave off
        sample_stack_cursor += spu_sample_length;
        size_left -= spu_sample_length;
    }

    // Notify the user if we run out of RAM, might be nice for them to know.
    if (size_left < 0) {
        printf("Out of Sound RAM! Try downsampling or cutting the samples shorter\n");
        printf("Amount of bytes to reduce: %i\n", -size_left);
        return 1;
    }

    // Reorder the regions in a more sane way
    struct {
        uint16_t region_start_index;
        uint16_t n_regions;
    } inst_descs[256];

    InstRegion regions[1024];

    int better_index = 0;
    for (int i = 0; i < 256; ++i) {
        inst_descs[i].region_start_index = better_index;
        for (int j = 0; j < region_count_per_instrument[i]; ++j) {
            regions[better_index] = inst_regions[region_indices_per_instrument[i][j]];
            better_index++;
        }
        inst_descs[i].n_regions = better_index - inst_descs[i].region_start_index;
    }

    // Determine where and how big each section will be
    const uint32_t size_header = 20;
    uint32_t size_inst_descs = 256 * sizeof(uint16_t) * 2;
    uint32_t size_region_table = n_samples * sizeof(InstRegion);
    uint32_t size_sample_headers = n_samples * sizeof(SampleHeader);
    uint32_t size_sample_data = sample_stack_cursor - sample_stack;
    uint32_t offset_inst_descs = 0;
    uint32_t offset_region_table = offset_inst_descs + size_inst_descs;
    uint32_t offset_sample_headers = offset_region_table + size_region_table;
    uint32_t offset_sample_data = offset_sample_headers + size_sample_headers;

    // Write the output file
    FILE* out_file = fopen(out_path, "wb");
    fwrite("FSBK", 1, 4, out_file);
    fwrite(&n_samples, 1, 4, out_file);
    fwrite(&offset_inst_descs, 1, 4, out_file);
    fwrite(&offset_region_table, 1, 4, out_file);
    fwrite(&offset_sample_headers, 1, 4, out_file);
    fwrite(&offset_sample_data, 1, 4, out_file);
    fwrite(&size_sample_data, 1, 4, out_file);
    fwrite(inst_descs, sizeof(inst_descs[0]), 256, out_file);
    fwrite(regions, sizeof(regions[0]), n_samples, out_file);
    fwrite(sample_headers, sizeof(sample_headers[0]), n_samples, out_file);
    fwrite(sample_stack, 1, sample_stack_cursor - sample_stack, out_file);

    return 0;
}