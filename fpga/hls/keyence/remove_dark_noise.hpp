// Remove dark noise: closing. Expand, then shrink. Removes dark specks.

#ifndef KEYENCE_REMOVE_DARK_HPP
#define KEYENCE_REMOVE_DARK_HPP

#include "expand.hpp"
#include "shrink.hpp"

#ifndef __SYNTHESIS__
inline void apply_remove_dark_noise(const uint8_t* image, uint8_t* out, uint8_t* scratch,
                                    int width, int height) {
    apply_expand(image, scratch, width, height);
    apply_shrink(scratch, out, width, height);
}
#endif

#endif
