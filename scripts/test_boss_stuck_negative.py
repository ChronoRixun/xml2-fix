"""Compile the real passive fixtures with one recovery disabled at a time (no game).

All generated sources, projects, binaries and logs stay under build/boss-negative.
Run after the ordinary build; uses Idle priority and serial, non-reused MSBuild.
Use --adapters-only when only the production Mold adapter fixture has changed.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "build/boss-negative"
IDLE_HIDDEN = 0x40 | 0x08000000
MUTATIONS = {
    "blob_attack": ("blob_rules.hpp", "attack_step_limit=5.f", "attack_step_limit=1.e9f"),
    "dopple_reply": ("dopple_rules.hpp", "reply_watchdog=5.f", "reply_watchdog=1.e9f"),
    "dopple_rearm": ("dopple_rules.hpp", "s.rearm_at=v.now+rearm", "s.rearm_at=s.rearm_at"),
    "dopple_rewind": ("dopple_rules.hpp", "if(s.ticked&&now<s.last_tick)s={};", "if(false)s={};"),
    "avalanche_close": ("avalanche_rules.hpp", "s.close_wait.ready(v.now,false,5.f)", "s.close_wait.ready(v.now,false,1.e9f)"),
    "shadowking_xtreme": ("shadowking_rules.hpp", "s.xtreme_wait.ready(now,false,5.f)", "s.xtreme_wait.ready(now,false,1.e9f)"),
    "magneto_facing": ("magneto_rules.hpp", "g.face(.9f),5.f", "g.face(.9f),1.e9f"),
    "magneto_tile": ("magneto_rules.hpp", "now-started>10.f", "now-started>1.e9f"),
    "mold_core": ("mold_combat_rules.hpp", "now-s.step_started>5.f", "now-s.step_started>1.e9f"),
    "mold_travel_rearm": ("mold_combat_rules.hpp", "if(!arrived)s={};", "if(false)s={};"),
    "facing_travel_window": ("boss_wait_rules.hpp", "now-started>limit", "now-started>1.e9f"),
}
ADAPTER_MUTATIONS = {
    "mold_adapter_rewind": ("mold_controller.cpp", "if(t<boss.last_time)", "if(false)"),
    "mold_adapter_marker": ("mold_controller.cpp", "hero,output,.9f),5.f", "hero,output,.9f),1.e9f"),
    "mold_adapter_core": ("mold_combat_rules.hpp", "now-s.step_started>5.f", "now-s.step_started>1.e9f"),
    "mold_adapter_rearm": ("mold_combat_rules.hpp", "if(!arrived)s={};", "if(false)s={};"),
}


def compiler_idle():
    while True:
        listing = subprocess.run(["powershell", "-NoProfile", "-Command",
                                  "Get-Process cl,link -ErrorAction SilentlyContinue | ForEach-Object { $_.ProcessName }; exit 0"], capture_output=True,
                                 text=True, check=True, creationflags=IDLE_HIDDEN)
        busy = [line for line in listing.stdout.splitlines() if line.strip().lower() in ("cl", "link")]
        if not busy:
            return
        print("Waiting for cl.exe/link.exe to finish", flush=True)
        time.sleep(10)


def run(command, log):
    compiler_idle()
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            creationflags=IDLE_HIDDEN)
    log.write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {command}; see {log}")


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    (ROOT / "temp").mkdir(exist_ok=True)
    os.environ.update(TEMP=str(ROOT / "temp"), TMP=str(ROOT / "temp"))
    outcomes = {}
    adapters_only = "--adapters-only" in sys.argv
    for name, (header, before, after) in ({} if adapters_only else MUTATIONS).items():
        directory = WORK / name
        source = directory / "source"
        source.mkdir(parents=True, exist_ok=True)
        for file in (ROOT / "src").glob("*_rules.hpp"):
            shutil.copyfile(file, source / file.name)
        text = (source / header).read_text(encoding="utf-8")
        assert text.count(before) == 1, (name, "mutation must hit exactly one recovery")
        (source / header).write_text(text.replace(before, after), encoding="utf-8")
        shutil.copyfile(ROOT / "test/boss_stuck_steps_test.cpp", source / "test.cpp")
        (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(boss_negative LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_executable(passive test.cpp)
target_include_directories(passive PRIVATE .)
''', encoding="utf-8")
        run(["cmake", "-S", str(source), "-B", str(directory / "out"),
             "-G", "Visual Studio 17 2022", "-A", "Win32"], directory / "configure.log")
        run(["cmake", "--build", str(directory / "out"), "--config", "Release",
             "--", "/m:1", "/nr:false"], directory / "build.log")
        result = subprocess.run([str(directory / "out/Release/passive.exe")],
                                capture_output=True, text=True, creationflags=IDLE_HIDDEN, timeout=30)
        (directory / "test.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        # An assertion failure (1) proves sensitivity; a crash is not an acceptable negative control.
        assert result.returncode == 1 and "failures" in result.stdout, (name, result.returncode)
        outcomes[name] = result.stdout.strip()
        print(name, outcomes[name], flush=True)
    for name, (file, before, after) in ADAPTER_MUTATIONS.items():
        directory = WORK / name
        source = directory / "source"
        (source / "src").mkdir(parents=True, exist_ok=True)
        (source / "test").mkdir(exist_ok=True)
        for header in (ROOT / "src").glob("*.hpp"):
            shutil.copyfile(header, source / "src" / header.name)
        shutil.copyfile(ROOT / "src/mold_controller.cpp", source / "src/mold_controller.cpp")
        target = source / "src" / file
        text = target.read_text(encoding="utf-8")
        assert text.count(before) == 1, (name, "mutation must hit exactly one recovery")
        target.write_text(text.replace(before, after), encoding="utf-8")
        shutil.copyfile(ROOT / "test/boss_mold_stuck_test.cpp", source / "test/adapter.cpp")
        (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(mold_negative LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_executable(passive test/adapter.cpp)
target_include_directories(passive PRIVATE src)
target_compile_definitions(passive PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_options(passive PRIVATE /BASE:0x400000 /DYNAMICBASE:NO /FIXED /MERGE:.text=.fixture /MERGE:.rdata=.fixture /MERGE:.data=.fixture /SECTION:.fixture,ERW)
''', encoding="utf-8")
        run(["cmake", "-S", str(source), "-B", str(directory / "out"),
             "-G", "Visual Studio 17 2022", "-A", "Win32"], directory / "configure.log")
        run(["cmake", "--build", str(directory / "out"), "--config", "Release",
             "--", "/m:1", "/nr:false"], directory / "build.log")
        result = subprocess.run([str(directory / "out/Release/passive.exe")], capture_output=True,
                                text=True, creationflags=IDLE_HIDDEN, timeout=30)
        (directory / "test.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        assert result.returncode == 1 and "failures" in result.stdout, (name, result.returncode)
        outcomes[name] = result.stdout.strip()
        print(name, outcomes[name], flush=True)
    result_name = "results-adapters.json" if adapters_only else "results.json"
    (WORK / result_name).write_text(json.dumps(outcomes, indent=2), encoding="utf-8")
    print(f"All {len(outcomes)} negative controls failed on assertions as required.")


if __name__ == "__main__":
    main()
