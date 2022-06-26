// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef INKSCAPE_TRACE_SIOX_H
#define INKSCAPE_TRACE_SIOX_H
/*
 *  Copyright 2005, 2006 by Gerald Friedland, Kristian Jantz and Lars Knipping
 *
 *  Conversion to C++ for Inkscape by Bob Jamison
 *
 *  Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

/*
 * Note by Bob Jamison:
 * After translating the siox.org Java API to C++ and receiving an
 * education into this wonderful code, I began again,
 * and started this version using lessons learned. This version is
 * an attempt to provide an dependency-free SIOX engine that anyone
 * can use in their project with minimal effort.
 *
 * Many thanks to the fine people at siox.org.
 */

#include <string>
#include <vector>
#include <optional>
#include <gdkmm/pixbuf.h>
#include <glib.h>
#include "cielab.h"

namespace Inkscape {
namespace Trace {

/**
 * SioxImage is the input/output format of Siox.
 * It pairs a 32-bit image with an equally-sized matrix of floats representing foreground confidence values.
 */
class SioxImage
{
public:
    /**
     * Create an image from a Gdk::Pixbuf.
     * A copy of the pixbuf is set as the pixel data, while the confidence matrix is initialized to zero.
     */
    SioxImage(Glib::RefPtr<Gdk::Pixbuf> const &buf);

    /**
     * Create a Gdk::Pixbuf from this image.
     */
    Glib::RefPtr<Gdk::Pixbuf> getGdkPixbuf() const;

    /**
     * Return the image data buffer.
     */
    uint32_t *getImageData() { return pixdata.data(); }
    uint32_t const *getImageData() const { return pixdata.data(); }

    /**
     * Set a confidence value at the x, y coordinates to the given value.
     */
    void setConfidence(int x, int y, float conf) { cmdata[offset(x, y)] = conf; }

    /**
     * Return the confidence data buffer.
     */
    float *getConfidenceData() { return cmdata.data(); }
    float const *getConfidenceData() const { return cmdata.data(); }

    /**
     * Return the width of this image
     */
    int getWidth() const { return width; }

    /**
     * Return the height of this image
     */
    int getHeight() const { return height; }

    /**
     * Save this image as a simple color PPM
     */
    bool writePPM(char const *filename) const;

    /**
     * Return an extremely naive but fast hash of the image/confidence map contents.
     */
    unsigned hash() const;

private:
    int width;                     ///< Width of the image
    int height;                    ///< Height of the image
    std::vector<uint32_t> pixdata; ///< Pixel data
    std::vector<float> cmdata;     ///< Confidence matrix data

    /**
     * Return the offset of a given pixel within both data arrays.
     */
    int constexpr offset(int x, int y) const { return width * y + x; }
};

/**
 * This is a class for observing the progress of a Siox engine. Reimplement
 * the methods in your subclass to get the desired behaviour.
 */
class SioxObserver
{
public:
    virtual ~SioxObserver() = default;

    /**
     * Informs the observer how much has been completed.
     * Return false if the processing should be aborted.
     */
    virtual bool progress(float percentCompleted) { return true; }

    /**
     * Send an status string to the Observer.
     */
    virtual void trace(std::string const &msg) { g_message("Siox: %s\n", msg.c_str()); }

    /**
     * Send an error string to the Observer. Processing will be halted.
     */
    virtual void error(std::string const &msg) { g_warning("Siox error: %s\n", msg.c_str()); }
};

class Siox
{
public:
    /**
     * Confidence corresponding to a certain foreground region (equals one).
     */
    static constexpr float CERTAIN_FOREGROUND_CONFIDENCE = 1.0f;

    /**
     * Confidence for a region likely being foreground.
     */
    static constexpr float FOREGROUND_CONFIDENCE = 0.8f;

    /**
     * Confidence for foreground or background type being equally likely.
     */
    static constexpr float UNKNOWN_REGION_CONFIDENCE = 0.5f;

    /**
     * Confidence for a region likely being background.
     */
    static constexpr float BACKGROUND_CONFIDENCE = 0.1f;

    /**
     * Confidence corresponding to a certain background reagion (equals zero).
     */
    static constexpr float CERTAIN_BACKGROUND_CONFIDENCE = 0.0f;

    Siox(SioxObserver *observer = nullptr);

    /**
     * Extract the foreground of the original image, according to the values in the confidence matrix.
     * If the operation fails or is aborted, an empty optional is returned.
     * backgroundFillColor is any ARGB color, such as 0xffffff (white) or 0x000000 (black).
     */
    std::optional<SioxImage> extractForeground(SioxImage const &originalImage, uint32_t backgroundFillColor);

private:
    SioxObserver *observer;

    /**
     * Progress reporting
     */
    bool progressReport(float percentCompleted);

    int width;       ///< Width of the image
    int height;      ///< Height of the image
    int pixelCount;  ///< Number of pixels in the image
    uint32_t *image; ///< Working image data
    float *cm;       ///< Working image confidence matrix

    /**
     * Markup for image editing
     */
    int *labelField;

    /**
     * Our signature limits
     */
    float limits[3];

    /**
     * Maximum distance of two lab values.
     */
    float clusterSize;

    /**
     * Initialize the Siox engine to its 'pristine' state.
     * Performed at the beginning of extractForeground().
     */
    void init();

    /**
     * Error logging
     */
    void error(std::string const &str);

    /**
     * Trace logging
     */
    void trace(std::string const &str);

    /**
     * Stage 1 of the color signature work. 'dims' will be either 2 for grays, or 3 for colors.
     */
    void colorSignatureStage1(CieLab *points,
                              unsigned leftBase,
                              unsigned rightBase,
                              unsigned recursionDepth,
                              unsigned *clusters,
                              unsigned dims);

    /**
     * Stage 2 of the color signature work
     */
    void colorSignatureStage2(CieLab *points,
                              unsigned leftBase,
                              unsigned rightBase,
                              unsigned recursionDepth,
                              unsigned *clusters,
                              float    threshold,
                              unsigned dims);

    /**
     * Main color signature method
     */
    void colorSignature(std::vector<CieLab> const &inputVec,
                        std::vector<CieLab> &result,
                        unsigned dims);

    void keepOnlyLargeComponents(float threshold, double sizeFactorToKeep);

    int depthFirstSearch(int startPos, float threshold, int curLabel);

    void fillColorRegions();
};

} // namespace Trace
} // namespace Inkscape

#endif // INKSCAPE_TRACE_SIOX_H
