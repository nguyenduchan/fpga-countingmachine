// Remove bright noise: opening. Shrink, then expand. Removes bright specks.

#ifndef KEYENCE_REMOVE_BRIGHT_HPP
#define KEYENCE_REMOVE_BRIGHT_HPP

#include "expand.hpp"
#include "shrink.hpp"

#ifndef __SYNTHESIS__
inline void apply_remove_bright_noise(const uint8_t* image, uint8_t* out, uint8_t* scratch,
                                      int width, int height) {
    apply_shrink(image, scratch, width, height);
    apply_expand(scratch, out, width, height);
}
#endif

#endif
