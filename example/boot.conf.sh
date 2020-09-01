#!/bin/sh

root_emmc="root=PARTUUID=529c3971-02 rootfstype=f2fs rootflags=fastboot rw rootwait"
root_sd="root=PARTUUID=be2d7b4c-02 rootfstype=f2fs rootflags=fastboot rw rootwait"
serial="console=ttyS0,115200 earlycon=ns16550a,mmio32,0x01c28000"
#  no_console_suspend initcall_debug
linux_con="console=tty1"
silent="quiet loglevel=1 panic=3"
verbose="loglevel=15 panic=15"
trace="trace_event=regulator:*,gpio:*,i2c:*,clk:* trace_buf_size=40M"
# drm.debug=0x2  ,regmap:*
systemd="systemd.restore_state=0"
#common="cma=256M video=HDMI-1-A:d"

kernel_stable=stable
kernel_dev=test

cat << EOF
#
# Boot configurations for PinePhone 1.2
#
# Some usefult bootargs flags:
#   - quiet loglevel=0
#   - console=tty1
#   - console=ttyS0,115200
#   - root=/dev/mmcblk2p2 rootfstype=f2fs rootflags=fastboot rootwait rw
#   - panic=3
#   - init=/bin/tablet-init
#   - initcall_debug
#   - earlycon=ns16550a,mmio32,0x01c28000 loglevel=15
#   - trace_event=regulator:*,clock:*,clk:*,gpio:* trace_buf_size=40M
#   - video=HDMI-A-1:d
#   - video=DSI-1:d
#   - fbcon=nodefer
#
# boot name is up to 36 chars

device_id = pp3 (PP 1.2a)

no          = 0
  name      = xnux.eu / dev kernel (SD)
  atf       = fw.bin
  dtb       = $kernel_dev/board.dtb
  linux     = $kernel_dev/Image
  bootargs  = init=/bin/tablet-init $linux_con $serial $root_sd $silent $common
  splash    = files/xnux.argb

no          = 1
  name      = xnux.eu / stable kernel (SD)
  atf       = fw.bin
  linux     = $kernel_stable/Image
  dtb       = $kernel_stable/board.dtb
  bootargs  = init=/bin/tablet-init $linux_con $serial $root_sd $verbose $common
  splash    = files/xnux.argb

no          = 2
  name      = Arch Linux / dev kernel (SD)
  atf       = fw.bin
  dtb       = $kernel_dev/board.dtb
  linux     = $kernel_dev/Image
  bootargs  = $linux_con $serial $root_sd $verbose $common $systemd
  splash    = files/arch.argb

EOF
