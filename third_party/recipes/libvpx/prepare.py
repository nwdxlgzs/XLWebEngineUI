"""Generate portable VP8/VP9 decoder inputs using locked upstream scripts."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    source, work = args.source.resolve(), args.build.resolve()
    work.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        git = Path(shutil.which("git")).resolve().parent.parent
        shell = git / "bin/bash.exe"
        perl = git / "usr/bin/perl.exe"
        make = shutil.which("make")
        if not shell.is_file() or not perl.is_file() or not make:
            raise RuntimeError("Git for Windows bash/perl and GNU make are required for libvpx code generation")
        posix_source = "/" + source.drive[0].lower() + source.as_posix()[2:]
    else:
        shell, perl, make = "sh", "perl", "make"
        posix_source = source.as_posix()
    subprocess.run([str(shell), posix_source + "/configure", "--target=generic-gnu", "--disable-examples",
                    "--disable-tools", "--disable-docs", "--disable-unit-tests", "--disable-vp8-encoder",
                    "--disable-vp9-encoder", "--disable-runtime-cpu-detect", "--enable-pic", "--disable-webm-io",
                    "--disable-libyuv", "--enable-multithread", "--enable-vp9-highbitdepth"], cwd=str(work), check=True)
    if os.name == "nt":
        # The available make is Cygwin. It evaluates source lists only; Clang
        # through CMake performs every compilation and link operation.
        platform_make = work / "libs-generic-gnu.mk"
        text = platform_make.read_text().replace(posix_source, "/cygdrive" + posix_source)
        platform_make.write_bytes(text.encode("utf-8"))
    make_env = dict(os.environ)
    if os.name == "nt":
        make_env["PATH"] = str(Path(make).parent) + os.pathsep + make_env.get("PATH", "")
    subprocess.run([str(make), "target=libs", "libvpx_srcs.txt"], cwd=str(work), env=make_env, check=True)
    for name, definition in (("vpx_scale", "vpx_scale/vpx_scale_rtcd.pl"), ("vpx_dsp", "vpx_dsp/vpx_dsp_rtcd_defs.pl"),
                             ("vp8", "vp8/common/rtcd_defs.pl"), ("vp9", "vp9/common/vp9_rtcd_defs.pl")):
        with (work / (name + "_rtcd.h")).open("w", encoding="utf-8") as output:
            subprocess.run([str(perl), str(source / "build/make/rtcd.pl"), "--arch=generic", "--sym=" + name + "_rtcd",
                            "--config=" + str(work / "libs-generic-gnu.mk"), str(source / definition)],
                           stdout=output, cwd=str(work), check=True)
    subprocess.run([str(shell), posix_source + "/build/make/version.sh", posix_source,
                    (work / "vpx_version.h").as_posix()], cwd=str(work), check=True)
    if os.name == "nt":
        config = work / "vpx_config.h"
        text = config.read_text().replace("#define HAVE_PTHREAD_H 1", "#define HAVE_PTHREAD_H 0")
        text = text.replace("#define HAVE_UNISTD_H 1", "#define HAVE_UNISTD_H 0")
        text = text.replace("#define CONFIG_GCC 1", "#define CONFIG_GCC 0")
        text = text.replace("#define CONFIG_MSVS 0", "#define CONFIG_MSVS 1")
        config.write_text(text)
    paths = []
    for relative in (work / "libvpx_srcs.txt").read_text().splitlines():
        if relative.endswith(".c"):
            path = work / relative if relative == "vpx_config.c" else source / relative
            if not path.is_file():
                raise RuntimeError("upstream libvpx source list contains a missing file: " + relative)
            paths.append(path.as_posix())
    (work / "sources.cmake").write_text("set(XL_VPX_SOURCES\n" + "".join('  "' + p + '"\n' for p in paths) + ")\n")


if __name__ == "__main__":
    main()
