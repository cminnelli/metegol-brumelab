Import("env")
import subprocess

def get_version():
    try:
        v = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        v = "unknown"
    return v

env.Append(CPPDEFINES=[("FW_VERSION", '\\"{}\\"'.format(get_version()))])
