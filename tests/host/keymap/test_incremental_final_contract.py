#!/usr/bin/env python3
"""Baseline parity and dependency contract for the final 27 host migrations."""

import json
import re
import subprocess
import tempfile
from pathlib import Path

from test_incremental_batch_contract import Model, expand_vars


HERE = Path(__file__).resolve().parent
FIXTURE = HERE / "incremental_final_baseline.json"
MAKEFILE = HERE / "Makefile"


def check() -> None:
    baseline = json.loads(FIXTURE.read_text())
    makefile = MAKEFILE.read_text()
    model = Model(makefile)
    bins = model.expanded_var("BINS").split()
    depfiles = model.expanded_var("MAKE_DEPFILES").split()
    clean = model.rule("clean")
    aggregate = model.rule("test")
    assert clean and aggregate
    assert len(bins) == len(set(bins)) == 83
    assert "test_music_manager_bin" in bins
    music_controller = "test_music_command_controller"
    music_controller_bin = music_controller + "_bin"
    assert music_controller_bin in bins
    assert music_controller in model.phony()
    controller_wrapper = model.rule_resolved(music_controller)
    assert controller_wrapper and music_controller_bin in expand_vars(
        controller_wrapper["prereqs_raw"], model
    ).split()
    assert any(music_controller in line for line in model.rule("test")["recipe"])
    lvgl_dispatch = "test_lvgl_dispatch_state"
    lvgl_dispatch_bin = lvgl_dispatch + "_bin"
    assert lvgl_dispatch_bin in bins
    assert lvgl_dispatch in model.phony()
    dispatch_wrapper = model.rule_resolved(lvgl_dispatch)
    assert dispatch_wrapper and lvgl_dispatch_bin in expand_vars(
        dispatch_wrapper["prereqs_raw"], model
    ).split()
    assert any(lvgl_dispatch in line for line in model.rule("test")["recipe"])
    assert any("$(BINS)" in line for line in model.rule("clean")["recipe"])
    assert len(baseline) == 27

    clean_dirs = set()
    for command in clean["recipe"]:
        tokens = command.split()
        if tokens[:2] == ["rm", "-rf"]:
            clean_dirs.update(tokens[2:])
    aggregate_commands = [line.strip().lstrip("@") for line in aggregate["recipe"]]

    for name, expected in baseline.items():
        artifact = name + "_bin"
        objects_var = name.removeprefix("test_").upper() + "_MIG_OBJS"
        objects = model.expanded_var(objects_var).split()
        sources = expected["sources"]
        assert len(objects) == len(sources), name
        assert len(set(objects)) == len(objects), name
        assert objects[0] == f"{name}/test.o", name
        assert all(obj.startswith(f"{name}/") for obj in objects), name
        assert artifact in bins and name in clean_dirs, name
        assert (sum(expand_vars(line, model) == f"./{artifact}"
                    for line in aggregate_commands) +
                sum(line.startswith("$(MAKE)") and line.split()[-1] == name
                    for line in aggregate_commands)) == 1, name
        assert not any(line == f"./{name}" for line in aggregate_commands), name
        assert name in model.phony(), name

        for obj, source in zip(objects, sources):
            rule = model.rule(obj)
            assert rule, obj
            prerequisites = expand_vars(rule["prereqs_raw"], model).split()
            assert prerequisites[0] == source and "Makefile" in prerequisites, obj
            assert f"{obj}.d" in depfiles, obj
            assert (HERE / f"{obj}.d").is_file(), f"missing emitted depfile: {obj}"
            recipes = [line.strip().split() for line in rule["recipe"] if "$(CXX)" in line]
            assert len(recipes) == 1, obj
            command = recipes[0]
            assert command[:1 + len(expected["flags"])] == ["$(CXX)", *expected["flags"]], obj
            assert "-MMD -MP -MF $@.d -c $< -o $@" in " ".join(command), obj

        link = model.rule_resolved(artifact)
        assert link, artifact
        assert f"$({objects_var})" in link["prereqs_raw"] and "Makefile" in link["prereqs_raw"], name
        links = [line.strip().split() for line in link["recipe"] if "$(CXX)" in line]
        assert len(links) == 1, name
        command = links[0]
        assert command == ["$(CXX)", *expected["flags"], f"$({objects_var})",
                           *expected["link_flags"], "-o", "$@"], name

        wrapper = model.rule_resolved(name)
        assert wrapper, name
        assert artifact in expand_vars(wrapper["prereqs_raw"], model).split(), name
        invocations = [line.strip().lstrip("@") for line in wrapper["recipe"]]
        if name == "test_local_shell":
            # The wrapper also owns the ESP opendir integration contract; keep
            # both checks ordered and exact instead of treating the extra
            # required scenario as an unexpected duplicate binary invocation.
            assert len(invocations) == 2, name
            assert expand_vars(invocations[0], model) == f"./{artifact}", name
            assert expand_vars(invocations[1], model) == "python3 test_music_scan_opendir_contract.py", name
        else:
            assert len(invocations) == 1, name
            assert expand_vars(invocations[0], model) == f"./{artifact}", name

        production_depfile = (HERE / f"{objects[1]}.d").read_text()
        first_rule = production_depfile.split("\n\n", 1)[0]
        headers = re.findall(r"\S+\.h\b", first_rule)
        headers = [header for header in headers if (HERE / header).is_file()]
        assert headers, f"{name}: production depfile has no header"
        header = headers[0]
        dry_run = subprocess.run(["make", "-s", "-n", "-W", header, artifact],
                                 cwd=HERE, check=True, capture_output=True, text=True).stdout
        consumers = [obj for obj in objects
                     if re.search(r"(?<!\S)" + re.escape(header) + r"(?!\S)",
                                  (HERE / f"{obj}.d").read_text().split("\n\n", 1)[0])]
        for obj in consumers:
            assert f"-o {obj}" in dry_run, f"{name}: header does not rebuild {obj}"
        for obj in set(objects) - set(consumers):
            assert f"-o {obj}" not in dry_run, f"{name}: header rebuilds unrelated {obj}"

    music_object = "test_music_app/music_app.o"
    assert music_object + ".d" in depfiles
    assert model.expanded_var("MUSIC_APP_OBJ").strip() == music_object
    assert (HERE / f"{music_object}.d").is_file(), "missing music app depfile"

    # Makefile edits invalidate all 176 TUs; a second no-op build does not.
    built = subprocess.run(["make", "-s", "-n", "test-build"], cwd=HERE,
                           check=True, capture_output=True, text=True).stdout
    assert not re.search(r"(?:g\+\+|clang\+\+) .* -c ", built), "second build recompiles"

    music_dir = Path("test_music_app")
    assert music_dir.name in clean_dirs
    with tempfile.TemporaryDirectory(prefix="incremental-clean-") as sandbox:
        sandbox_path = Path(sandbox)
        isolated_music_dir = sandbox_path / music_dir
        isolated_music_dir.mkdir()
        for artifact in ("music_app.o", "music_app.o.d", "music_app.gcda"):
            (isolated_music_dir / artifact).touch()
        music_bin = sandbox_path / "test_music_app_bin"
        unrelated_bin = sandbox_path / "test_terminal_filter_bin"
        music_bin.touch()
        unrelated_bin.touch()
        subprocess.run(["make", "-s", "-f", str(MAKEFILE), "-C", sandbox,
                        "clean"], check=True)
        assert not isolated_music_dir.exists(), "clean must remove music objects and coverage data"
        assert not music_bin.exists(), "clean must remove listed BINS"
        assert not unrelated_bin.exists(), "clean must remove all declared BINS in sandbox"


if __name__ == "__main__":
    check()
    print("PASS: final incremental migration (27 targets, 176 TUs, baseline parity)")
