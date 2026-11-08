#!/usr/bin/env python3
"""Stack-budget regression for the image viewer (BUG-IMG-01/02, AC-IMG-04).

The physical failure was a `Stack protection fault` in task `image_load`
(6144 bytes) caused by a ~8 KiB `image_directory` page materialized on the task
stack — first as explicit locals (BUG-IMG-01) and then as a value-initialized
temporary inside `directory_workspace::acquire` (BUG-IMG-02).  A textual check
alone was not enough: the first fix passed this kind of review and still blew
the stack.

This contract therefore measures real frames:

1. `-fstack-usage` on the host-linkable production TU
   (`cyberdeck_image_list.cpp`): every function of the worker call chain has a
   small frame, and the chain fits the worker stack with a wide reserve.
2. Structural ELF validation: when the ESP-IDF build and the RISC-V toolchain
   are present, the frames of the image-application functions are read from
   `build/cyberdeck5.elf` (prologue `addi sp,sp,-N` plus any `lui`/`add sp,sp`
   adjustment) and each must fit `k_worker_stack_bytes`.  Without the ELF the
   step prints SKIP with the reason — it is a limitation, never a pass.
3. Supplementary textual checks (never sufficient on their own): production
   code does not value-initialize an `image_directory`, and the workspace zeroes
   raw storage instead of using placement-new with `{}`.

Everything derives its limit from `k_worker_stack_bytes` in
`cyberdeck_image_types.h`; no literal stack size is duplicated here.
"""

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TYPES_HEADER = ROOT / "components/cyberdeck/include/apps/image/cyberdeck_image_types.h"
LIST_SOURCE = ROOT / "components/cyberdeck/src/apps/image/cyberdeck_image_list.cpp"
LIST_HEADER = ROOT / "components/cyberdeck/include/apps/image/cyberdeck_image_list.h"
APP_SOURCE = ROOT / "components/cyberdeck/src/apps/image/cyberdeck_image_app.cpp"
ELF = ROOT / "build/cyberdeck5.elf"

# Reserve kept free inside the worker stack after the measured frames: the VFS
# (opendir/readdir/pread) and logging run *below* the checked functions, so the
# budget must leave room for them instead of merely fitting exactly.
CHAIN_RESERVE_BYTES = 2048

# Per-function ceiling for the pieces we measure directly.  A page copy or a
# value-initialized aggregate would be ~8272 bytes and fail immediately.
SINGLE_FRAME_CEILING = 1024

CHECKED_APP_SYMBOLS = (
    "cyberdeck_image::application::run_job",
    "cyberdeck_image::application::worker_entry",
    "cyberdeck_image::application::publish",
    "cyberdeck_image::application::fail",
    "cyberdeck_image::application::start",
    "cyberdeck_image::application::stop",
)


def stack_budget():
    source = TYPES_HEADER.read_text(encoding="utf-8")
    match = re.search(r"k_worker_stack_bytes\s*=\s*(\d+)\s*;", source)
    if match is None:
        raise SystemExit("k_worker_stack_bytes not found in cyberdeck_image_types.h")
    return int(match.group(1))


def measured_list_frames():
    """Compiles the production list TU with -fstack-usage and returns frames."""
    compiler = shutil.which("g++")
    if compiler is None:
        return None
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        command = [
            compiler, "-std=c++17", "-O2", "-fstack-usage", "-c", str(LIST_SOURCE),
            "-I" + str(ROOT / "components/cyberdeck/include"),
            "-o", str(work / "list.o"),
        ]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if result.returncode != 0:
            raise SystemExit(f"host compile failed: {result.stderr.strip()[:400]}")
        usage = work / "list.su"
        if not usage.exists():
            raise SystemExit("-fstack-usage produced no .su file")
        frames = {}
        for line in usage.read_text(encoding="utf-8").splitlines():
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            # `file:line:col:function<TAB>bytes<TAB>qualifier`; the function name
            # itself contains `::`, so split only the three location fields.
            location = parts[0].split(":", 3)
            if len(location) != 4:
                continue
            frames[location[3]] = int(parts[1])
        return frames


