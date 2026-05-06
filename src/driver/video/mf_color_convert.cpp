#include "mf_color_convert.h"

#include <algorithm>

namespace opendriver::driver::video::mfcolor {

namespace {

uint8_t ClampToByte(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

bool ReadRgbPixel(const uint8_t* pixel, DXGI_FORMAT format, int& r, int& g, int& b) {
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        b = pixel[0];
        g = pixel[1];
        r = pixel[2];
        return true;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        r = pixel[0];
        g = pixel[1];
        b = pixel[2];
        return true;
    default:
        return false;
    }
}

} // namespace

void ConvertPackedRgbToNV12(const uint8_t* source,
                            uint32_t source_stride,
                            uint32_t width,
                            uint32_t height,
                            DXGI_FORMAT format,
                            std::vector<uint8_t>& output) {
    const size_t y_plane_size = static_cast<size_t>(width) * height;
    const size_t uv_plane_size = y_plane_size / 2;
    output.resize(y_plane_size + uv_plane_size);

    uint8_t* y_plane = output.data();
    uint8_t* uv_plane = output.data() + y_plane_size;

    for (uint32_t y = 0; y < height; y += 2) {
        const uint8_t* row0 = source + (static_cast<size_t>(y) * source_stride);
        const uint8_t* row1 = source + (static_cast<size_t>(std::min(y + 1, height - 1)) * source_stride);

        for (uint32_t x = 0; x < width; x += 2) {
            int u_accumulator = 0;
            int v_accumulator = 0;

            for (uint32_t dy = 0; dy < 2; ++dy) {
                const uint8_t* row = (dy == 0) ? row0 : row1;
                const uint32_t sample_y = std::min(y + dy, height - 1);

                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint32_t sample_x = std::min(x + dx, width - 1);
                    const uint8_t* pixel = row + (static_cast<size_t>(sample_x) * 4);

                    int r = 0;
                    int g = 0;
                    int b = 0;
                    if (!ReadRgbPixel(pixel, format, r, g, b)) {
                        r = 255;
                        g = 0;
                        b = 255;
                    }

                    const int y_value = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
                    const int u_value = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                    const int v_value = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;

                    y_plane[(static_cast<size_t>(sample_y) * width) + sample_x] = ClampToByte(y_value);
                    u_accumulator += u_value;
                    v_accumulator += v_value;
                }
            }

            const size_t uv_index = (static_cast<size_t>(y / 2) * width) + x;
            uv_plane[uv_index] = ClampToByte(u_accumulator / 4);
            uv_plane[uv_index + 1] = ClampToByte(v_accumulator / 4);
        }
    }
}

} // namespace opendriver::driver::video::mfcolor

