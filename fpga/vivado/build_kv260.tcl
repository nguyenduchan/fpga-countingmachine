# Build a KV260 PL bitstream that exposes the HLS tile_brightness kernel.
# Control AXI-Lite is mapped at 0xA0000000. The kernel reads and writes the
# grayscale frame in DDR through the PS HP port.

set root [file normalize [file join [file dirname [info script]] ..]]
set proj_dir [file normalize [file join $root vivado kv260_tile]]
set ip_repo [file normalize [file join $root hls tile_brightness_hls sol1 impl ip]]

if {![file exists $ip_repo/component.xml]} {
    puts "ERROR: HLS IP catalog not found at $ip_repo"
    exit 1
}

file mkdir [file dirname $proj_dir]
create_project kv260_tile $proj_dir -part xck26-sfvc784-2LV-c -force
set_property ip_repo_paths $ip_repo [current_project]
update_ip_catalog

create_bd_design design_1
set zynq_vlnv [lindex [get_ipdefs -filter {NAME == zynq_ultra_ps_e}] 0]
if {$zynq_vlnv eq ""} {
    puts "ERROR: zynq_ultra_ps_e IP is not installed"
    exit 1
}
puts "INFO: using $zynq_vlnv"
set ps [create_bd_cell -type ip -vlnv $zynq_vlnv zynq_ultra_ps_e_0]

# GP0 enables M_AXI_HPM0_FPD (control). GP2 enables S_AXI_HP0_FPD (DDR).
set ps_config [list \
    CONFIG.PSU__FPGA_PL0_ENABLE {1} \
    CONFIG.PSU__CRL_APB__PL0_REF_CTRL__FREQMHZ {100} \
    CONFIG.PSU__USE__M_AXI_GP0 {1} \
    CONFIG.PSU__USE__M_AXI_GP2 {0} \
    CONFIG.PSU__USE__S_AXI_GP2 {1} \
]
if {[catch {set_property -dict $ps_config $ps} err]} {
    puts "ERROR: PS configuration failed: $err"
    exit 1
}

set kernel_vlnv [lindex [get_ipdefs -filter {NAME == tile_brightness}] 0]
if {$kernel_vlnv eq ""} {
    puts "ERROR: tile_brightness IP not in catalog"
    exit 1
}
puts "INFO: using $kernel_vlnv"
set krnl [create_bd_cell -type ip -vlnv $kernel_vlnv tile_brightness_0]

set rst [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 proc_sys_reset_0]
set sc_ctrl [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 smartconnect_ctrl]
set_property CONFIG.NUM_SI {1} $sc_ctrl
set_property CONFIG.NUM_MI {1} $sc_ctrl
set sc_mem [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 smartconnect_mem]
set_property CONFIG.NUM_SI {1} $sc_mem
set_property CONFIG.NUM_MI {1} $sc_mem

connect_bd_net [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] \
    [get_bd_pins proc_sys_reset_0/slowest_sync_clk] \
    [get_bd_pins tile_brightness_0/ap_clk] \
    [get_bd_pins smartconnect_ctrl/aclk] \
    [get_bd_pins smartconnect_mem/aclk]
connect_bd_net [get_bd_pins zynq_ultra_ps_e_0/pl_resetn0] [get_bd_pins proc_sys_reset_0/ext_reset_in]
connect_bd_net [get_bd_pins proc_sys_reset_0/peripheral_aresetn] \
    [get_bd_pins tile_brightness_0/ap_rst_n] \
    [get_bd_pins smartconnect_ctrl/aresetn] \
    [get_bd_pins smartconnect_mem/aresetn]

connect_bd_intf_net [get_bd_intf_pins zynq_ultra_ps_e_0/M_AXI_HPM0_FPD] [get_bd_intf_pins smartconnect_ctrl/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins smartconnect_ctrl/M00_AXI] [get_bd_intf_pins tile_brightness_0/s_axi_control]
connect_bd_intf_net [get_bd_intf_pins tile_brightness_0/m_axi_gmem] [get_bd_intf_pins smartconnect_mem/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins smartconnect_mem/M00_AXI] [get_bd_intf_pins zynq_ultra_ps_e_0/S_AXI_HP0_FPD]

connect_bd_net [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] [get_bd_pins zynq_ultra_ps_e_0/maxihpm0_fpd_aclk]
connect_bd_net [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] [get_bd_pins zynq_ultra_ps_e_0/saxihp0_fpd_aclk]

assign_bd_address
set segs [get_bd_addr_segs -of_objects [get_bd_addr_spaces zynq_ultra_ps_e_0/Data]]
puts "INFO: address segments: $segs"
foreach seg $segs {
    if {[string match *tile_brightness* $seg]} {
        set_property offset 0xA0000000 $seg
        set_property range 64K $seg
        puts "INFO: placed $seg at 0xA0000000"
    }
}

validate_bd_design
save_bd_design
set wrapper [make_wrapper -files [get_files design_1.bd] -top]
add_files -norecurse $wrapper
set_property top design_1_wrapper [current_fileset]
update_compile_order -fileset sources_1

launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
set status [get_property STATUS [get_runs impl_1]]
set progress [get_property PROGRESS [get_runs impl_1]]
puts "INFO: impl status=$status progress=$progress"
if {$progress ne "100%"} {
    puts "ERROR: implementation did not finish"
    exit 1
}

set bit [glob $proj_dir/kv260_tile.runs/impl_1/*.bit]
file copy -force $bit [file join $root vivado tile_brightness.bit]
puts "INFO: bitstream $bit"
exit 0
