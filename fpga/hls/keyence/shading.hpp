// Shading correction. Same arithmetic as fpga/hls/tile_brightness_core.hpp.

#ifndef KEYENCE_SHADING_HPP
#define KEYENCE_SHADING_HPP

#include "../tile_brightness_core.hpp"

#ifndef __SYNTHESIS__
inline void apply_shading(const uint8_t* image, uint8_t* out, int width, int height) {
    shade_image(image, out, width, height);
}
#endif

#endif
