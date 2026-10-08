# PlatformIO pre-build script: build metadata for src/BuildInfo.cpp only.
#
# - Git SHA: $DASHBOARD_GIT_SHA if set (deploy-esp32.sh passes it to the
#   Windows build, which is not a git checkout), else `git rev-parse`, else
#   "unknown". Git is never required.
# - Environment/board: the PlatformIO env and board names.
# The defines are applied to BuildInfo.cpp alone (other files keep their
# build signature) and that file is rebuilt every time so __DATE__/__TIME__
# stay current.
import os
import subprocess

Import("env")


def git_sha():
    value = os.environ.get("DASHBOARD_GIT_SHA", "").strip()
    if value:
        return value
    root = env.subst("$PROJECT_DIR")
    try:
        sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=root, capture_output=True,
                             text=True, timeout=5, check=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=root,
                               capture_output=True, text=True, timeout=5, check=True).stdout.strip()
        return sha + ("+dirty" if dirty else "") if sha else "unknown"
    except (OSError, subprocess.SubprocessError):
        return "unknown"


def quoted(value):
    safe = "".join(c for c in value if c.isalnum() or c in "+-._")
    return env.StringifyMacro(safe or "unknown")


BUILD_DEFINES = [
    ("DASHBOARD_GIT_SHA", quoted(git_sha())),
    ("DASHBOARD_BUILD_ENV", quoted(env.subst("$PIOENV"))),
    ("DASHBOARD_BUILD_BOARD", quoted(env.subst("$BOARD"))),
]


def build_info_object(env, node):
    obj = env.Object(node, CPPDEFINES=list(env["CPPDEFINES"]) + BUILD_DEFINES)
    env.AlwaysBuild(obj)
    return obj


env.AddBuildMiddleware(build_info_object, "*/BuildInfo.cpp")
