#include "backend.h"
#include "gui.h"
#include "windowing.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// #define GLFW_INCLUDE_GLCOREARB // Enable OpenGL 3
#include <GLFW/glfw3.h>

#define PITCHTRACKERLISTSIZE 256
#define SPECTROGRAM_LENGTH   100

static GLFWwindow* trackerWindow;
static GLFWwindow* mainWindow;

static double dbRange;
static int    width, height;
static float  aspectRatio;
static float  pitchTrackerList[PITCHTRACKERLISTSIZE];

int spectrogram_buffer_loc;
double spectrogram_buffer[SPECTROGRAM_LENGTH][FFT_SIZE/2+1];

static void on_key_press (GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);
    if (key == GLFW_KEY_0 && action == GLFW_PRESS) window_function = RECTANGLE;
    if (key == GLFW_KEY_1 && action == GLFW_PRESS) window_function = WELCH;
    if (key == GLFW_KEY_2 && action == GLFW_PRESS) window_function = HANNING;
    if (key == GLFW_KEY_3 && action == GLFW_PRESS) window_function = HAMMING;
    if (key == GLFW_KEY_4 && action == GLFW_PRESS) window_function = BLACKMAN;
    if (key == GLFW_KEY_5 && action == GLFW_PRESS) window_function = NUTTAL;
}

static double x_log_normalize (double unscaled, double logMax)
{
    return log10(unscaled)/logMax;
}

static double db_normalize (double magnitude, double max_magnitude, double dbRange)
{
    double absDb = 10*log10(magnitude/max_magnitude); //between -inf and 0
    return (dbRange+absDb)/dbRange; //between 0 and 1
}

static void on_glfw_error (int error, const char* description)
{
    fputs(description, stderr);
}

static void graph_log_lines (double aspectRatio)
{
    //log lines
    glBegin(GL_LINES);
    int j = 0;
    int k = 10;
    double logLogLinesX = log10(SAMPLE_RATE/2);
    while(j<(SAMPLE_RATE/2))
    {
        glColor3f(0.6f,0.4f,0.1f);
        double logJ = x_log_normalize((double)j, logLogLinesX);
        glVertex3f(aspectRatio*(2*logJ-1), -1, 0.f);
        glVertex3f(aspectRatio*(2*logJ-1), 1, 0.f);
        j+=k;
        if (j==k*10) k*=10;
    }
    glEnd();
}

static void graph_fft_mag (int dbRange, double aspectRatio)
{
    //fft_mag graph (db, log)
    glBegin(GL_LINE_STRIP);
    glColor3f(1.0f,0.0f,1.0f);
    double logMax = log10(SAMPLE_RATE/2);
    for (int i = 0; i < FFT_SIZE/2+1; ++i)
    {
        double logI = x_log_normalize(i*BIN_SIZE, logMax);
        double scaledMag = db_normalize(fft_mag[i], 1, dbRange); //max amplitude is FFT_SIZE/2)^2
        glVertex3f(2*aspectRatio*logI-aspectRatio, 2*scaledMag-1, 0.f);
    }
    glEnd();
}

static void graph_spectral_centroid (double aspectRatio)
{
    //specral centroid marker
    glBegin(GL_LINES);
    glColor3f(1.f, 0.f, 0.f);
    double logCentroid = log10(spectral_centroid)*(SAMPLE_RATE/2)/log10(SAMPLE_RATE/2+1);
    glVertex3f(2*aspectRatio*logCentroid/(SAMPLE_RATE/2)-aspectRatio, -1, 0.f);
    glVertex3f(2*aspectRatio*logCentroid/(SAMPLE_RATE/2)-aspectRatio, 1, 0.f);
    glEnd();
}

static void graph_dominant_pitch_lp (double aspectRatio)
{
    //dominant pitch line (lowpassed)
    glBegin(GL_LINES);
    glColor3f(0.f, 1.f, 1.f);
    double domlogMax = log10(SAMPLE_RATE/2);
    double logNormDomFreq_lp = x_log_normalize(dominant_frequency_lp, domlogMax);
    glVertex3f(aspectRatio*(2*logNormDomFreq_lp-1), -1, 0.f);
    glVertex3f(aspectRatio*(2*logNormDomFreq_lp-1), 1, 0.f);
    glEnd();
}

static void graph_spectrogram (int dbRange, double aspectRatio){
    double logMax = log10(SAMPLE_RATE/2);
    for (int i = 0; i<SPECTROGRAM_LENGTH; ++i)
    {
        double curXLeft  = aspectRatio * (2 * (double)i/SPECTROGRAM_LENGTH - 1);
        double curXRight = aspectRatio * (2 * (double)(i+1)/SPECTROGRAM_LENGTH -1);
        glBegin(GL_QUAD_STRIP);
        glVertex3f(curXLeft , -1, 0);
        glVertex3f(curXRight, -1, 0);
        for (int j = 1; j<FFT_SIZE/2+1; ++j)
        {
            //draw QUAD for each bin?
            double logJ  = x_log_normalize(j*BIN_SIZE, logMax);
            double curYTop = 2*logJ-1;
            double scaledMag = spectrogram_buffer[(i+spectrogram_buffer_loc)%SPECTROGRAM_LENGTH][j];
            glColor3f(scaledMag, scaledMag, scaledMag);
            glVertex3f(curXLeft, curYTop, 0.f);
            glVertex3f(curXRight, curYTop, 0.f);
        }
        glEnd();
    }
}