def find_installed_objdump():
    """Locates the RISC-V objdump of an installed ESP-IDF toolchain.

    The host suite runs without `export.sh`, so the ELF proof would otherwise be
    skipped on every developer machine that has the toolchain but not the
    environment.  Searching the managed tools directory keeps the check real
    while still printing SKIP when nothing is installed.
    """
    for candidate in sorted(Path.home().glob(".espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/"
                                             "riscv32-esp-elf-objdump")):
        if candidate.is_file():
            return str(candidate)
    return None


def elf_frames(budget):
    """Reads the frame of each checked symbol from the ESP-IDF ELF."""
    objdump = shutil.which("riscv32-esp-elf-objdump") or find_installed_objdump()
    if objdump is None or not ELF.exists():
        return None, "build/cyberdeck5.elf or riscv32-esp-elf-objdump not available"
    disassembly = subprocess.run([objdump, "-d", "-C", str(ELF)], capture_output=True, text=True)
    if disassembly.returncode != 0:
        return None, "objdump failed on build/cyberdeck5.elf"
    symbols = {}
    current = None
    pending_lui = {}
    for line in disassembly.stdout.splitlines():
        header = re.match(r"^[0-9a-f]+ <(?P<name>.+)>:$", line)
        if header is not None:
            current = header.group("name")
            symbols[current] = 0
            pending_lui.clear()
            continue
        if current is None:
            continue
        # Disassembly lines start at column 0 (`4003bbd8:\tcb010113\taddi...`).
        instruction = re.match(r"^[0-9a-f]+:\s+[0-9a-f]+\s+(?P<asm>.+)$", line)
        if instruction is None:
            continue
        asm = instruction.group("asm").split("#")[0].strip()
        base = re.match(r"^addi\s+sp,sp,(-?\d+)$", asm)
        if base is not None:
            # Only a negative adjustment allocates frame space; a positive one
            # restores sp (epilogue/mid-function shrink) and must not be added,
            # or the reported frame would be inflated by the epilogue.
            amount = int(base.group(1))
            if amount < 0:
                symbols[current] += -amount
            continue
        lui = re.match(r"^lui\s+(\w+),(0x[0-9a-f]+)$", asm)
        if lui is not None:
            pending_lui[lui.group(1)] = int(lui.group(2), 16)
            continue
        adjust = re.match(r"^add\s+sp,sp,(\w+)$", asm)
        if adjust is not None:
            value = pending_lui.pop(adjust.group(1), None)
            if value is not None and value >= 0x80000:
                symbols[current] += 0x100000000 - (value << 12)
    frames = {}
    for name, size in symbols.items():
        for checked in CHECKED_APP_SYMBOLS:
            # Lambda outlines share the enclosing name prefix; they are separate
            # functions and must not shadow the real frame measurement.
            if name.startswith(checked + "(") and "::{lambda" not in name:
                frames[checked] = max(frames.get(checked, 0), size)
    return frames, None


