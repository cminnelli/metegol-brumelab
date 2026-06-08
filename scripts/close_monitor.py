Import("env")
import subprocess

def close_monitor(source, target, env):
    subprocess.run(["pkill", "-f", "platformio device monitor"], capture_output=True)

env.AddPreAction("upload", close_monitor)
