// SPDX-License-Identifier: GPL-2.0-or-later
#include "texture.h"

#define USE_OPTIMAL_PATH !(__APPLE__)

namespace Inkscape {

Texture::Texture(const Geom::IntPoint &size)
    : size(size)
{
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    // Common flags for all textures used at the moment.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    #if USE_OPTIMAL_PATH
        // Use the optimal path, requiring either OpenGL 4.2 or GL_ARB_TEXTURE_STORAGE, which should be widely supported.
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, size.x(), size.y());
    #else
        // Mac users get the suboptimal path at the moment because Apple support neither of the above.
        // Todo: This only a temporary hack to allow testing on the Mac. In future, want to emulate
        // the above over Metal using a translation layer.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.x(), size.y(), 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    #endif
}

Texture &Texture::operator=(Texture &&other) noexcept
{
    this->~Texture();
    new (this) Texture(other.id, other.size);
    other.id = 0;
    return *this;
}

} // namespace Inkscape
