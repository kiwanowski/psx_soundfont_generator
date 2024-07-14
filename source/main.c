#include "libpsxav.h"
#include "wav.h"
#include <stdlib.h>

typedef struct {
    uint8_t key_min;                // Minimum MIDI key for this instrument region
    uint8_t key_max;                // Maximum MIDI key for this instrument region
    uint16_t volume_multiplier;     // @8.8 fixed point volume multiplier
    uint32_t sample_start;          // Offset (bytes) into sample data chunk. Can be written to SPU Sample Start Address
    uint32_t sample_rate;           // Sample rate (Hz) at MIDI key 60 (C5)
    uint16_t reg_adsr1;             // Raw data to be written to SPU_CH_ADSR1 when enabling a note
    uint16_t reg_adsr2;             // Raw data to be written to SPU_CH_ADSR2 when enabling a note
    uint8_t panning;
    uint8_t _pad[3];
} InstRegion;

int main(int argc, char** argv) {
    // Validate input
    if (argc != 3) {
        printf("Usage: psx_soundfont_creator.exe <.csv> <.sbk>\n");
        exit(1);
    }
    const char* path = argv[1];
    const char* out_path = argv[2];

    // The PS1 has 512 KB of sound RAM, I allocate 380 KB for music instruments
    const uint8_t* scratch_buffer = malloc(380 * 1024);
    const uint8_t* sample_stack = malloc(380 * 1024); 
    uint8_t* sample_stack_cursor = sample_stack;
    uint32_t sample_offsets[1024] = {0};
    char* sample_names[1024] = { 0 };
    InstRegion inst_regions[1024];
    int size_left = 380 * 1024;
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
            unsigned int attack_mode;
            unsigned int attack_shift;
            unsigned int attack_step;
            unsigned int decay_shift;
            unsigned int sustain_level;
            unsigned int sustain_mode;
            unsigned int sustain_direction;
            unsigned int sustain_shift;
            unsigned int sustain_step;
            unsigned int release_step;
            unsigned int release_mode;
            unsigned int release_shift;
            unsigned int volume_multiplier;
            unsigned int panning;
            char sample_source[128];
        } instrument_info;
        sscanf(line, "%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;%s",
            &instrument_info.instrument_id,
            &instrument_info.key_min,
            &instrument_info.key_max,
            &instrument_info.attack_mode,
            &instrument_info.attack_shift,
            &instrument_info.attack_step,
            &instrument_info.decay_shift,
            &instrument_info.sustain_level,
            &instrument_info.sustain_mode,
            &instrument_info.sustain_direction,
            &instrument_info.sustain_shift,
            &instrument_info.sustain_step,
            &instrument_info.release_step,
            &instrument_info.release_mode,
            &instrument_info.release_shift,
            &instrument_info.volume_multiplier,
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
        int spu_sample_length = psx_audio_spu_encode_simple(wave.samples, sample_length, scratch_buffer, wave.loop_start);

        // If the data fits, copy it over, and add it to the list
        if (spu_sample_length <= size_left) {
            // Sample data
            memcpy(sample_stack_cursor, scratch_buffer, spu_sample_length);

            // Sample name
            sample_names[n_samples] = malloc(length_sample_source + 1);
            memcpy(sample_names[n_samples], instrument_info.sample_source, length_sample_source + 1);

            // Sample offset
            sample_offsets[n_samples] = sample_stack_cursor - sample_stack;

            // Instrument region
            inst_regions[n_samples] = (InstRegion){
                .key_min = instrument_info.key_min,
                .key_max = instrument_info.key_max,
                .volume_multiplier = instrument_info.volume_multiplier,
                .sample_start = sample_offsets[n_samples],
                .sample_rate = wave.sample_rate,
                .reg_adsr1 =
                    instrument_info.attack_mode << 15 |
                    instrument_info.attack_shift << 10 |
                    instrument_info.attack_step << 8 |
                    instrument_info.decay_shift << 4 |
                    instrument_info.sustain_level << 0,
                .reg_adsr2 =
                    instrument_info.sustain_mode << 15 |
                    instrument_info.sustain_direction << 14 |
                    instrument_info.sustain_shift << 8 |
                    instrument_info.sustain_step << 6 |
                    instrument_info.release_mode << 5 |
                    instrument_info.release_shift << 0,
                .panning = instrument_info.panning,
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

    // Determine how big each section will be
    const uint32_t size_header = 20;
    uint32_t size_inst_descs = 256 * sizeof(uint16_t) * 2;
    uint32_t size_region_table = n_samples * sizeof(InstRegion);
    uint32_t size_sample_data = sample_stack_cursor - sample_stack;

    // Define offsets
    uint32_t offset_inst_descs = 0;
    uint32_t offset_region_table = offset_inst_descs + size_inst_descs;
    uint32_t offset_sample_data = offset_region_table + size_region_table;

    // Create the output file
    FILE* out_file = fopen(out_path, "wb");

    // Write the file header
    fwrite("FSBK", 1, 4, out_file);
    fwrite(&n_samples, 1, 4, out_file);
    fwrite(&offset_inst_descs, 1, 4, out_file);
    fwrite(&offset_region_table, 1, 4, out_file);
    fwrite(&offset_sample_data, 1, 4, out_file);
    fwrite(&size_sample_data, 1, 4, out_file);

    // Write instrument descriptions
    fwrite(inst_descs, sizeof(inst_descs[0]), 256, out_file);

    // Write region table
    fwrite(regions, sizeof(regions[0]), n_samples, out_file);

    // Write raw sample data
    fwrite(sample_stack, 1, sample_stack_cursor - sample_stack, out_file);

    return 0;
}