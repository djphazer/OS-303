Import("env")

import subprocess

def get_git_rev():
    git_rev = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"]).decode().strip()
    git_status = subprocess.check_output(["git", "status", "-s", "--untracked-files=no"]).decode()
    suffix = "dirty" if git_status else ""

    return git_rev + suffix

git_rev = get_git_rev()

env.Replace(PROGNAME=f"OS-303_v%s_{git_rev}" % env.GetProjectOption("project_version"))
