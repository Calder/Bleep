#include "pitch.h"
#include "tinydir.h"

#include <fftw3.h>
#include <sndfile.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

// Return true iff str ends with suffix
bool ends_with (char* str, char* suffix)
{
    if (!str || !suffix) return false;
    size_t strl = strlen(str);
    size_t sufl = strlen(suffix);
    if (sufl > strl) return false;
    return strncmp(str + strl - sufl, suffix, sufl) == 0;
}

// Test pitch detection algorithms on a sample
// Note: Caller relinquishes ownership of sample.
bool test (char* test, double* sample, size_t sample_size, double sample_rate, double sample_freq)
{
    // Set up
    bool pass = true;
    fftw_complex* fft = malloc(sizeof(fftw_complex)*(sample_size/2+1));
    double* fft_tmp = malloc(sizeof(double)*sample_size);
    double* fft_mag = malloc(sizeof(double)*(sample_size/2+1));

    // Test functions
    calc_fft(sample, fft, fft_tmp, sample_size);
    calc_fft_mag(fft, fft_mag, sample_size);
    double dom = dominant_freq(fft, fft_mag, sample_size, sample_rate);
    double dom_err = dom - sample_freq;

    if (fabs(dom_err)/sample_freq > .0125)
    {
        fprintf(stderr, "FAILED: %s\n", test);
        fprintf(stderr, "    Sample size: %ld\n", sample_size);
        fprintf(stderr, "    Sample rate: %.0f\n", sample_rate);
        fprintf(stderr, "    Frequency:   %.0f\n", sample_freq);
        fprintf(stderr, "    Periods:     %.2f\n", sample_freq*sample_size/sample_rate);
        fprintf(stderr, "    Dominant = %f (%+.2fhz) (%+.1f%%)\n", dom, dom_err, 100*dom_err/sample_freq);
        pass = false;
        fprintf(stderr, "\n");
    }

    // Clean up
    free(sample);
    free(fft);
    free(fft_tmp);
    free(fft_mag);
    return pass;
}

// Test pitch detection on a sine wave
bool sine_test (size_t sample_size, double sample_rate, double sample_freq)
{
    double* sample = malloc(sizeof(double)*sample_size);
    for (size_t i = 0; i < sample_size; ++i)
        sample[i] = sin(2.f*M_PI*sample_freq*i/sample_rate+M_PI);
    return test("sine wave", sample, sample_size, sample_rate, sample_freq);
}

// Test pitch detection on a WAV file
bool file_test (char* file, double sample_freq)
{
    SF_INFO info;
    SNDFILE* f = sf_open(file, SFM_READ, &info);
    if (f == NULL)
    {
        fprintf(stderr, "FAILED: %s\n    File not found.\n", file);
        return false;
    }
    double* sample = malloc(sizeof(double)*info.frames);

    sf_read_double(f, sample, info.frames);
    test(file, sample, info.frames, info.samplerate, sample_freq);

    sf_close(f);
    return true;
}

bool dir_test (char* path)
{
    tinydir_dir dir;
    tinydir_open(&dir, path);

    while (dir.has_next)
    {
        tinydir_file file;
        tinydir_readfile(&dir, &file);
        if (file.name[0] != '.')
        {
            char file_path[1024];
            strcpy(file_path, path);
            strcat(file_path, "/");
            strcat(file_path, file.name);

            if (file.is_dir)
            {
                if (!dir_test(file_path)) goto fail;
            }
            else
            {
                double freq = atoi(file.name);
                if (freq > 0 && !file_test(file_path, freq)) goto fail;
            }
        }

        tinydir_next(&dir);
    }

    tinydir_close(&dir);
    return true;

fail:
    tinydir_close(&dir);
    return false;
}

int main (void)
{
    for (int f = 80; f < 1200; f += 1)
    {
        if (!sine_test(1024, 44100, f)) return 1;
    }

    char path[1024];
    getcwd(path, 1024);
    strcat(path, "/pitch_tests");
    dir_test(path);
}