def main():
    failures = []
    budget = stack_budget()

    def require(condition, message):
        if not condition:
            failures.append(message)

    # 1. Host-measured frames of the production list TU.
    frames = measured_list_frames()
    if frames is None:
        print("SKIP: g++ not available for -fstack-usage")
    else:
        # `-fstack-usage` reports `return-type namespace::function(args)`, so the
        # match is on the qualified name plus the opening parenthesis.  `offer`
        # may be inlined away; then it has no frame of its own.
        def lookup(name):
            return next((candidate for candidate in frames if name + "(" in candidate), None)

        for name in ("cyberdeck_image::list_images", "cyberdeck_image::directory_workspace::acquire",
                     "cyberdeck_image::directory_workspace::release", "cyberdeck_image::find_image"):
            key = lookup(name)
            require(key is not None, f"{name} not measured by -fstack-usage")
            if key is not None:
                require(frames[key] <= SINGLE_FRAME_CEILING,
                        f"{name} frame {frames[key]} B exceeds {SINGLE_FRAME_CEILING} B")
        inlined_offer = lookup("cyberdeck_image::offer") is None
        chain = sum(frames[key] for key in (lookup("cyberdeck_image::list_images"),
                                             lookup("cyberdeck_image::directory_workspace::acquire"),
                                             lookup("cyberdeck_image::offer")) if key is not None)
        require(chain + CHAIN_RESERVE_BYTES <= budget,
                f"list_images call chain {chain} B + {CHAIN_RESERVE_BYTES} B reserve exceeds {budget} B")
        print(f"host -fstack-usage: chain={chain} B ({'offer inlined' if inlined_offer else 'offer out-of-line'}), "
              f"budget={budget} B")

    # 2. ELF-measured frames of the application functions.
    #
    # The ELF is the only authoritative measurement of the target frame, but it
    # is only valid for the sources that were compiled.  A source edit without a
    # rebuild would silently measure the previous binary and pass — exactly the
    # false confidence that let BUG-IMG-02 through textual review.  Fail closed
    # instead of trusting a stale build.
    elf_dependencies = (APP_SOURCE, LIST_SOURCE, LIST_HEADER, TYPES_HEADER)
    stale = ([path.name for path in elf_dependencies if path.stat().st_mtime > ELF.stat().st_mtime]
             if ELF.exists() else [])
    if stale:
        failures.append(
            "stale build/cyberdeck5.elf: newer sources " + ", ".join(sorted(stale)) +
            "; run `idf.py build` before trusting the ELF frame measurements")
    app_frames, reason = elf_frames(budget)
    if app_frames is None:
        print(f"SKIP: ELF frame validation ({reason})")
    else:
        for symbol in CHECKED_APP_SYMBOLS:
            frame = app_frames.get(symbol)
            require(frame is not None, f"{symbol} missing from the ELF disassembly")
            if frame is not None:
                require(frame + CHAIN_RESERVE_BYTES <= budget,
                        f"{symbol} frame {frame} B + {CHAIN_RESERVE_BYTES} B reserve exceeds {budget} B")
        print("elf frames: " + ", ".join(f"{name.split('::')[-1]}={size}B"
                                         for name, size in sorted(app_frames.items())))

    # 3. Supplementary textual guards.
    for source in (APP_SOURCE, LIST_SOURCE):
        text = source.read_text(encoding="utf-8")
        code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        code = re.sub(r"//[^\n]*", "", code)
        require("image_directory{}" not in code,
                f"{source.name}: value-initializing an image_directory page would rebuild it on the stack")
        # A named local (`image_directory listing{}`) does not contain the
        # substring above but reserves the same ~8 KiB on the caller stack.
        named_local = re.search(r"\bimage_directory\s+\w+\s*\{", code)
        require(named_local is None,
                f"{source.name}: a named image_directory local "
                f"{(named_local.group(0) if named_local else '')!r} would rebuild the page on the stack")
        # Placement *default*-init (`new (memory) image_directory;`) is the
        # required form: it starts the object lifetime.  Only the brace form,
        # which value-initializes a temporary, is forbidden.
        require("image_directory{" not in code,
                f"{source.name}: no brace form of image_directory is allowed (value-init temporary)")
    list_code = re.sub(r"/\*.*?\*/", "", LIST_SOURCE.read_text(encoding="utf-8"), flags=re.S)
    list_code = re.sub(r"//[^\n]*", "", list_code)
    require("new (memory) image_directory;" in list_code,
            "directory_workspace must start the page lifetime with placement default-init")
    require("page_->~image_directory();" in list_code,
            "directory_workspace::release must end the page lifetime explicitly")
    require("std::memset(memory, 0, sizeof(image_directory))" not in list_code,
            "zeroing raw storage does not start a C++ lifetime; use placement default-init")
    header = LIST_HEADER.read_text(encoding="utf-8")
    require("void clear() noexcept" in header,
            "image_directory must expose the in-place clear() reset")
    require("is_trivially_destructible<image_directory>" in list_code,
            "the explicit destructor call must stay justified by a trivial-destructor assertion")
    require("is_trivially_copyable<image_directory>" in list_code,
            "directory_workspace must keep the trivially-copyable static assertion")

    for failure in failures:
        print(f"FAIL: {failure}")
    print(f"test_image_stack_frame_contract: {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
