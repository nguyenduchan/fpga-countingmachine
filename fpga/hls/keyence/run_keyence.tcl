# One Vitis HLS project per Keyence top. Run from fpga/hls/keyence.
# Each top is a separate IP. Arithmetic lives in the matching header.

set tops {
    keyence_binary binary.cpp
    keyence_contrast contrast_conversion.cpp
    keyence_expand expand.cpp
    keyence_shrink shrink.cpp
    keyence_remove_dark_noise remove_dark_noise.cpp
    keyence_remove_bright_noise remove_bright_noise.cpp
    keyence_median median.cpp
    keyence_average average.cpp
    keyence_blur blur.cpp
    keyence_shading shading.cpp
    keyence_preserve_intensity preserve_intensity.cpp
    keyence_contrast_expansion contrast_expansion.cpp
    keyence_subtraction subtraction.cpp
    keyence_image_extraction image_extraction.cpp
    keyence_sobel_x sobel_x.cpp
    keyence_sobel_y sobel_y.cpp
    keyence_sobel sobel.cpp
    keyence_prewitt prewitt.cpp
    keyence_roberts roberts.cpp
    keyence_laplacian laplacian.cpp
    keyence_sharpen sharpen.cpp
    keyence_scratch_defect scratch_defect.cpp
    keyence_noise_isolation noise_isolation.cpp
    keyence_blob blob.cpp
    keyence_lumitrax lumitrax.cpp
}

foreach {top source} $tops {
    open_project ${top}_prj
    set_top $top
    add_files $source
    if {$top eq "keyence_shading"} {
        add_files ../tile_brightness_core.hpp
    }
    open_solution sol1 -flow_target vivado
    set_part {xck26-sfvc784-2LV-c}
    create_clock -period 10 -name default
    csynth_design
    close_project
}

exit
