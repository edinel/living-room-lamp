# PlatformIO pre-build hook: stamp the firmware with the git commit it was
# built from, so the web page can prove which exact code is running —
# especially useful for confirming an OTA push actually landed.
Import("env")

import subprocess


def git(*args):
    return subprocess.check_output(["git", *args], stderr=subprocess.DEVNULL).strip().decode()


try:
    build_id = git("rev-parse", "--short", "HEAD")
    try:
        git("diff", "--quiet", "--exit-code")
    except subprocess.CalledProcessError:
        build_id += "-dirty"
except Exception:
    build_id = "unknown"

env.Append(CPPDEFINES=[("BUILD_ID", '\\"%s\\"' % build_id)])
print("build_id.py: BUILD_ID=%s" % build_id)
