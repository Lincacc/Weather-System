#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

struct LoadedImage {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<unsigned char> pixels;
};

static LoadedImage loadImage(const char* path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path, &width, &height, &channels, 0);
    if (data == nullptr) {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(std::string(path) + ": " + (reason != nullptr ? reason : "load failed"));
    }
    LoadedImage image;
    image.width = width;
    image.height = height;
    image.channels = channels;
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels);
    image.pixels.assign(data, data + count);
    stbi_image_free(data);
    return image;
}

static void requireSize(const char* path, const LoadedImage& image, int width, int height, int channels) {
    if (image.width != width || image.height != height || image.channels != channels) {
        throw std::runtime_error(
            std::string(path) + " is " + std::to_string(image.width) + "x" + std::to_string(image.height)
            + " x" + std::to_string(image.channels)
            + ", expected " + std::to_string(width) + "x" + std::to_string(height)
            + " x" + std::to_string(channels));
    }
}

static std::vector<unsigned char> rgFromPrintedImage(const char* path, const LoadedImage& image) {
    std::vector<unsigned char> rg(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 2);
    for (int y = 0; y < image.height; ++y) {
        const int srcY = image.height - 1 - y;
        const unsigned char* src = image.pixels.data()
            + static_cast<std::size_t>(srcY) * static_cast<std::size_t>(image.width) * 3;
        unsigned char* dst = rg.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 2;
        for (int x = 0; x < image.width; ++x) {
            if (src[x * 3 + 2] != 0) {
                throw std::runtime_error(std::string(path) + " blue channel must be 0");
            }
            dst[x * 2] = src[x * 3];
            dst[x * 2 + 1] = src[x * 3 + 1];
        }
    }
    return rg;
}

static void setLinearFilter(GLuint tex) {
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

static GLuint makeRgTexture(const char* path, int width, int height, GLint wrap) {
    const LoadedImage image = loadImage(path);
    requireSize(path, image, width, height, 3);
    const std::vector<unsigned char> rg = rgFromPrintedImage(path, image);
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    glTextureStorage2D(tex, 1, GL_RG8, width, height);
    glTextureSubImage2D(tex, 0, 0, 0, width, height, GL_RG, GL_UNSIGNED_BYTE, rg.data());
    setLinearFilter(tex);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, wrap);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, wrap);
    return tex;
}

static GLuint makeVolumeTexture(const char* path, int n) {
    const LoadedImage image = loadImage(path);
    requireSize(path, image, n * n, n, 1);
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_3D, 1, &tex);
    glTextureStorage3D(tex, 1, GL_R8, n, n, n);
    glTextureSubImage3D(tex, 0, 0, 0, 0, n, n, n, GL_RED, GL_UNSIGNED_BYTE, image.pixels.data());
    setLinearFilter(tex);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_R, GL_REPEAT);
    return tex;
}