"""Output -I flags for Arduino ESP32 3.x framework libraries.

Called by platformio.ini via the ! dynamic build_flags syntax.
This is NOT a PlatformIO extra_script — it runs as a plain subprocess
and prints compiler flags to stdout.
"""
import glob
import os
import sys

core_dir = os.environ.get(
    "PLATFORMIO_CORE_DIR", os.path.expanduser("~/.platformio")
)
packages_dir = os.path.join(core_dir, "packages")

libs_dir = None
candidates = sorted(
    glob.glob(os.path.join(packages_dir, "framework-arduinoespressif32*", "libraries")),
    reverse=True,
)
if candidates:
    libs_dir = candidates[0]

if not libs_dir or not os.path.isdir(libs_dir):
    print("", end="")
    print("[get_network_flags] WARNING: framework libraries dir not found", file=sys.stderr)
    print("[get_network_flags]   searched: %s/framework-arduinoespressif32*/libraries" % packages_dir, file=sys.stderr)
else:
    print("[get_network_flags] Found: %s" % libs_dir, file=sys.stderr)

flags = []
if libs_dir:
    for lib in ["Network", "WiFi", "HTTPClient", "NetworkClientSecure"]:
        src = os.path.join(libs_dir, lib, "src")
        if os.path.isdir(src):
            flags.append("-I" + src)
            print("[get_network_flags]   +%s" % lib, file=sys.stderr)

print(" ".join(flags))
