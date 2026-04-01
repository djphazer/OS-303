Import("env")
import os, sys

sys.path.insert(0, os.path.join(env["PROJECT_DIR"], "tools"))
import hex2sysex

syx_path = os.path.join(env["PROJECT_DIR"], "app-update.syx")

def make_syx(target, source, env):
    hex_path = str(source[0])
    out_path = str(target[0])
    print("makesyx: %s -> app-update.syx" % hex_path)
    with open(out_path, "wb") as f:
        hex2sysex.process(hex_path, f)
    print("makesyx: wrote %d bytes" % os.path.getsize(out_path))

# Command node: always rebuild the .syx regardless of whether the .hex changed.
# AlwaysBuild marks it unconditionally out-of-date every run.
# Default adds it to the targets built by plain `pio run`.
syx = env.Command(syx_path, "$BUILD_DIR/${PROGNAME}.hex", make_syx)
env.AlwaysBuild(syx)
env.Default(syx)
