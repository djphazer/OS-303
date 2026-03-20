Import("env")

from subprocess import call

app = env.subst(".pio/build/app/firmware.hex")
boot = env.subst(".pio/build/bootloader/firmware.hex")
out = env.subst("${PROGNAME}.hex")

call(["srec_cat", app, "-Intel", boot, "-Intel", "-o", out, "-Intel"])
