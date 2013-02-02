#include "Python.h"
#include <mpg123.h> // has to be before Python.h for some reason

#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <strings.h>
#include <sndfile.h>

#include <wand/MagickWand.h>

static PyObject *
cwaveform_draw(PyObject * self, PyObject * args, PyObject * keywds)
{
    const int speedHackFrames = 500;

    // arg list
    char * inAudioFile;
    char * outImageFile;
    long imageWidth;
    long imageHeight;
    char cheat; //bool

    // get arguments
    if (!PyArg_ParseTuple(args, "ssllb",
        &inAudioFile, &outImageFile, &imageWidth, &imageHeight,
        &cheat))
    {
        return NULL;
    }

    // try libsndfile, then libmpg123, then give up
    int libToUse = 0; // 0 - sndfile, 1 - mpg123

    int frameCount = -1;
    int channelCount = -1;
    int frameSize = -1;
    const float sampleMin = (float) SHRT_MIN;
    const float sampleMax = (float) SHRT_MAX;
    const float sampleRange = (sampleMax - sampleMin);

    // libsndfile variables
    SF_INFO sfInfo;
    short * frames = NULL;

    // mpg123 variables
    mpg123_handle * mh = NULL;

    // try libsndfile
    memset(&sfInfo, 0, sizeof(SF_INFO));
    SNDFILE * sfFile = sf_open(inAudioFile, SFM_READ, &sfInfo);
    if (sfFile == NULL) {
        // sndfile no good. let's try mpg123.
        int err = mpg123_init();
        int encoding = 0;
        long rate = 0;
        if (err != MPG123_OK || (mh = mpg123_new(NULL, &err)) == NULL
            // let mpg123 work with thefile, that excludes MPG123_NEED_MORE
            // messages.
            || mpg123_open(mh, inAudioFile) != MPG123_OK
            // peek into the track and get first output format.
            || mpg123_getformat(mh, &rate, &channelCount, &encoding)
                != MPG123_OK)
        {
            mpg123_close(mh);
            mpg123_delete(mh);
            mpg123_exit();
            PyErr_SetString(PyExc_IOError, "Unrecognized audio format.");
            return NULL;
        }

        mpg123_format_none(mh);
        mpg123_format(mh, rate, channelCount, encoding);
        
        // mpg123 is good. compute stuff
        libToUse = 1;
        mpg123_scan(mh);
        int sampleSize = 2;
        frameSize = sampleSize * channelCount;

        int sampleCount = mpg123_length(mh);
        frameCount = sampleCount / channelCount;
    } else {
        // sndfile is good. compute stuff
        frameCount = sfInfo.frames;
        channelCount = sfInfo.channels;
    }

    float framesPerPixel = frameCount / ((float)imageWidth);
    int framesToSee = (int)framesPerPixel;
    // speed hack
    if (cheat) {
        if (framesToSee > speedHackFrames)
            framesToSee = speedHackFrames;
    }
    int framesTimesChannels = framesToSee * channelCount;

    // allocate memory to read from library
    frames = (short *) malloc(sizeof(short) * channelCount * framesToSee);
    if (frames == NULL) {
        sf_close(sfFile);
        PyErr_SetString(PyExc_MemoryError, "Out of memory.");
        return NULL;
    }

    // image magick crap
    MagickWandGenesis();
    MagickWand * wand = NewMagickWand();
    DrawingWand * draw = NewDrawingWand();

    // create colors
    PixelWand * bgPixWand = NewPixelWand();
    PixelWand * fgPixWand = NewPixelWand();

    PixelSetRed(bgPixWand, 1.0);
    PixelSetGreen(bgPixWand, 1.0);
    PixelSetBlue(bgPixWand, 1.0);

    PixelSetRed(fgPixWand, 0.0);
    PixelSetGreen(fgPixWand, 0.0);
    PixelSetBlue(fgPixWand, 0.0);

    // create image
    MagickNewImage(wand, imageWidth, imageHeight, bgPixWand);

    // create drawing wand
    DrawSetFillColor(draw, fgPixWand);
    DrawSetFillOpacity(draw, 1);

    // for each pixel
    int imageBoundY = imageHeight-1;
    int x;
    for (x=0; x<imageWidth; ++x) {
        // range of frames that fit in this pixel
        int start = x * framesPerPixel;

        // get the min and max of this range
        float min = sampleMax;
        float max = sampleMin;
        if (libToUse == 0) { // sndfile
            sf_seek(sfFile, start, SEEK_SET);
            sf_readf_short(sfFile, frames, framesToSee);
        } else if (libToUse == 1) {
            size_t done;
            mpg123_seek(mh, start*channelCount, SEEK_SET);
            mpg123_read(mh, (unsigned char *) frames, framesToSee*frameSize,
                &done);
        }

        // for each frame from start to end
        int i;
        for (i=0; i<framesTimesChannels; i+=channelCount) {
            // average the channels
            float value = 0;
            int c;
            for (c=0; c<channelCount; ++c)
                value += frames[i+c];
            value /= channelCount;
            
            // keep track of max/min
            if (value < min)
                min = value;
            if (value > max)
                max = value;
        }
        // translate into y pixel coord
        int yMin = (int)((min - sampleMin) / sampleRange * imageBoundY);
        int yMax = (int)((max - sampleMin) / sampleRange * imageBoundY);
        DrawRectangle(draw, x, yMin, x+1, yMax);
    }
    // save the image
    MagickDrawImage(wand, draw);
    MagickTransparentPaintImage(wand, fgPixWand, 0, 0.0, MagickFalse);
    MagickWriteImage(wand, outImageFile);

    // clean up
    draw = DestroyDrawingWand(draw);
    DestroyPixelWand(bgPixWand);
    DestroyPixelWand(fgPixWand);
    wand = DestroyMagickWand(wand);
    MagickWandTerminus();

    free(frames);
    if (libToUse == 0) {
        // libsndfile
        sf_close(sfFile);
    } else if (libToUse == 1) {
        // libmpg123
        mpg123_close(mh);
        mpg123_delete(mh);
        mpg123_exit();
    }

    // no return value
    Py_INCREF(Py_None);
    return Py_None;
}


/* List of functions defined in the module */

static PyMethodDef cwaveform_methods[] = {
    {"draw",        (PyCFunction)cwaveform_draw,        METH_VARARGS|METH_KEYWORDS},
    {NULL,        NULL}        /* sentinel */
};


/* Initialization function for the module (*must* be called initcwaveform) */

void initcwaveform()
{
    PyObject *m;

    /* Create the module and add the functions */
    m = Py_InitModule("cwaveform", cwaveform_methods);
}
