Import("env")

import subprocess

def after_build(source, target, env):
    # python tools/hex2sysex.py .pio/build/app/firmware.hex > update.syx
    with open("update.syx", "w") as outfile:
        subprocess.run(["python", "tools/hex2sysex.py", ".pio/build/app/firmware.hex"], stdout=outfile)

env.AddPostAction("buildprog", after_build)
