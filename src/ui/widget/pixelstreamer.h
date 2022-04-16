// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A class hierarchy implementing various ways of streaming pixel buffers to the GPU.
 * Copyright (C) 2022 PBS <pbs3141@gmail.com>
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_PIXELSTREAMER_H
#define INKSCAPE_PIXELSTREAMER_H

#include <memory>
#include <cairomm/refptr.h>
#include <cairomm/surface.h>
#include "texture.h"

namespace Inkscape {

// A class for turning Cairo image surfaces into OpenGL textures.
class PixelStreamer
{
public:
    virtual ~PixelStreamer() {}

    // Method for streaming pixels to the GPU.
    enum class Method
    {
        Persistent,   // Persistent buffer mapping. (Best, requires OpenGL 4.4.)
        Asynchronous, // Ordinary buffer mapping. (Almost as good, requires OpenGL 3.0.)
        Synchronous,  // Synchronous texture uploads. (Worst but still tolerable, requires OpenGL 1.1.)
        Auto          // Use the best option available at runtime.
    };

    // Create a PixelStreamer using the given method.
    template <Method method>
    static std::unique_ptr<PixelStreamer> create();

    // Create a PixelStreamer using a choice of method specified at runtime.
    static std::unique_ptr<PixelStreamer> create(Method method);

    // Return the method in use.
    virtual Method get_method() const = 0;

    // Request a drawing surface of the given dimensions.
    virtual Cairo::RefPtr<Cairo::ImageSurface> request(const Geom::IntPoint &dimensions) = 0;

    // Give back the surface to turn it into a texture.
    virtual Texture finish(Cairo::RefPtr<Cairo::ImageSurface> surface) = 0;
};

} // namespace Inkscape

#endif // INKSCAPE_PIXELSTREAMER_H
