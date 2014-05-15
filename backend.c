#include "backend.h"
#include "dywapitchtrack.h"
#include "filter.h"
#include "pitch.h"
#include "windowing.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <fftw3.h>

// FFT data
double       fft_buffer[FFT_SIZE];
int          fft_buffer_loc;
fftw_complex fft[FFT_SIZE];
double       fft_mag[FFT_SIZE/2 + 1];
double       fft_band_mag[FFT_SIZE/2 + 1];
fftw_complex fft_fft[FFT_SIZE/2 + 1];
double       fft_fft_mag[(FFT_SIZE/2 + 1)/2 + 1];
double       harmonics[FFT_SIZE/2+1];

// FFT characteristics
double       spectral_centroid;
double       dominant_frequency;
double       dominant_frequency_lp;
double       average_amplitude;
double       spectral_crest;
double       spectral_flatness;
double       harmonic_average;

// Onset detection
int          window_function;
bool         note_on;
double       onset_fft_buffer[ONSET_FFT_SIZE];
fftw_complex onset_fft[ONSET_FFT_SIZE];
double       onset_fft_mag[ONSET_FFT_SIZE/2+1];
double       onset_average_amplitude;
int          onset_fft_buffer_loc;
int          onset_triggered;

// Hysterisis
double       prev_spectral_centroid = -INFINITY;
double       prev_output_pitch = -INFINITY;

// Formants
double       formant_buffer[FFT_SIZE];
double       formant_pitch;

// // Dynamic wavelet pitch tracker
// dywapitchtracker pitch_tracker;

void backend_init ()
{
    fft_buffer_loc = 0;
    onset_fft_buffer_loc = 0;
    onset_triggered = 0;
    dywapitch_inittracking(&pitch_tracker);
}

bool backend_push_sample (float sample)
{
    onset_fft_buffer[onset_fft_buffer_loc] = sample;
    ++onset_fft_buffer_loc;
    if (onset_fft_buffer_loc==ONSET_FFT_SIZE)
    {
        onset_fft_buffer_loc = 0;
        calc_fft(onset_fft_buffer, onset_fft, ONSET_FFT_SIZE);
        calc_fft_mag(onset_fft, onset_fft_mag, ONSET_FFT_SIZE/2+1);
        onset_average_amplitude = calc_avg_amplitude(onset_fft_mag, ONSET_FFT_SIZE, SAMPLE_RATE, 0, SAMPLE_RATE/2);
    }
    //stall calculations of large ffts until onset is detected. This will currently cancel the last 25ms of a transform that with p>.5, should happen. IDC right now. Mechanism is to reset fft_buffer_loc back to the beginning.
    if (onset_average_amplitude<ONSET_THRESHOLD && !note_on) fft_buffer_loc = 0;
    if (onset_average_amplitude<OFFSET_THRESHOLD && note_on) {fft_buffer_loc = 0;}
    fft_buffer[fft_buffer_loc] = sample;
    ++fft_buffer_loc;
    if (fft_buffer_loc==FFT_SIZE)
    {
        fft_buffer_loc=0;
        calc_fft(fft_buffer, fft, FFT_SIZE);
        calc_fft_mag(fft, fft_mag, FFT_SIZE);
        // double tmp2[FFT_SIZE/2 + 1];
        switch (window_function) {
            case WELCH:
                welch_window(fft_mag, FFT_SIZE/2+1, fft_mag); break;
            case HANNING:
                hanning_window(fft_mag, FFT_SIZE/2+1, fft_mag); break;
            case HAMMING:
                hamming_window(fft_mag, FFT_SIZE/2+1, fft_mag); break;
            case BLACKMAN:
                blackman_window(fft_mag, FFT_SIZE/2+1, fft_mag); break;
            case NUTTAL:
                nuttal_window(fft_mag, FFT_SIZE/2+1, fft_mag); break;
            default:
                break;
        }
        //calc_fft(fft_mag, fft_fft, tmp2, FFT_SIZE/2 +1);
        //calc_fft_mag(fft_fft, fft_fft_mag, FFT_SIZE/2 + 1);
        dominant_frequency = 0; //dominant_freq(fft, fft_mag, FFT_SIZE, SAMPLE_RATE);
        spectral_centroid = calc_spectral_centroid(fft_mag,FFT_SIZE, SAMPLE_RATE);
        dominant_frequency_lp = dominant_freq_lp(fft, fft_mag, FFT_SIZE, SAMPLE_RATE, 5000);
        // dominant_frequency_lp = dominant_freq_bp(fft, fft_mag, FFT_SIZE, SAMPLE_RATE, 900, 2800);
        // dominant_frequency_lp = dywapitch_computepitch(&pitch_tracker, fft_buffer, 0, FFT_SIZE);
        average_amplitude = calc_avg_amplitude(fft_mag, FFT_SIZE, SAMPLE_RATE, 0, FFT_SIZE/2);
        spectral_crest = 0; //calc_spectral_crest(fft_mag, FFT_SIZE, SAMPLE_RATE);
        spectral_flatness = 0; //calc_spectral_flatness(fft_mag, FFT_SIZE, SAMPLE_RATE, 0, SAMPLE_RATE/2);
        harmonic_average = 0; //calc_harmonics(fft, fft_mag, FFT_SIZE, SAMPLE_RATE); //useless and computationally intensive

        // Formants
        // band_pass(fft_buffer, formant_buffer, FFT_SIZE, SAMPLE_RATE, FORMANT_MIN_FREQ, FORMANT_MAX_FREQ);
        // formant_pitch = _dywapitch_computeWaveletPitch(formant_buffer, 0, FFT_SIZE);
        // printf("%f, %f\n", dominant_frequency_lp, formant_pitch);

        // printf("%f\n", spectral_centroid);

        return true;
    }
    return false;
}