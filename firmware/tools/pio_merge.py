# PlatformIO post-build step: writes out/PortWatch-<env>.bin (whole flash image, for the
# browser flasher at address 0x0) and out/PortWatch-<env>.ota.bin (app only, for the
# device's web update page), then rebuilds the flasher with whatever firmware is in out/.
Import("env")
import os, shutil, subprocess

def esptool_cmds(env):
    py = env.subst("$PYTHONEXE")
    yield [py, "-m", "esptool"]
    up = env.subst("$UPLOADER")
    if up and os.path.isfile(up):
        yield ([py, up] if up.endswith(".py") else [up])
    pkg = env.PioPlatform().get_package_dir("tool-esptoolpy")
    if pkg and os.path.isfile(os.path.join(pkg, "esptool.py")):
        yield [py, os.path.join(pkg, "esptool.py")]

def merge(source, target, env):
    proj = env.subst("$PROJECT_DIR")
    out_dir = os.path.join(proj, "out")
    os.makedirs(out_dir, exist_ok=True)
    name = env.subst("$PIOENV")
    app = str(target[0])
    merged = os.path.join(out_dir, f"PortWatch-{name}.bin")
    images = []
    for addr, path in env.get("FLASH_EXTRA_IMAGES", []):
        images += [env.subst(addr), env.subst(path)]
    images += [env.subst("$ESP32_APP_OFFSET"), app]
    args = ["--chip", env.BoardConfig().get("build.mcu"), "merge_bin", "-o", merged,
            "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"] + images
    ok = False
    for base in esptool_cmds(env):
        try:
            if subprocess.call(base + args) == 0:
                ok = True
                break
        except OSError:
            pass
    if not ok:
        print("PortWatch: couldn't run esptool merge_bin; use 'pio run -e %s -t upload' instead" % name)
        return
    shutil.copyfile(app, os.path.join(out_dir, f"PortWatch-{name}.ota.bin"))
    print("PortWatch: wrote", merged)
    subprocess.call([env.subst("$PYTHONEXE"), os.path.join(proj, "tools", "build_flasher.py")])

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)
