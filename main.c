#include "pitch.h"
#include "midi.h"
#include "serial.h"

// #define GLFW_INCLUDE_GLCOREARB
#include <stdlib.h>
#include <stdio.h>
#include <math.h> //math comes before fftw so that fftw_complex is not overriden

#include <GLFW/glfw3.h>
#include <portaudio.h>
#include <fftw3.h>
#include <portmidi.h>

#define SAMPLE_RATE   (double)(44100)
#define FRAMES_PER_BUFFER  (64)
#define FFT_SIZE (1024) //512 = 11ms delay, 86Hz bins
#define ONSET_FFT_SIZE (64)
#define BIN_SIZE ((double) SAMPLE_RATE/FFT_SIZE)
#define ONSET_THRESHOLD (.025)
#define OFFSET_THRESHOLD (.015)
#define RETRIG_THRESHOLD (.1)

//fft generators et cetera
double fft_buffer[FFT_SIZE];
double ONSET_FFT_BUFFER[ONSET_FFT_SIZE];
int fft_buffer_loc;
fftw_complex fft[FFT_SIZE/2 + 1];
fftw_complex fft_fft[(FFT_SIZE/2 +1)/2 +1];
double fft_mag[FFT_SIZE/2 + 1];
double fft_fft_mag[(FFT_SIZE/2+1)/2+1];
double harmonics[FFT_SIZE/2+1];
//fft analyzers
double spectral_centroid;
double dominant_frequency;
double dominant_frequency_lp;
double average_amplitude;
double spectral_crest;
double spectral_flatness;
double harmonic_average;
//onset detection
double onset_fft_buffer[ONSET_FFT_SIZE];
fftw_complex onset_fft[ONSET_FFT_SIZE];
double onset_fft_mag[ONSET_FFT_SIZE/2+1];
double onset_average_amplitude;
int    onset_fft_buffer_loc;
int    onset_triggered;

static void glfwError (int error, const char* description)
{
    fputs(description, stderr);
}

static void paCheckError (PaError error)
{
    if (error == paNoError) return;

    Pa_Terminate();
    fprintf( stderr, "An error occured while using the portaudio stream\n" );
    fprintf( stderr, "Error number: %d\n", error );
    fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( error ) );
    exit(EXIT_FAILURE);
}

static void onKeyPress (GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);
}

static void update_fft_buffer(float mic_bit)
{
    onset_fft_buffer[onset_fft_buffer_loc] = mic_bit;
    ++onset_fft_buffer_loc;
    if (onset_fft_buffer_loc==ONSET_FFT_SIZE)
        {
            onset_fft_buffer_loc = 0;
            double tmp[ONSET_FFT_SIZE];
            calc_fft(onset_fft_buffer, onset_fft, tmp, ONSET_FFT_SIZE);
            calc_fft_mag(onset_fft, onset_fft_mag, ONSET_FFT_SIZE/2+1);
            onset_average_amplitude = calc_avg_amplitude(onset_fft_mag, ONSET_FFT_SIZE, SAMPLE_RATE, 0, SAMPLE_RATE/2);
        }
    //stall calculations of large ffts until onset is detected. This will currently cancel the last 25ms of a transform that with p>.5, should happen. IDC right now. Mechanism is to reset fft_buffer_loc back to the beginning.
    if (onset_average_amplitude<ONSET_THRESHOLD) fft_buffer_loc = 0;
    fft_buffer[fft_buffer_loc] = mic_bit;
    ++fft_buffer_loc;
    if (fft_buffer_loc==FFT_SIZE)
        {
            fft_buffer_loc=0;
            double tmp[FFT_SIZE];
//            double tmp2[FFT_SIZE/2 + 1];
            calc_fft(fft_buffer, fft, tmp, FFT_SIZE);
            calc_fft_mag(fft, fft_mag, FFT_SIZE);
            //calc_fft(fft_mag, fft_fft, tmp2, FFT_SIZE/2 +1);
            //calc_fft_mag(fft_fft, fft_fft_mag, FFT_SIZE/2 + 1);
            dominant_frequency = 0; //dominant_freq(fft, fft_mag, FFT_SIZE, SAMPLE_RATE);
            spectral_centroid = calc_spectral_centroid(fft_mag,FFT_SIZE, SAMPLE_RATE);
            dominant_frequency_lp = dominant_freq_lp(fft, fft_mag, FFT_SIZE, SAMPLE_RATE, 500);
            average_amplitude = calc_avg_amplitude(fft_mag, FFT_SIZE, SAMPLE_RATE, 0, FFT_SIZE/2);
            spectral_crest = 0; //calc_spectral_crest(fft_mag, FFT_SIZE, SAMPLE_RATE);
            spectral_flatness = 0; //calc_spectral_flatness(fft_mag, FFT_SIZE, SAMPLE_RATE, 0, SAMPLE_RATE/2);
            harmonic_average = calc_harmonics(fft, fft_mag, FFT_SIZE, SAMPLE_RATE); //useless and computationally intensive
            
            //onset fft settings and calculations
            
        }
}
static int onAudioSync (const void* inputBuffer, void* outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo* timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void* userData)
{
    float* in = (float*)inputBuffer;
    //float* out = (float*)outputBuffer;

    for (int i = 0; i < framesPerBuffer; ++i)
    {
        if (in[i] > 1){update_fft_buffer(1);printf("%s\n", "clipping high");continue;}
        if (in[i] < -1){update_fft_buffer(-1);printf("%s\n", "clipping low");continue;}
        update_fft_buffer(in[i]);
        // if (in[i] > 0.1) printf("%f\n", in[i]);
    }

    return paContinue;
}

