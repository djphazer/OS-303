Import("env")

import subprocess
from os.path import join

def get_git_rev():
    git_rev = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"]).decode().strip()
    git_status = subprocess.check_output(["git", "status", "-s", "--untracked-files=no"]).decode()
    suffix = "dirty" if git_status else ""

    return git_rev + suffix

def after_build(source, target, env):
    git_rev = get_git_rev()
    env.Replace(PROGNAME=f"OS-303_v%s_{git_rev}" % env.GetProjectOption("custom_project_version"))

    app = env.subst(".pio/build/app/firmware.hex")
    boot = env.subst(".pio/build/bootloader/firmware.hex")
    out = env.subst("${PROGNAME}.hex")

    platform = env.PioPlatform()
    subprocess.call([join(platform.get_package_dir("tool-sreccat") or "", "srec_cat"), app, "-Intel", boot, "-Intel", "-o", out, "-Intel"])

env.AddPostAction("buildprog", after_build)
