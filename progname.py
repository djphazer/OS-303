Import("env")

env.Replace(PROGNAME=f"OS-303_v%s" % env.GetProjectOption("project_version"))
