#include "driver/video/mf_color_convert.h"

#include <cassert>
#include <vector>

int main() {
    using namespace opendriver::driver::video::mfcolor;

    // 2x2 BGRA black frame -> NV12 should produce Y=16 and UV=128.
    const uint32_t width = 2;
    const uint32_t height = 2;
    const uint32_t stride = width * 4;
    const std::vector<uint8_t> bgra_black = {
        0, 0, 0, 255, 0, 0, 0, 255,
        0, 0, 0, 255, 0, 0, 0, 255
    };

    std::vector<uint8_t> nv12;
    ConvertPackedRgbToNV12(
        bgra_black.data(),
        stride,
        width,
        height,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        nv12);

    assert(nv12.size() == 6);
    assert(nv12[0] == 16);
    assert(nv12[1] == 16);
    assert(nv12[2] == 16);
    assert(nv12[3] == 16);
    assert(nv12[4] == 128);
    assert(nv12[5] == 128);
    return 0;
}
