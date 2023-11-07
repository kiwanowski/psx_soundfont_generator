#ifndef WAV
#define WAV

#include <stdint.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

typedef struct {
    uint16_t audio_format;    // Audio format (1 for PCM)
    uint16_t num_channels;    // Number of channels (e.g., 1 for mono, 2 for stereo)
    uint32_t sample_rate;     // Sample rate (e.g., 44100)
    uint32_t byte_rate;       // Byte rate (SampleRate * NumChannels * BitsPerSample / 8)
    uint16_t block_align;     // Block alignment (NumChannels * BitsPerSample / 8)
    uint16_t bits_per_sample;  // Bits per sample (e.g., 16 for 16-bit PCM)
} WavHeader;

typedef struct {
  int32_t        manufacturer;
  int32_t        product;
  int32_t        sample_period;
  int32_t        midi_unity_note;
  int32_t        midi_pitch_fraction;
  int32_t        smpte_format;
  int32_t        smpte_offset;
  int32_t        sample_loops;
  int32_t        sampler_data;
} SamplerChunk;

typedef struct {
  int32_t  identifier;
  int32_t  type;
  int32_t  start;
  int32_t  end;
  int32_t  fraction;
  int32_t  play_count;
} SampleLoop;

typedef struct {
    int16_t* samples;
    uint32_t sample_rate;
    int length;
    int loop_start;
    int loop_end;
} WaveFile;

int read_riff_chunk(FILE** file, char* name, uint32_t* size) {
    // Read name of chunk and add null terminator
    fread(name, 4, 1, *file);
    name[4] = 0;

    // Read size of chunk
    int bytes_read = fread(size, sizeof(uint32_t), 1, *file);

    // If both of these read operations failed, return 0
    if (bytes_read == 0) {
        return 0;
    }

    return 1;
}

WaveFile load_wav(const char* path) {
    // Open file
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        printf("Failed to open file %s\n", path);
        return;
    }
    
    char name[5] = {0};
    uint32_t size;

    // This should be the RIFF WAVE chunk
    read_riff_chunk(&file, name, &size);

    if (strcmp(name, "RIFF") != 0) {
        printf("Invalid RIFF file\n");
    }
    fread(&name, 4, 1, file);
    if (strcmp(name, "WAVE") != 0) {
        printf("Invalid WAVE file\n");
    }

    // We will gather this information
    WavHeader header;
    SamplerChunk sampler;
    WaveFile wave = {
        .samples = NULL,
        .sample_rate = 0,
        .length = -1,
        .loop_start = -1,
        .loop_end = -1,
    };

    while(1) {
        if (!read_riff_chunk(&file, name, &size)) break;

        // Sample metadata
        if (strcmp(name, "fmt ") == 0) {
            // Read header
            fread(&header, sizeof(header), 1, file);

            // Do we support this? if not, bail
            if (header.num_channels != 1) {
                printf("Only mono samples are supported for now!");
                return;
            }
            if (header.bits_per_sample != 16) {
                printf("Only 16-bit samples are supported for now!");
                return;
            }

            wave.sample_rate = header.sample_rate;
        }

        // Wave data
        else if (strcmp(name, "data") == 0) {
            wave.samples = (int16_t*)malloc(size);
            fread(wave.samples, 1, size, file);
            wave.length = size / 2;
        }

        // Sampler info (e.g. loop points)
        else if (strcmp(name, "smpl") == 0) {
            fread(&sampler, sizeof(sampler), 1, file);

            if (sampler.sample_loops > 0) {
                SampleLoop* sample_loop = (SampleLoop*)malloc(sampler.sample_loops * sizeof(SampleLoop));
                fread(sample_loop, sizeof(SampleLoop), sampler.sample_loops, file);
                wave.loop_start = sample_loop[0].start;
                wave.loop_end = sample_loop[0].end;
            }
        }

        // Skip unknown chunk
        else {
            fseek(file, size, SEEK_CUR);
        }
    }

    return wave;
}
#endif