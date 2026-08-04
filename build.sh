rm -rv build/
pio run -t clean && pio run
cp .pio/build/bootloader/firmware.hex build/bootloader.hex
