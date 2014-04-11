#include <fftw3.h>

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

void calc_fft (double* sample, fftw_complex* fft, double* tmp, size_t sample_size)
{
    for (size_t i = 0; i < sample_size; ++i) tmp[i] = sample[i];
    fftw_plan plan_forward = fftw_plan_dft_r2c_1d(sample_size, tmp, fft, FFTW_ESTIMATE);
    fftw_execute (plan_forward);
    fftw_destroy_plan(plan_forward);
}

void calc_fft_mag (fftw_complex* fft, double* fft_mag, size_t sample_size)
{
    for (size_t i = 0; i < sample_size/2+1; ++i)
        fft_mag[i] = fft[i][0]*fft[i][0] + fft[i][1]*fft[i][1];
}

double dominant_freq (fftw_complex* fft, double* fft_mag, size_t sample_size, double sample_rate)
{
    double max = 0;
    long max_bin = 0;
    for (size_t i = 0; i < sample_size/2+1; ++i)
    {
        if (fft_mag[i] > max)
        {
            max = fft_mag[i];
            max_bin = i + 1;
        }
    }

    double peak  = fft[max_bin][0];
    double left  = fft[max_bin-1][0];
    double right = fft[max_bin+1][0];
    double delta = (right - left) / (2 * peak - left - right);
    return sample_rate / sample_size * (max_bin - delta);
}

double dominant_freq_lp (fftw_complex* fft, double* fft_mag, size_t sample_size, double sample_rate, int frequency)
{
    double bin_size = sample_rate/sample_size;
    double max = 0;
    double max_2 = 0; //second highest
    long max_bin = 0;
    long max_2_bin = 0;
    for (size_t i = 0; (i+1)*bin_size < frequency; ++i)
    {
        //double scale_factor = (frequency-((i+1)*bin_size))/frequency;
        if (fft_mag[i]/**scale_factor*/ > max)
        {
            max_2 = max;
            max_2_bin = max_bin;
            max = fft_mag[i];
            max_bin = i + 1;
        }
    }
    
    double peak  = fft[max_bin][0];
    double left  = fft[max_bin-1][0];
    double right = fft[max_bin+1][0];
    double delta = (right - left) / (2 * peak - left - right);
    
    double peak2  = fft[max_bin][0];
    double left2  = fft[max_bin-1][0];
    double right2 = fft[max_bin+1][0];
    double delta2 = (right2 - left2) / (2 * peak2 - left2 - right2);
    
    if ((peak<3*peak2) && (peak>1.25*peak2)){
        double candidate = sample_rate / sample_size * (max_2_bin - delta2);
        if (candidate > 75) return candidate;
    }
    
    double candidate = sample_rate / sample_size * (max_bin - delta);
    if (candidate > 75) return candidate;
    else return -INFINITY;
}
double calc_spectral_centroid(double* fft_mag, size_t sample_size, double sample_rate)
{
    double top_sum = 0;
    double bottom_sum = 0;
    for (int i=0; i<sample_size/2 + 1; ++i)
    {
        // spectral magnitude is already contained in fft_result
        bottom_sum+=fft_mag[i];
        top_sum+=fft_mag[i]*(i+1)*(sample_rate/sample_size);
    }
    double spectral_centroid = top_sum/bottom_sum;
    return spectral_centroid;
}

double avg_amplitude (double* fft_mag, size_t sample_size)
{
    double average = 0;
    for (size_t i = 0; i < sample_size/2 + 1; ++i)
    {
        average += fft_mag[i];
    }
    average = average/(sample_size/2 + 1);
    return average;
}

double calc_spectral_crest(double* fft_mag, size_t sample_size, double sample_rate)
{
    double max = 0;
    size_t maxi;
    double total = 0;
    for (int i = 0; i < sample_size/2 + 1; ++i)
    {
        total += fft_mag[i];
        if (fft_mag[i] > max)
        {
            max = fft_mag[i];
            maxi = i+1;
        }
    }
    double crest = (max*sample_size)/total;
    return crest;
}