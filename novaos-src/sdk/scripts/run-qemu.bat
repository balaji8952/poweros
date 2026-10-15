@echo off
rem Starts NovaOS with disk.img attached.  Adjust the QEMU path if it is not on your PATH.
cd /d "%~dp0\.."
rem Add  -accel whpx  for much better speed if "Windows Hypervisor Platform" is enabled in Windows Features.
qemu-system-i386.exe -cdrom NovaOS.iso -hda disk.img -boot d -m 128 -vga std -rtc base=localtime