void gui_init ()
{
    // Initialize OpenGL window
    dbRange = 96;
    glfwSetErrorCallback(on_glfw_error);
    if (!glfwInit()) exit(EXIT_FAILURE);
    mainWindow = glfwCreateWindow(640, 480, "Main Analysis", NULL, NULL);
    trackerWindow = glfwCreateWindow(640, 480, "Pitch Tracking", NULL, NULL);
    if (!mainWindow || !trackerWindow)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
}

void gui_cleanup ()
{
    // Shut down GLFW
    glfwDestroyWindow(mainWindow);
    glfwDestroyWindow(trackerWindow);
    glfwTerminate();
}

bool gui_should_exit ()
{
    return glfwWindowShouldClose(mainWindow) || glfwWindowShouldClose(trackerWindow);
}

void gui_fft_filled ()
{
    for (int i = 0; i < FFT_SIZE/2+1; ++i)
    {
        if (note_on) spectrogram_buffer[spectrogram_buffer_loc][i] = db_normalize(fft_mag[i], 1, 96);
        else spectrogram_buffer[spectrogram_buffer_loc][i] = 0;
    }
    spectrogram_buffer_loc = (spectrogram_buffer_loc+1)%SPECTROGRAM_LENGTH;
}

void gui_redraw ()
{
    glfwMakeContextCurrent(mainWindow);
    glfwSetKeyCallback(mainWindow, on_key_press);

    glfwGetFramebufferSize(mainWindow, &width, &height);
    aspectRatio = width / (float) height;

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-aspectRatio, aspectRatio, -1.f, 1.f, 1.f, -1.f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
//    graph_log_lines(aspectRatio);
//    graph_fft_mag(dbRange, aspectRatio);
//    graph_spectral_centroid(aspectRatio);
//    graph_dominant_pitch_lp(aspectRatio);
    graph_spectrogram(dbRange, aspectRatio);

    glfwSwapBuffers(mainWindow);
    glfwPollEvents();
    
    //PITCH TRACKING WINDOW
    glfwMakeContextCurrent(trackerWindow);
    glfwSetKeyCallback(trackerWindow, on_key_press);
    
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

    for (int i = 1; i < PITCHTRACKERLISTSIZE; ++i)
    {
        pitchTrackerList[i] = pitchTrackerList[i+1];
    }

    double midiNumber = 12 * log2(dominant_frequency_lp/440) + 69;
    int outputPitch = (int)((midiNumber-38)/32*0x3FFF);
    if (onset_average_amplitude < ONSET_THRESHOLD) outputPitch = -INFINITY;
    pitchTrackerList[PITCHTRACKERLISTSIZE-1] = (float)outputPitch/0x3FFF;
    
    glBegin(GL_LINES);
    glColor3f(0.f, 0.f, 0.1f);
    for (int i = 0; i < PITCHTRACKERLISTSIZE; ++i)
    {
        float xPos1 = aspectRatio*((2*i/(float)PITCHTRACKERLISTSIZE)-1);
        float xPos2 = aspectRatio*((2*(i+1)/(float)PITCHTRACKERLISTSIZE)-1);
        float yPos = 2*pitchTrackerList[i]-1;
        glVertex3f(xPos1, yPos, 0.f);
        glVertex3f(xPos2, yPos, 0.f);
    }
    glEnd();
    
    glBegin(GL_LINES);
    for (int i = 0; i < PITCHTRACKERLISTSIZE; ++i)
    {
        if (i!=0)
        {
            if ((pitchTrackerList[i-1] < 0) & (pitchTrackerList[i]>=0))
            {
                float xPos = aspectRatio*((2*i/(float)PITCHTRACKERLISTSIZE)-1);
                glColor3f(0.0f, 0.3f, 0.0f);
                glVertex3f(xPos, -1, 0.f);
                glVertex3f(xPos, 1, 0.f);
            }
        }
        if (i!=PITCHTRACKERLISTSIZE)
        {
            if ((pitchTrackerList[i+1] < 0) & (pitchTrackerList[i]>=0))
            {
                float xPos = aspectRatio*((2*i/(float)PITCHTRACKERLISTSIZE)-1);
                glColor3f(0.3f, 0.0f, 0.0f);
                glVertex3f(xPos, -1, 0.f);
                glVertex3f(xPos, 1, 0.f);
            }
        }

    }
    glEnd();
    
    glfwSwapBuffers(trackerWindow);
    glfwPollEvents();
}