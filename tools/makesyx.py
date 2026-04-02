Import("env")
import os, sys, subprocess

sys.path.insert(0, os.path.join(env["PROJECT_DIR"], "tools"))
import hex2sysex

def get_git_rev():
    git_rev = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"]).decode().strip()
    git_status = subprocess.check_output(["git", "status", "-s", "--untracked-files=no"]).decode()
    suffix = "dirty" if git_status else ""

    return git_rev + suffix

def make_syx(target, source, env):
    hex_path = str(source[0])
    out_path = str(target[0])
    print(f"makesyx: {hex_path} -> {out_path}")
    with open(out_path, "wb") as f:
        hex2sysex.process(hex_path, f)
    print("makesyx: wrote %d bytes" % os.path.getsize(out_path))

git_rev = get_git_rev()
progname = f"OS-303_v%s_{git_rev}" % env.GetProjectOption("custom_project_version")
syx_path = os.path.join(env["PROJECT_DIR"], f"{progname}.syx")

# Command node: always rebuild the .syx regardless of whether the .hex changed.
# AlwaysBuild marks it unconditionally out-of-date every run.
# Default adds it to the targets built by plain `pio run`.
syx = env.Command(syx_path, "$BUILD_DIR/${PROGNAME}.hex", make_syx)
env.AlwaysBuild(syx)
env.Default(syx)
