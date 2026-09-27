set root [file normalize [file join [file dirname [info script]] ..]]
set ip_repo [file normalize [file join $root hls tile_brightness_hls sol1 impl ip]]
open_project [file join $root vivado kv260_tile kv260_tile.xpr]
set_property ip_repo_paths $ip_repo [current_project]
update_ip_catalog -rebuild
set kernel [get_ips -filter {NAME =~ *tile_brightness*}]
puts "INFO: upgrading $kernel"
upgrade_ip $kernel

open_bd_design [get_files design_1.bd]
set ps [get_bd_cells zynq_ultra_ps_e_0]
set_property CONFIG.PSU__DDR_HIGH_ADDRESS_GUI_ENABLE {1} $ps
set kernel_axi [get_bd_intf_pins tile_brightness_0/m_axi_gmem]
set mem_si [get_bd_intf_pins smartconnect_mem/S00_AXI]
if {[llength [get_bd_intf_nets -quiet -of_objects $kernel_axi]] == 0} {
    puts "INFO: reconnecting tile_brightness_0/m_axi_gmem to DDR"
    connect_bd_intf_net $kernel_axi $mem_si
}
assign_bd_address
set high_slave [get_bd_addr_segs -quiet zynq_ultra_ps_e_0/SAXIGP2/HP0_DDR_HIGH]
if {$high_slave ne ""} {
    assign_bd_address $high_slave -target_address_space [get_bd_addr_spaces tile_brightness_0/Data_m_axi_gmem]
}
set segs [get_bd_addr_segs -of_objects [get_bd_addr_spaces tile_brightness_0/Data_m_axi_gmem]]
foreach seg $segs {
    set offset [get_property OFFSET $seg]
    set range [get_property RANGE $seg]
    puts "INFO: kernel DDR segment $seg offset=$offset range=$range"
    if {[string match *DDR_HIGH* $seg]} {
        set_property offset 0x800000000 $seg
        set_property range 2G $seg
        puts "INFO: high DDR mapped at 0x800000000 size 2G"
    }
}
validate_bd_design
save_bd_design
reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
set progress [get_property PROGRESS [get_runs impl_1]]
set status [get_property STATUS [get_runs impl_1]]
puts "INFO: impl status=$status progress=$progress"
if {$progress ne "100%"} {
    exit 1
}
exit 0
