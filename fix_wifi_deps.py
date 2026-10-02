# PlatformIO post-script — fix WiFi → Network framework library dependency.
#
# Arduino ESP32 3.x WiFi depends on Network but doesn't declare it.
# This post-script injects Network's include path into projenv (the actual
# project source compilation environment) AFTER PlatformIO finishes its
# library resolution — so our changes actually stick.
#
# Must be referenced as:  extra_scripts = post:fix_wifi_deps.py

Import("env", "projenv")
import glob
import os
import sys

# Try the standard PlatformIO API first
framework_dir = None
try:
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    print("[fix_wifi_deps] Framework dir (API): %s" % framework_dir, file=sys.stderr)
except Exception as e:
    print("[fix_wifi_deps] get_package_dir failed: %s" % e, file=sys.stderr)

# Fallback: search for versioned package directories
if not framework_dir or not os.path.isdir(os.path.join(framework_dir, "libraries")):
    core_dir = os.environ.get("PLATFORMIO_CORE_DIR", os.path.expanduser("~/.platformio"))
    candidates = sorted(
        glob.glob(os.path.join(core_dir, "packages", "framework-arduinoespressif32*", "libraries")),
        reverse=True,
    )
    if candidates:
        framework_dir = os.path.dirname(candidates[0])
        print("[fix_wifi_deps] Framework dir (glob fallback): %s" % framework_dir, file=sys.stderr)
    else:
        print("[fix_wifi_deps] ERROR: Cannot find framework-arduinoespressif32 libraries!", file=sys.stderr)
        print("[fix_wifi_deps]   Searched: %s/packages/framework-arduinoespressif32*/libraries" % core_dir, file=sys.stderr)

libs_dir = os.path.join(framework_dir, "libraries") if framework_dir else ""

added = 0
for name in ["Network", "WiFi", "HTTPClient", "NetworkClientSecure"]:
    p = os.path.join(libs_dir, name, "src")
    if os.path.isdir(p):
        projenv.Prepend(CPPPATH=[p])
        env.Prepend(CPPPATH=[p])
        print("[fix_wifi_deps] Prepended: %s" % p, file=sys.stderr)
        added += 1

if added == 0:
    print("[fix_wifi_deps] WARNING: No library paths were added!", file=sys.stderr)
