#pragma once

#include <cstdint>
#include <vector>
#include <dxgiformat.h>

namespace opendriver::driver::video::mfcolor {

void ConvertPackedRgbToNV12(const uint8_t* source,
                            uint32_t source_stride,
                            uint32_t width,
                            uint32_t height,
                            DXGI_FORMAT format,
                            std::vector<uint8_t>& output);

} // namespace opendriver::driver::video::mfcolor