double xLogNormalize(double unscaled, double logMax)
{
    return log10(unscaled)/logMax;
}
double dbNormalize(double magnitude, double max_magnitude, double dbRange)
{
    double absDb = 10*log10(magnitude/max_magnitude); //between -inf and 0
    return (dbRange+absDb)/dbRange; //between 0 and 1
}
int main (void)
{
    fft_buffer_loc = 0;
    onset_fft_buffer_loc = 0;
    onset_triggered = 0;
    
    // Initialize PortMidi
    midi_init();
    
    // Initialize RS-232 connection to glove
    int ser_live;
    int ser_buf[32];
    int ser_buf_loc;
    int midi_channel = 0;
    int angle = 0; //the glove is held at an angle, since serial commands come in in pairs, we need to pick just one.
    ser_live = serial_init();

    // Initialize PortAudio
    paCheckError(Pa_Initialize());

    // Configure stream input
    PaStreamParameters in;
    in.device = Pa_GetDefaultInputDevice();
    in.channelCount = 1;
    in.sampleFormat = paFloat32;
    in.suggestedLatency = Pa_GetDeviceInfo(in.device)->defaultLowInputLatency;
    in.hostApiSpecificStreamInfo = NULL;


    // Configure stream output
    PaStreamParameters out;
    out.device = Pa_GetDefaultOutputDevice();
    if (out.device == paNoDevice)
    {
        fprintf(stderr, "Error: No default output device.\n");
        exit(EXIT_FAILURE);
    }
    out.channelCount = 2;
    out.sampleFormat = paFloat32;
    out.suggestedLatency = Pa_GetDeviceInfo(out.device)->defaultLowOutputLatency;
    out.hostApiSpecificStreamInfo = NULL;

    // Initialize stream
    PaStream *stream;
    paCheckError(Pa_OpenStream(
                 &stream,
                 &in,
                 &out,
                 SAMPLE_RATE,
                 FRAMES_PER_BUFFER,
                 paClipOff,
                 onAudioSync,
                 NULL));

    // Initialize OpenGL window
    double dbRange = 96;
    GLFWwindow* trackerWindow;
    GLFWwindow* mainWindow;
    glfwSetErrorCallback(glfwError);
    if (!glfwInit()) exit(EXIT_FAILURE);
    mainWindow = glfwCreateWindow(640, 480, "Main Analysis", NULL, NULL);
    trackerWindow = glfwCreateWindow(640, 480, "Pitch Tracking", NULL, NULL);
    if (!mainWindow || !trackerWindow)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    // start stream
    paCheckError(Pa_StartStream(stream));
    
    
    float aspectRatio;
    int width, height;
    int note_on = 0;
    int pitchTrackerListSize = 256;
    float pitchTrackerList[pitchTrackerListSize];
    while (!glfwWindowShouldClose(mainWindow))
    {
        glfwMakeContextCurrent(mainWindow);
        glfwSetKeyCallback(mainWindow, onKeyPress);

        glfwGetFramebufferSize(mainWindow, &width, &height);
        aspectRatio = width / (float) height;

        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-aspectRatio, aspectRatio, -1.f, 1.f, 1.f, -1.f);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        
        //log lines
        glBegin(GL_LINES);
        int j = 0;
        int k = 10;
        double logLogLinesX = log10(SAMPLE_RATE/2);
        while(j<(SAMPLE_RATE/2))
        {
            glColor3f(0.6f,0.4f,0.1f);
            double logJ = xLogNormalize((double)j, logLogLinesX);
            glVertex3f(aspectRatio*(2*logJ-1), -1, 0.f);
            glVertex3f(aspectRatio*(2*logJ-1), 1, 0.f);
            j+=k;
            if (j==k*10) k*=10;
        }
        glEnd();
        
        //fft_mag graph (db, log)
        glBegin(GL_LINE_STRIP);
        glColor3f(1.0f,0.0f,1.0f);
        double logMax = log10(SAMPLE_RATE/2);
        for (int i = 0; i < FFT_SIZE/2+1; ++i)
        {
            double logI = xLogNormalize(i*BIN_SIZE, logMax);
            double scaledMag = dbNormalize(fft_mag[i], 1, dbRange); //max amplitude is FFT_SIZE/2)^2
            glVertex3f(2*aspectRatio*logI-aspectRatio, 2*scaledMag-1, 0.f);
        }
        glEnd();
        
        //specral centroid marker
        glBegin(GL_LINES);
        glColor3f(1.f, 0.f, 0.f);
        double logCentroid = log10(spectral_centroid)*(SAMPLE_RATE/2)/log10(SAMPLE_RATE/2+1);
        glVertex3f(2*aspectRatio*logCentroid/(SAMPLE_RATE/2)-aspectRatio, -1, 0.f);
        glVertex3f(2*aspectRatio*logCentroid/(SAMPLE_RATE/2)-aspectRatio, 1, 0.f);
        glEnd();
        
//        //dominant pitch line
//        glBegin(GL_LINES);
//        glColor3f(0.f, 1.f, 0.f);
//        logMax = log10(SAMPLE_RATE/2);
//        double logNormDomFreq = xLogNormalize(dominant_frequency, logMax);
//        glVertex3f(aspectRatio*(2*logNormDomFreq-1), -1, 0.f);
//        glVertex3f(aspectRatio*(2*logNormDomFreq-1), 1, 0.f);
//        glEnd();
        
        //dominant pitch line (lowpassed)
        glBegin(GL_LINES);
        glColor3f(0.f, 1.f, 1.f);
        double domlogMax = log10(SAMPLE_RATE/2);
        double logNormDomFreq_lp = xLogNormalize(dominant_frequency_lp, domlogMax);
        glVertex3f(aspectRatio*(2*logNormDomFreq_lp-1), -1, 0.f);
        glVertex3f(aspectRatio*(2*logNormDomFreq_lp-1), 1, 0.f);
        glEnd();

        glfwSwapBuffers(mainWindow);
        glfwPollEvents();

        
        //SERIAL DATA HANDLING
        if (ser_live)
        {
            ser_buf_loc = serial_poll(ser_buf);
            for (int i=0; i<ser_buf_loc; ++i)
            {
                if (ser_buf[i]<0)
                {
                    if (midi_channel!=ser_buf[i]+128)
                    {
                        midi_channel = ser_buf[i]+128;
                        midi_NOFF(); // clear all notes
                        note_on = 0;
                    }
                }
                else angle = ser_buf[i];
            }
        }
        //MIDI OUT STATEMENTS
        int outputPitch = -INFINITY;
        if (onset_average_amplitude>ONSET_THRESHOLD){
            if (!note_on)
            {
                midi_write(Pm_Message(0x90|midi_channel, 54, 100/*(int)average_amplitude*/));
                note_on = 1;
            }
            //0x2000 is 185 hz, 0x0000 is 73.416, 0x3fff is 466.16
            double midiNumber = 12 * log2(dominant_frequency_lp/440) + 69;
            //0x0000 is 38, 0x3fff is 70
            outputPitch = (int)((midiNumber-38)/32*0x3FFF);
            if (outputPitch > 0x3FFF) outputPitch = 0x3FFF;
            if (outputPitch < 0x0000) outputPitch = 0x0000;
            int lsb_7 = outputPitch&0x7F;
            int msb_7 = (outputPitch>>7)&0x7F;
            int outputCentroid = (int)((spectral_centroid-400)/400*127);
            if (outputCentroid > 127) outputCentroid = 127;
            if (outputCentroid < 000) outputCentroid = 000;
            
            if (ser_live) midi_write(Pm_Message(0xB0/*|midi_channel*/, 0, angle));
            midi_write(Pm_Message(0xB0/*|midi_channel*/, 1, outputCentroid));
            midi_write(Pm_Message(0xE0|midi_channel, lsb_7, msb_7));
        }
        else if (onset_average_amplitude<OFFSET_THRESHOLD){
            if (note_on) {
                midi_write(Pm_Message(0x80|midi_channel, 54, 100));
                note_on = 0;
            }
            //midi_write(Pm_Message(0x80|midi_channel, 54, 100));
            outputPitch = -INFINITY;
        }
        midi_flush();
        
        //PITCH TRACKING WINDOW
        glfwMakeContextCurrent(trackerWindow);
        glfwSetKeyCallback(trackerWindow, onKeyPress);
        
        glfwGetFramebufferSize(trackerWindow, &width, &height);
        aspectRatio = width / (float) height;
        
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
        
        glClearColor(1, 1, 1, 1);
        
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-aspectRatio, aspectRatio, -1.f, 1.f, 1.f, -1.f);
        
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        
        //log lines
        glBegin(GL_LINES);
        for (int i = 0; i<36; ++i)
        {
            glColor3f(0.9f,0.7f,0.5f);
            glVertex3f(aspectRatio*-1, 2*(i/36.f)-1, 0.f);
            glVertex3f(aspectRatio, 2*(i/36.f)-1, 0.f);
        }
        glEnd();

        for (int i = 1; i < pitchTrackerListSize; ++i)
        {
            pitchTrackerList[i] = pitchTrackerList[i+1];
        }
        pitchTrackerList[pitchTrackerListSize-1] = (float)outputPitch/0x3FFF;
        
        glBegin(GL_LINES);
        glColor3f(0.f, 0.f, 0.1f);
        for (int i = 0; i < pitchTrackerListSize; ++i)
        {
            float xPos1 = aspectRatio*((2*i/(float)pitchTrackerListSize)-1);
            float xPos2 = aspectRatio*((2*(i+1)/(float)pitchTrackerListSize)-1);
            float yPos = 2*pitchTrackerList[i]-1;
            glVertex3f(xPos1, yPos, 0.f);
            glVertex3f(xPos2, yPos, 0.f);
        }
        glEnd();
        
        glBegin(GL_LINES);
        for (int i = 0; i < pitchTrackerListSize; ++i)
        {
            if (i!=0)
            {
                if ((pitchTrackerList[i-1] < 0) & (pitchTrackerList[i]>=0))
                {
                    float xPos = aspectRatio*((2*i/(float)pitchTrackerListSize)-1);
                    glColor3f(0.0f, 0.3f, 0.0f);
                    glVertex3f(xPos, -1, 0.f);
                    glVertex3f(xPos, 1, 0.f);
                }
            }
            if (i!=pitchTrackerListSize)
            {
                if ((pitchTrackerList[i+1] < 0) & (pitchTrackerList[i]>=0))
                {
                    float xPos = aspectRatio*((2*i/(float)pitchTrackerListSize)-1);
                    glColor3f(0.3f, 0.0f, 0.0f);
                    glVertex3f(xPos, -1, 0.f);
                    glVertex3f(xPos, 1, 0.f);
                }
            }

        }
        glEnd();
        
        
        glfwSwapBuffers(trackerWindow);
        glfwPollEvents();
        

        

        
        
        //PRINT STATEMENTS
        //        printf("full_spectrum amplitude: %f\n", average_amplitude);
        //        printf("full_spectrum_flatness: %f\n", spectral_flatness);
        //        printf("low_passed_dom_freq : %f\n", dominant_frequency_lp);
        //        printf("first 8 bins: %06.2f %06.2f %06.2f %06.2f %06.2f %06.2f %06.2f %06.2f\n", fft_mag[0], fft_mag[1], fft_mag[2], fft_mag[3], fft_mag[4], fft_mag[5], fft_mag[6], fft_mag[7]);
        //        printf("onset amplitude: %f\n",onset_average_amplitude);
//            printf("harmonic average vs. lp_pitch: %f %f\n", harmonic_average, dominant_frequency_lp);
        printf("midi channel, angle: %i\t %i\n",midi_channel, angle);
    }
    
    

    

    // Shut down GLFW
    glfwDestroyWindow(mainWindow);
    glfwDestroyWindow(trackerWindow);
    glfwTerminate();
    
    // Shut down PortAudio
    paCheckError(Pa_StopStream(stream));
    paCheckError(Pa_CloseStream(stream));
    Pa_Terminate();

    // Shut down PortMidi
    midi_cleanup();
    
    // Exit happily
    exit(EXIT_SUCCESS);
}