# Synthesize tile_brightness.cpp for the Kria KV260 (xck26) at 100 MHz.
open_project tile_brightness_hls
set_top tile_brightness
add_files tile_brightness.cpp
open_solution sol1 -flow_target vivado
set_part {xck26-sfvc784-2LV-c}
create_clock -period 10 -name default
csynth_design
export_design -format ip_catalog -output tile_brightness.zip
exit
