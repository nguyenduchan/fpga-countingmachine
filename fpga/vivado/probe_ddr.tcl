set root [file normalize [file join [file dirname [info script]] ..]]
open_project [file join $root vivado kv260_tile kv260_tile.xpr]
open_bd_design [get_files design_1.bd]
set ps [get_bd_cells zynq_ultra_ps_e_0]
puts "INFO: high-gui before [get_property CONFIG.PSU__DDR_HIGH_ADDRESS_GUI_ENABLE $ps]"
set_property CONFIG.PSU__DDR_HIGH_ADDRESS_GUI_ENABLE {1} $ps
puts "INFO: high-gui after [get_property CONFIG.PSU__DDR_HIGH_ADDRESS_GUI_ENABLE $ps]"
puts "INFO: hp segs [get_bd_addr_segs -quiet -of_objects [get_bd_intf_pins zynq_ultra_ps_e_0/S_AXI_HP0_FPD]]"
puts "INFO: saxigp segs [get_bd_addr_segs -quiet zynq_ultra_ps_e_0/SAXIGP2/*]"
exit 0
