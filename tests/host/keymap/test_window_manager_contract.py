#!/usr/bin/env python3
"""Structural host contract for the Fase 8 window manager (TEST-WM-03/05/06/07).

Rastreabilidade (host, sem hardware):

  REQ/AC-8.3  barra de sistema persistente  -> TEST-WM-03  check_composed_root,
                                                check_ui_uses_composed_root
  REQ/AC-8.5  sem acesso direto das apps a
               arvore LVGL                    -> TEST-WM-05  check_lvgl_confined_to_adapter
  REQ/AC-8.6  API controlada de view/contexto -> TEST-WM-06  check_pure_policy,
                                                check_view_context_opaque
  REQ/AC-8.7  teardown, quiesciencia e
               invalidacao                    -> TEST-WM-07  check_boundary_guards,
                                                check_ui_teardown_before_deinit
  BUG-8-WM-BAR  system_bar com 42 px fixos, raiz em coluna flex, content abaixo
               da barra sem intersecao e teclado virtual em overlay
                                                -> TEST-REG-8-BAR  check_bar_row_layout
  TEST-REG-8-CHROME  system_bar chrome-only (fundo preto, border 0, padding 0,
               scroll desligado), 100% x 42 com flex_grow 0, header ocupando a
               area inteira sem deslocamento interno, content abaixo e padding
               externo preservado
                                                -> TEST-REG-8-CHROME  check_bar_chrome

O binario `test_window_manager` liga os TUs reais (politica pura + adaptador LVGL)
contra o shim host completo e cobre o comportamento.  Este contrato cobre o que
o binario nao alcanca: a pureza dos headers, a opacidade da `view_context`, a
confinacao do LVGL no adaptador, a ordem teardown -> deinit na UI e o wiring
CMake/Makefile/guard de cobertura.  Nenhum corpo de producao e copiado aqui.
"""

from pathlib import Path, PurePath
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
COMPONENT = ROOT / "components/cyberdeck"
POLICY_HEADER = COMPONENT / "include/apps/runtime/cyberdeck_window_manager.h"
POLICY_SOURCE = COMPONENT / "src/apps/runtime/cyberdeck_window_manager.cpp"
ADAPTER_HEADER = COMPONENT / "include/platform/display/cyberdeck_window_manager_adapter.h"
ADAPTER_SOURCE = COMPONENT / "src/platform/display/cyberdeck_window_manager_adapter.cpp"
UI_SOURCE = COMPONENT / "src/platform/display/cyberdeck_ui.cpp"
TERMINAL_VIEW_SOURCE = COMPONENT / "src/platform/display/cyberdeck_terminal_view.cpp"
HEADER_VIEW_SOURCE = COMPONENT / "src/platform/display/cyberdeck_header_view.cpp"
DEMO_SOURCE = COMPONENT / "src/apps/demo/cyberdeck_demo_app.cpp"
DEMO_HEADER = COMPONENT / "include/apps/demo/cyberdeck_demo_app.h"
CMAKE = COMPONENT / "CMakeLists.txt"
SCOPE_GUARD = ROOT / "tools/coverage_scope_guard.py"
CODEMAP = ROOT / "code-map.md"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
SHIM = ROOT / "tests/host/keymap/shim/full/lvgl.h"
BEHAVIORAL_TEST = ROOT / "tests/host/keymap/test_window_manager.cpp"

# The policy is the seam an application is allowed to depend on.  Any of these
# tokens would drag LVGL, ESP-IDF or FreeRTOS into the app-facing header.
FORBIDDEN_PURE_TOKENS = (
    "lvgl", "lv_", "esp_", "ESP_", "esp_err", "bsp_", "driver/", "nvs_",
    "freertos", "FreeRTOS", "xTask", "vTask", "xQueue", "vQueue",
    "xSemaphore", "portMUX", "SemaphoreHandle_t", "TaskHandle_t",
    "TickType_t", "printf", "malloc", "new ",
)

STANDARD_INCLUDES = {
    "algorithm", "array", "cstddef", "cstdint", "string", "string_view",
    "utility", "vector",
}

# The widget-tree ownership API.  Constructing, destroying or restyling an LVGL
# object is what "the app owns its own surface" means in practice, so no
# application TU may call it: apps receive a `view_context` instead.
TREE_OWNERSHIP_CALLS = (
    r"lv_obj_create\s*\(", r"lv_obj_delete\s*\(", r"lv_obj_del\s*\(",
    r"lv_label_create\s*\(", r"lv_textarea_create\s*\(",
    r"lv_keyboard_create\s*\(", r"lv_arc_create\s*\(",
    r"lv_obj_add_event_cb\s*\(", r"lv_obj_send_event\s*\(",
    r"lv_obj_add_state\s*\(", r"lv_obj_align\s*\(",
    r"lv_obj_update_layout\s*\(", r"lv_obj_set_[a-z_]+\s*\(",
    r"lv_label_set_text\s*\(", r"lv_scr_act\s*\(",
)

# Input injection is not tree ownership: the manual Serial-JTAG bridge replays
# ui.tap/ui.click by hit-testing an existing widget and synthesizing LV_EVENT_
# CLICKED.  That seam is read-only on the tree and must stay confined to the one
# authorized TU, so it is still forbidden everywhere else in the app layer.
BRIDGE_INPUT_INJECTION_CALLS = (r"lv_obj_send_event\s*\(",)
BRIDGE_INPUT_INJECTION_TU = "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"

# Reading LVGL is not owning it either.  Two app-layer seams are authorized to
# include the graphics API, each for a narrow, enumerated reason; every other
# application TU must stay free of it.  An application still never builds,
# destroys or restyles a widget.
LVGL_READER_TUS = {
    # ui.dump / ui.tap / ui.click / screen.dump over Serial-JTAG: hit-testing,
    # read-only introspection and synthesized input events on existing widgets.
    BRIDGE_INPUT_INJECTION_TU,
    # Marshals the cat worker result onto the LVGL thread with lv_async_call.
    "components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_before(body: str, first: str, second: str, message: str) -> None:
    require(first in body and second in body, message)
    require(body.index(first) < body.index(second), message)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def block_after(source: str, anchor: str) -> str:
    """Return the brace-balanced block that starts at `anchor`."""
    opening = source.index("{", source.index(anchor))
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated block for {anchor!r}")


def function_body(source: str, signature: str) -> str:
    return block_after(source, signature)


def includes(source: str) -> list[str]:
    """Return the include directives as spelled, keeping the <> or \"\" delimiter."""
    return re.findall(r'#\s*include\s*([<"][^>"]+[>"])', source)


def production_sources() -> list[Path]:
    suffixes = {".cpp", ".c", ".h", ".hpp"}
    return sorted(
        path for path in COMPONENT.rglob("*")
        if path.suffix in suffixes and "/build/" not in path.as_posix()
    )


def app_sources() -> list[Path]:
    roots = (COMPONENT / "src/apps", COMPONENT / "include/apps")
    suffixes = {".cpp", ".c", ".h", ".hpp"}
    return sorted(
        path for root in roots for path in root.rglob("*")
        if path.suffix in suffixes
    )


def makefile_target_lines(makefile: str, target: str) -> tuple[int, int]:
    """Return the first and last line index of a target's logical prerequisite line."""
    lines = makefile.splitlines()
    start = next((index for index, line in enumerate(lines)
                  if re.match(rf"^{re.escape(target)}:", line)), None)
    require(start is not None, f"Makefile must register the {target} target")
    last = start
    while last < len(lines) and lines[last].rstrip().endswith("\\"):
        last += 1
    return start, min(last, len(lines) - 1)


def makefile_target(makefile: str, target: str) -> str:
    """Return a Makefile target prerequisites as one line, continuations joined."""
    lines = makefile.splitlines()
    start, last = makefile_target_lines(makefile, target)
    collected = []
    for line in lines[start:last + 1]:
        piece = line.strip()
        collected.append(piece[:-1].strip() if piece.endswith("\\") else piece)
    return " ".join(collected)


def makefile_recipe(makefile: str, target: str) -> str:
    """Return every recipe line of a Makefile target, up to the next target."""
    lines = makefile.splitlines()
    _, last = makefile_target_lines(makefile, target)
    recipe = []
    for line in lines[last + 1:]:
        if re.match(r"^[A-Za-z_][A-Za-z_0-9.-]*:", line):
            break
        recipe.append(line)
    require(recipe, f"the {target} target must carry a recipe")
    return "\n".join(recipe)


def makefile_prerequisites(makefile: str, target: str) -> str:
    """Return the prerequisites of a target with simple ``NAME := value`` expanded."""
    logical = makefile_target(makefile, target)
    require(":" in logical, f"Makefile must declare prerequisites for {target}")
    prerequisites = logical.split(":", 1)[1]
    definitions = dict(re.findall(r"^(\w+)[ \t]*:?=[ \t]*(\S+)[ \t]*$",
                                  makefile, flags=re.M))
    for _ in range(4):  # bounded expansion: a cycle must not loop forever
        expanded = re.sub(
            r"\$\((\w+)\)",
            lambda found: definitions.get(found.group(1), found.group(0)),
            prerequisites,
        )
        if expanded == prerequisites:
            break
        prerequisites = expanded
    return prerequisites


def check_pure_policy(header: str, source: str) -> None:
    """TEST-WM-06: the app-facing policy depends on the standard library only."""
    combined = f"{header}\n{source}"
    for token in FORBIDDEN_PURE_TOKENS:
        require(token not in combined,
                f"the pure window manager must not depend on {token!r}")

    # Every translation unit of the policy is standard-library only; the only
    # quoted include is the policy's own header (bare or fully qualified), so the
    # seam cannot grow a platform dependency through a neighbour.
    own = POLICY_HEADER.name
    for unit, label in ((header, "header"), (source, "source")):
        for include in includes(unit):
            if include.startswith("<"):
                require(include[1:-1] in STANDARD_INCLUDES,
                        f"pure policy {label} must only include the standard "
                        f"library, found {include}")
            else:
                require(PurePath(include[1:-1]).name == own,
                        f"pure policy {label} must not include {include}")

    # Bounded registries and the invalid sentinel are the limit guards that the
    # structural layer can prove; the behavioral suite proves they hold.
    require("inline constexpr std::size_t k_max_surfaces = 8;" in header,
            "the surface registry bound must stay declared in the policy header")
    require("inline constexpr std::size_t k_max_notifications = 8;" in header,
            "the notification registry bound must stay declared in the policy header")
    require("std::array<surface, k_max_surfaces> surfaces_{};" in header,
            "surfaces must live in a fixed std::array bound by k_max_surfaces")
    require("std::array<notification, k_max_notifications> notifications_{};" in header,
            "notifications must live in a fixed std::array bound by k_max_notifications")
    require("std::size_t focused_slot_ = k_max_surfaces;" in header,
            "focus must default to the out-of-range sentinel, never to slot 0")
    require("std::uint64_t next_generation_ = 1;" in header,
            "the capability generation must start above zero, the empty value")

    # A policy that could grow at runtime would defeat every bound above.
    for token in ("std::vector", "std::deque", "std::list", "std::map",
                  "push_back", "resize(", "reserve("):
        require(token not in combined,
                f"the pure window manager must not grow unbounded ({token})")

    # The manager is a single owner: final, non-copyable, and the only class
    # allowed to mint a capability.
    require("class manager final" in header,
            "the window manager policy must be final")
    require("manager(const manager &) = delete;" in header and
            "manager &operator=(const manager &) = delete;" in header,
            "the window manager policy must not be copyable")
    require(header.count("friend class manager;") == 1,
            "only the manager may mint a view context")


def check_view_context_opaque(header: str) -> None:
    """TEST-WM-06/TEST-WM-05: `view_context` is a capability, not a handle."""
    body = block_after(header, "class view_context")
    public = body.split("private:", 1)[0]
    require("public:" in public, "view_context must declare its public surface")
    require(public.count("public:") == 1,
            "view_context must not reopen a second public section")
    private = body.split("private:", 1)[1]

    # Public surface: default construction plus one predicate, nothing else.
    require("view_context() = default;" in public,
            "a view context must stay default constructible (empty)")
    require("bool empty() const" in public,
            "a view context must expose exactly one predicate: empty()")
    require("slot_" not in public,
            "a view context must not publish its slot to applications")
    require(re.search(r"\b(?:slot|generation)\s*\(\s*\)", public) is None,
            "a view context must not expose a slot()/generation() getter")

    # Minting requires slot + generation, is private, and is only reachable by
    # the manager; otherwise an app could forge a live handle.
    require("friend class manager;" in private,
            "the minting constructor must stay private and befriended")
    require(re.search(r"view_context\s*\(\s*std::uint16_t\s+slot\s*,\s*"
                      r"std::uint64_t\s+generation\s*\)", private) is not None,
            "the minting constructor must require both slot and generation")
    require(private.count("view_context(") == 1,
            "view_context must expose exactly one minting constructor")
    for member in ("std::uint16_t slot_", "std::uint64_t generation_"):
        require(member in private,
                f"the opaque capability must keep {member.split()[-1]} private")

    # No implicit conversion to anything an app could dereference.
    require("operator" not in public,
            "a view context must not define a conversion operator")


def check_boundary_guards(source: str) -> None:
    """TEST-WM-07: every capability, payload and registry bound fails closed."""
    find = function_body(source, "std::size_t manager::find(")
    require(re.search(r"context\.slot_\s*>=\s*k_max_surfaces", find) is not None,
            "find() must range-check the slot before indexing the registry")
    require("context.empty()" in find,
            "find() must reject an empty capability")
    require("item.generation == context.generation_" in find,
            "find() must compare the generation, so a stale capability expires")
    require("item.state != surface_state::absent" in find,
            "find() must reject a capability whose slot is free")
    require("? context.slot_ : k_invalid_slot" in find,
            "find() must fail closed to the invalid sentinel")

    create = function_body(source, "view_status manager::create(")
    require(create.lstrip().startswith("out = {};"),
            "create() must clear the out capability before any decision, so a "
            "refused registration never leaks the caller's previous handle")
    require("find_app(app) != k_invalid_slot" in create,
            "create() must refuse a second surface for the same application")
    require("if (next_generation_ == 0) ++next_generation_;" in create,
            "create() must never mint generation zero, which reads as empty")
    require("view_context(static_cast<std::uint16_t>(i), item.generation)" in create,
            "create() must mint the capability from the slot and its generation")
    require(create.rindex("return view_status::overflow;") > create.index("for ("),
            "create() must still fail closed when the registry is full")
    require("focused_slot_ == k_invalid_slot && !teardown_pending" in create,
            "create() must not let a new surface preempt the teardown fallback")

    begin_teardown = function_body(source, "view_status manager::begin_teardown(")
    require("surfaces_[slot].state = surface_state::tearing_down;" in begin_teardown,
            "begin_teardown() must publish the tearing-down state")
    require("if (focused_slot_ == slot) focused_slot_ = k_invalid_slot;" in begin_teardown,
            "begin_teardown() must release the input ownership immediately")
    require("context.empty() ? view_status::invalid_context : view_status::expired_context"
            in begin_teardown,
            "begin_teardown() must distinguish empty from expired capabilities")

    remove = function_body(source, "view_status manager::remove(")
    require("surfaces_[slot].state != surface_state::tearing_down" in remove,
            "remove() must refuse a surface that never began its teardown")
    require("surfaces_[slot] = {};" in remove,
            "remove() must free the slot so it cannot keep a stale capability")
    require("--count_;" in remove,
            "remove() must decrement the surface registry count")

    for operation in ("activate", "hide", "focus"):
        body = function_body(source, f"view_status manager::{operation}(")
        require("== surface_state::tearing_down) return view_status::expired_context;"
                in body,
                f"{operation}() must expire a surface that is tearing down")

    copy_text = function_body(source, "void manager::copy_text(")
    require("std::min(text.size(), out.size() - 1)" in copy_text,
            "copy_text() must clamp the payload to the array minus the terminator")
    require("std::copy_n(text.data(), length, out.data())" in copy_text,
            "copy_text() must copy only the clamped length")
    require("out[length] = '\\0';" in copy_text,
            "copy_text() must always terminate the payload")

    notify = function_body(source, "bool manager::notify(")
    require_before(notify, "text.size() >= notifications_[0].text.size()",
                   "const std::size_t index = (notification_head_ + notification_count_)",
                   "notify() must reject an oversized payload before it "
                   "reaches the ring buffer index")
    require("++dropped_notifications_; return false;" in notify,
            "a full notification queue must fail closed and stay accounted")
    require("== surface_state::tearing_down) return false;" in notify,
            "notify() must quiesce a surface that is tearing down")
    require("++notification_count_;" in notify and notify.rindex("return true;") >
            notify.index("++notification_count_;"),
            "notify() must enqueue before reporting acceptance")

    pop = function_body(source, "bool manager::pop_notification(")
    require("if (notification_count_ == 0) return false;" in pop,
            "pop_notification() must fail closed on an empty queue")
    require("notification_head_ = (notification_head_ + 1) % notifications_.size();"
            in pop,
            "pop_notification() must advance the head inside the fixed ring")

    reset = function_body(source, "void manager::reset(")
    require("surfaces_ = {};" in reset and "notifications_ = {};" in reset,
            "reset() must drop both registries")
    require("count_ = notification_count_ = notification_head_ = 0;" in reset,
            "reset() must zero the registry and queue counts")
    require("focused_slot_ = k_invalid_slot;" in reset,
            "reset() must clear the input ownership")
    require("dropped_notifications_ = 0;" in reset,
            "reset() must clear the drop diagnostic")


def check_composed_root(header: str, source: str, shim: str) -> None:
    """TEST-WM-03: the adapter is the single owner of the composed LVGL root."""
    require("lvgl.h" not in "\n".join(includes(header)),
            "the adapter header must not include LVGL; it only forward-declares "
            "the object type so the policy stays includable from the UI")
    require("struct _lv_obj_t;" in header and "using lv_obj_t = _lv_obj_t;" in header,
            "the adapter header must forward-declare the LVGL object type")
    require('"apps/runtime/cyberdeck_window_manager.h"' in includes(header),
            "the adapter must consume the pure policy header, not a copy")
    require("class adapter final" in header,
            "the adapter must be final: one composed root, no subclasses")
    for accessor in ("screen()", "content()", "system_bar()", "policy()",
                     "ready()", "init()", "deinit()"):
        require(accessor in header,
                f"the adapter must expose {accessor} to the platform layer")
    require("cyberdeck_window_manager::manager policy_;" in header,
            "the adapter must own the policy instance it hands out")

    require('"lvgl.h"' in includes(source),
            "LVGL must be included by the adapter implementation only")
    init = function_body(source, "bool adapter::init()")
    require("if (screen_ != nullptr) return true;" in init,
            "init() must be idempotent: a live root is not rebuilt")
    require_before(init, "screen_ = lv_scr_act();", "system_bar_ = lv_obj_create(screen_);",
                   "init() must take the LVGL root before composing the areas")
    require_before(init, "system_bar_ = lv_obj_create(screen_);",
                   "content_ = lv_obj_create(screen_);",
                   "the persistent system bar must be composed before the content area")
    require("lv_obj_set_height(system_bar_, 42);" in init,
            "the persistent system bar must keep its fixed height")
    require("lv_obj_set_width(system_bar_, LV_PCT(100));" in init,
            "the persistent system bar must span the root")
    require("lv_obj_set_width(content_, LV_PCT(100));" in init,
            "the content area must span the root width")
    require("lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);" in init,
            "the content area must own a column layout for its children")
    failure = re.search(
        r"if\s*\(\s*system_bar_\s*==\s*nullptr\s*\|\|\s*content_\s*==\s*nullptr\s*\)"
        r"\s*\{(?P<body>.*?)\}", init, re.S)
    require(failure is not None,
            "init() must detect a partially composed root")
    if failure is not None:
        require("deinit();" in failure.group("body"),
                "a partial composition must be unwound instead of published")
    require(init.count("return false;") >= 2,
            "init() must fail closed both without a screen and on a partial root")
    require("screen_ = nullptr" not in init,
            "init() must not clear a live root before deciding")

    deinit = function_body(source, "void adapter::deinit()")
    require(re.search(r"if\s*\(\s*content_\s*!=\s*nullptr\s*\)\s*lv_obj_del\(content_\);",
                      deinit) is not None,
            "deinit() must delete the content area it created")
    require(re.search(r"if\s*\(\s*system_bar_\s*!=\s*nullptr\s*\)\s*"
                      r"lv_obj_del\(system_bar_\);", deinit) is not None,
            "deinit() must delete the system bar it created")
    require(re.search(r"lv_obj_del\(\s*screen_\s*\)", deinit) is None and
            re.search(r"lv_obj_delete\(\s*screen_\s*\)", deinit) is None,
            "deinit() must not delete the LVGL screen: LVGL owns the root")
    require(deinit.count("= nullptr;") >= 3,
            "deinit() must null the root, the system bar and the content area")
    require("policy_.reset();" in deinit,
            "deinit() must reset the policy so no capability survives the root")

    require("static adapter instance;" in source,
            "global() must expose one adapter instance, not a fresh root per caller")

    # The host shim must not decide which LVGL spelling the adapter may use.
    for spelling in ("lv_obj_del", "lv_obj_delete", "lv_scr_act",
                     "lv_shim_active_screen"):
        require(re.search(rf"\b{spelling}\s*\(", shim) is not None,
                f"the LVGL shim must provide {spelling} so the adapter links on host")


def check_bar_row_layout(adapter_source: str, terminal_view: str, ui: str,
                         shim: str) -> None:
    """TEST-REG-8-BAR: the bar owns a fixed row, the content starts below it.

    BUG-8-WM-BAR kept a full-root height on the content while the root became a
    vertical flex column, so the content overflowed the row the bar already
    occupied.  The layout below pins the fixed row and forbids any explicit
    content height; `bar_layout_scenario` in the behavioral suite proves the
    placement the composed tree performs.
    """
    init = function_body(adapter_source, "bool adapter::init()")

    # The root is a vertical flex column, chosen before the areas exist so no
    # child is ever composed against the default layout.
    require("lv_obj_set_layout(screen_, LV_LAYOUT_FLEX);" in init,
            "the composed root must lay its areas out with the flex layout")
    require("lv_obj_set_flex_flow(screen_, LV_FLEX_FLOW_COLUMN);" in init,
            "the composed root must flow its areas vertically")
    require_before(init, "lv_obj_set_flex_flow(screen_, LV_FLEX_FLOW_COLUMN);",
                   "system_bar_ = lv_obj_create(screen_);",
                   "the root layout must be chosen before the areas are composed")

    # The bar is the fixed first row: 42 px that never grows.
    require("lv_obj_set_height(system_bar_, 42);" in init,
            "the persistent system bar must keep its fixed 42 px row")
    require("lv_obj_set_flex_grow(system_bar_, 0);" in init,
            "the system bar must not grow: its 42 px row is the whole row "
            "(BUG-8-WM-BAR)")

    # The content grows into the row left below the bar.  No explicit height may
    # come back on the content under any spelling, or the two rows overlap.
    require("lv_obj_set_flex_grow(content_, 1);" in init,
            "the content area must grow into the row left below the system bar")
    require("lv_obj_set_height(content_, LV_PCT(100));" not in init,
            "the content area must not be forced to a full root height: under the "
            "root column that is what overlapped the system bar (BUG-8-WM-BAR)")
    require(re.search(r"\blv_obj_set_(?:size|height)\s*\(\s*content_", init) is None,
            "the content area must take no explicit height from any spelling; "
            "the root column owns its row (BUG-8-WM-BAR)")
    require(init.count("lv_obj_set_flex_grow(") == 2,
            "only the bar and the content may carry a flex grow in the composed root")

    # The virtual keyboard is a floating overlay on the root: created on the
    # screen, excluded from the layout and placed by hand, so showing it never
    # consumes a row of the column.
    create = function_body(terminal_view, "bool view::create(")
    require("lv_keyboard_create(screen)" in create,
            "the virtual keyboard must be created on the composed root, not inside "
            "the content column")
    require("lv_textarea_create(parent)" in create,
            "the terminal area must stay inside the content column")
    require("lv_obj_set_ignore_layout(s_keyboard, true);" in create,
            "the virtual keyboard must be excluded from the root flex column")
    require("lv_obj_set_hidden(s_keyboard, true);" in create,
            "the virtual keyboard must start hidden, floating above the content")
    require("LV_OBJ_FLAG_IGNORE_LAYOUT" not in terminal_view,
            "the virtual keyboard exclusion must not go back to the deprecated "
            "lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT) spelling, which "
            "LV_DEPRECATED replaced with the lv_obj_set_ignore_layout() setter")
    # The exclusion must survive every build.  `lvgl.h` pulls `lv_version.h` in,
    # so a version guard is true on device but silently compiles the invariant
    # away in any host build whose header does not define the macro -- which is
    # exactly the row this regression is about.
    require(re.search(r"#\s*(?:if|ifdef|ifndef)\b", create) is None,
            "the keyboard overlay exclusion must be unconditional: a preprocessor "
            "guard can compile the BUG-8-WM-BAR invariant out of the host build")
    require("lv_obj_is_ignore_layout(" in shim,
            "the LVGL shim must expose the ignore_layout read-back so the host "
            "suite can observe the exclusion instead of trusting the spelling")
    require("lv_obj_set_ignore_layout(" in shim,
            "the LVGL shim must expose lv_obj_set_ignore_layout so the real "
            "production statement links on host")
    require(re.search(r"lv_obj_set_(?:size|width|height|flex_grow|flex_flow)\s*\("
                      r"\s*s_keyboard", create) is None,
            "the virtual keyboard must never take a row or a size of the column")
    require(re.search(r"lv_obj_add_flag\(\s*s_keyboard\s*,\s*LV_OBJ_FLAG_FLOATING\s*\)",
                      terminal_view) is None,
            "the keyboard overlay is kept out of the layout by IGNORE_LAYOUT, not "
            "by a floating flag that would change with the LVGL major version")

    # The UI reveals the overlay by aligning it, never by restyling the areas.
    require("lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID" in ui,
            "the UI must float the revealed keyboard over the content")
    require(re.search(r"lv_obj_(?:set_height|set_flex_grow|set_flex_flow)\s*\(\s*s_menu",
                      ui) is None,
            "revealing the keyboard must not resize or re-flow the composed content")
    require(re.search(r"lv_obj_(?:set_height|set_flex_grow|set_flex_flow)\s*\(\s*s_screen",
                      ui) is None,
            "revealing the keyboard must not resize or re-flow the composed root")


def check_bar_chrome(adapter_source: str, header_view: str, shim: str) -> None:
    """TEST-REG-8-CHROME: the bar is chrome-only, so the header fills the row.

    The bar used to inherit the LVGL theme card chrome (grey background, border
    and padding), so the header the UI attaches to it sat inset inside the 42 px
    row instead of filling it.  These statements are the contract; the binary
    proves the placement they produce.
    """
    init = function_body(adapter_source, "bool adapter::init()")

    # Background: black is part of the chrome contract, not an inherited default.
    require(re.search(r"lv_obj_set_style_bg_color\(\s*system_bar_\s*,\s*\w+\s*,\s*0\s*\)",
                      init) is not None,
            "the system bar must be styled explicitly: its black background is "
            "the corrected chrome, not the theme card it used to inherit")
    require(re.search(r"lv_color_hex\(\s*0x000000\s*\)", adapter_source) is not None,
            "the bar background must be black (0x000000); the theme card colour "
            "is the visible defect this regression removed")

    # Border and padding: zero on every side plus the internal gaps, because the
    # bar hosts a single full-width row and any inset displaces the header.
    for statement, reason in (
        ("lv_obj_set_style_border_width(system_bar_, 0, 0);",
         "the system bar must drop the inherited card border"),
        ("lv_obj_set_style_pad_all(system_bar_, 0, 0);",
         "the system bar must drop the inherited card padding"),
        ("lv_obj_set_style_pad_row(system_bar_, 0, 0);",
         "the system bar must declare no gap between its rows"),
        ("lv_obj_set_style_pad_column(system_bar_, 0, 0);",
         "the system bar must declare no gap between its columns"),
    ):
        require(statement in init, reason)
    bar_pads = re.findall(
        r"lv_obj_set_style_pad_all\(\s*system_bar_\s*,\s*(\d+)\s*,\s*0\s*\)", init)
    require(bar_pads and set(bar_pads) == {"0"},
            "the system bar must only ever be given a zero padding: a nonzero "
            "padding is exactly what displaced the header inside the row")

    # Scroll off: direction, chaining and scrollbar are three separate switches
    # in LVGL 9, and an object is scrollable and chains scrolling by default
    # (`lv_obj_constructor()`), so each one has to be turned off explicitly.
    require("disable_scrolling(system_bar_);" in init,
            "the system bar must disable its scrolling")
    helper = function_body(adapter_source, "void disable_scrolling(")
    for statement, reason in (
        ("lv_obj_set_scroll_dir(object, LV_DIR_NONE);",
         "the bar must scroll in no direction"),
        ("lv_obj_set_scroll_chain(object, false);",
         "the bar must not chain scrolling to its siblings"),
        ("lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);",
         "the bar must never show a scrollbar"),
    ):
        require(statement in helper, reason)
    require(re.search(r"lv_obj_set_scrollable\(\s*system_bar_", init) is None,
            "the bar must not re-enable the scrollable flag it inherits by default")

    # Geometry: the fixed 42 px row that never grows is asserted here (spelling)
    # and observed in the binary (placement); here it must also be explicit, so
    # `flex_grow 0` cannot be inherited from a default.
    require("lv_obj_set_flex_grow(system_bar_, 0);" in init,
            "the system bar must declare flex grow 0 explicitly")
    require("lv_obj_set_height(system_bar_, 42);" in init,
            "the system bar must keep its fixed 42 px row")
    require("lv_obj_set_width(system_bar_, LV_PCT(100));" in init,
            "the system bar must span the root width")

    # The external padding stays on the root: it is what insets both areas from
    # the panel edges, and removing it would move the composed rows.
    require("lv_obj_set_style_pad_all(screen_, 12, 0);" in init,
            "the composed root must keep its external padding")

    # The header is the real view the UI attaches to the bar, and it must claim
    # the whole row and drop its own chrome, or its grid starts inset.
    create = function_body(header_view, "bool view::create(")
    require("lv_obj_set_size(header, lv_pct(100), 42);" in create,
            "the header must claim the whole bar width by the full 42 px height")
    require(re.search(r"lv_obj_set_style_pad_all\(\s*header\s*,\s*0\s*,\s*0\s*\)",
                      create) is not None,
            "the header must drop its own padding, or it sits inset inside the bar")
    base = function_body(header_view, "void style_base(")
    require("lv_obj_set_style_border_width(object, 0, 0);" in base,
            "the styled header objects must drop their own border")
    require("style_base(header," in create,
            "the header must go through the zero-border styling")

    # LVGL 9 has no public getter for a style property, for the layout or for the
    # flex attributes, so the host suite observes the chrome through what the
    # shim recorded and through LVGL's own content-area read-backs.  A shim that
    # defaulted the recorded chrome to zero would make every behavioural check
    # above vacuous, so the unset sentinel and the read-backs are pinned here.
    fields = block_after(shim, "struct _lv_obj_t {")
    for field in ("border_width", "pad_top", "pad_left", "pad_right", "pad_row",
                  "pad_column", "flex_grow", "scroll_dir", "scrollbar_mode",
                  "scroll_chain"):
        require(re.search(rf"\b{field}\b", fields) is not None,
                f"the LVGL shim must record {field} so the host suite can observe "
                f"the chrome production applied")
    require(re.search(r"int32_t border_width\{-1\}", fields) is not None,
            "the shim must mark an unapplied style as unset, or an explicit zero "
            "chrome is indistinguishable from a default")
    for spelling in ("lv_obj_get_scroll_dir(", "lv_obj_get_scrollbar_mode(",
                     "lv_obj_get_content_width(", "lv_obj_get_content_height("):
        require(spelling in shim,
                f"the LVGL shim must expose {spelling} so the host suite asserts "
                f"the device-visible result and not the spelling that produced it")


def check_lvgl_confined_to_adapter(adapter_source: Path, ui: str,
                                  demo_header: str, demo_source: str) -> list[str]:
    """TEST-WM-05: applications never reach the LVGL tree themselves."""
    production = production_sources()
    require(len(production) > 50,
            "the production source scan must cover the whole component")

    root_callers = sorted(
        path.relative_to(ROOT).as_posix()
        for path in production
        if re.search(r"\blv_scr_act\s*\(", path.read_text(encoding="utf-8"))
    )
    require(root_callers == [adapter_source.relative_to(ROOT).as_posix()],
            f"the adapter must be the only TU that acquires the LVGL root, "
            f"found {root_callers}")

    # Applications own no widget: no creation, no destruction, no restyling.
    unused = []
    for path in app_sources():
        text = path.read_text(encoding="utf-8")
        relative = path.relative_to(ROOT).as_posix()
        forbidden = TREE_OWNERSHIP_CALLS
        if relative == BRIDGE_INPUT_INJECTION_TU:
            # The bridge may only inject input; it still may not own the tree.
            forbidden = tuple(pattern for pattern in TREE_OWNERSHIP_CALLS
                              if pattern not in BRIDGE_INPUT_INJECTION_CALLS)
        for pattern in forbidden:
            require(re.search(pattern, text) is None,
                    f"an application TU must not call the LVGL tree API "
                    f"{pattern} ({relative})")
        declared = '"lvgl.h"' in includes(text)
        uses_lvgl = re.search(r"\blv_[a-z_0-9]+\s*\(", text) is not None
        if declared and relative not in LVGL_READER_TUS:
            # No authorized reader: the include is only acceptable while it is
            # actually unused, and that dead weight is reported.
            require(not uses_lvgl,
                    f"an application TU must not include LVGL ({relative})")
            unused.append(relative)
        require(not any(include.endswith("cyberdeck_window_manager_adapter.h")
                        for include in includes(text)),
                f"an application TU must not include the LVGL adapter ({relative})")

    # Only the platform layer may see an LVGL type in a header.
    for path in production:
        if not path.as_posix().endswith((".h", ".hpp")):
            continue
        if '"lvgl.h"' in includes(path.read_text(encoding="utf-8")):
            require("include/platform/" in path.relative_to(ROOT).as_posix(),
                    f"only a platform header may include LVGL, found "
                    f"{path.relative_to(ROOT).as_posix()}")

    # The UI consumes the composed root; it never composes one itself.
    for pattern in (r"\blv_scr_act\s*\(", r"\blv_obj_create\s*\(",
                    r"\blv_obj_del(?:ete)?\s*\("):
        require(re.search(pattern, ui) is None,
                f"the UI must consume the adapter's root, not build it ({pattern})")

    # The compiled-in demo application stays headless: it is the reference for
    # "an application depends on the runtime, not on the display".
    for label, text in (("demo header", demo_header), ("demo source", demo_source)):
        require("lvgl" not in text, f"the {label} must stay free of LVGL")
        require(not any(include.endswith(("cyberdeck_window_manager.h",
                                          "cyberdeck_window_manager_adapter.h"))
                        for include in includes(text)),
                f"the {label} must not depend on the window manager seam")
    require('"apps/runtime/cyberdeck_app_runtime.h"' in includes(demo_header),
            "the demo application must depend on the runtime it is built against")

    return unused


def check_ui_uses_composed_root(ui: str) -> None:
    """TEST-WM-03: the persistent bar and the content area come from the adapter."""
    require("cyberdeck_window_manager::view_context s_shell_view_context;" in ui,
            "the UI must hold the shell capability, not a raw LVGL handle")
    init = function_body(ui, 'extern "C" esp_err_t cyberdeck_ui_init(')

    adapter_init = init.index("window_manager.init()")
    create = init.index("window_manager.policy().create(1, s_shell_view_context)")
    screen = init.index("s_screen = window_manager.screen();")
    content = init.index("s_menu = window_manager.content();")
    header_match = re.search(
        r"s_header_view\.create\(\s*window_manager\.system_bar\(\)\s*\)",
        init,
    )
    require(header_match is not None,
            "the UI must attach the header view to the persistent system bar")
    header = header_match.start()
    require(adapter_init < create < screen < content < header,
            "init must compose the root, register the shell surface, then bind "
            "screen, content and the persistent system bar in that order")

    adapter_failure = re.search(r"if\s*\(\s*!window_manager\.init\(\)\s*\)\s*"
                                r"\{(?P<body>.*?)\}", init, re.S)
    require(adapter_failure is not None,
            "a failed adapter init must have a failure branch")
    if adapter_failure is not None:
        require("destroy_ui_resource_handles()" in adapter_failure.group("body"),
                "a failed adapter init must unwind the partially built UI")
    create_failure = re.search(
        r"if\s*\(\s*window_manager\.policy\(\)\.create\([^)]*\)\s*!=\s*"
        r"cyberdeck_window_manager::view_status::ok\s*\)\s*\{(?P<body>.*?)\}",
        init, re.S)
    require(create_failure is not None,
            "a refused shell surface registration must have a failure branch")
    if create_failure is not None:
        require("destroy_ui_resource_handles()" in create_failure.group("body"),
                "a refused registration must unwind the composed root")

    # The header view attaches to the adapter's persistent bar, so the bar is
    # never rebuilt per app and never duplicated by the UI.
    require(init.count("window_manager.system_bar()") == 1,
            "the UI must attach to the persistent system bar exactly once")
    require(ui.count("lv_obj_set_flex_flow(") == 0,
            "the UI must not restyle the composed content area")


def check_ui_teardown_before_deinit(ui: str) -> None:
    """TEST-WM-07: the shell surface is torn down before the root is released."""
    destroy = function_body(ui, "void destroy_ui_resource_handles(")
    guard = re.search(r"if\s*\(\s*!s_shell_view_context\.empty\(\)\s*\)\s*"
                      r"\{(?P<body>.*?)\n    \}", destroy, re.S)
    require(guard is not None,
            "the shell teardown must be guarded by an empty-capability check, so a "
            "second deinit is inert")
    teardown = guard.group("body") if guard is not None else ""
    require_before(teardown, "begin_teardown(s_shell_view_context)",
                   "remove(s_shell_view_context)",
                   "the shell surface must begin its teardown before removal")
    require_before(teardown, "remove(s_shell_view_context)",
                   "s_shell_view_context = {};",
                   "the capability must be cleared after removal")
    require("s_shell_view_context = {};" in teardown,
            "a removed capability must not survive in the UI state")
    require("window_manager.deinit()" not in teardown,
            "the root must not be released inside the guarded surface teardown")
    require_before(destroy, "begin_teardown(s_shell_view_context)",
                   "window_manager.deinit()",
                   "the shell surface must be torn down before the root is released")
    require_before(destroy, "s_shell_app.detach_console();",
                   "begin_teardown(s_shell_view_context)",
                   "the console owner must be detached before its capability expires")
    require(destroy.count("window_manager.deinit()") == 1,
            "the root must be released exactly once per teardown pass")
    require('extern "C" void cyberdeck_ui_deinit(' in ui,
            "the UI must keep a single deinit entry point")
    deinit = function_body(ui, 'extern "C" void cyberdeck_ui_deinit(')
    require("destroy_ui_resource_handles();" in deinit,
            "the UI deinit must route through the single teardown pass")


def check_wiring(component: str, guard: str, makefile: str, codemap: str) -> None:
    """TEST-WM-03/06/07: build and coverage scope must cover both TUs."""
    for source in ("src/apps/runtime/cyberdeck_window_manager.cpp",
                   "src/platform/display/cyberdeck_window_manager_adapter.cpp"):
        require(f'"{source}"' in component,
                f"the component must register {source} in the firmware build")
    require("REQUIRES" in component and "lvgl" in component,
            "the component must keep the C++ and LVGL dependencies")

    # Both TUs are host-covered, so neither may sit in the hardware allowlist.
    for source in ("components/cyberdeck/src/apps/runtime/cyberdeck_window_manager.cpp",
                   "components/cyberdeck/src/platform/display/cyberdeck_window_manager_adapter.cpp"):
        require(source not in guard,
                f"{source} is host-covered and must stay out of the coverage "
                "allowlist")

    behavioral_prerequisites = makefile_target(makefile, "test_window_manager")
    for token in ("test_window_manager.cpp", "$(WINDOW_MANAGER_SRC)",
                  "$(WINDOW_MANAGER_HDR)", "$(WINDOW_MANAGER_ADAPTER_SRC)",
                  "$(WINDOW_MANAGER_ADAPTER_HDR)", "shim/full/lvgl.h",
                  # TEST-REG-8-CHROME observes the real header attached to the
                  # real bar, so the behavioral binary must link that view.
                  "$(HEADER_VIEW_SRC)", "$(WIFI_ICON_SRC)",
                  "$(WIFI_INDICATOR_SRC)", "$(BATTERY_VIEW_SRC)"):
        require(token in behavioral_prerequisites,
                f"test_window_manager must declare {token} as a prerequisite")
    behavioral_recipe = makefile_recipe(makefile, "test_window_manager")
    for token, message in (
        ("-Ishim/full", "the host build must use the full LVGL shim"),
        ("-std=c++17", "the host build must pin the C++ standard"),
        ("-Wall -Wextra -Werror", "the host build must keep warnings fatal"),
        ("--coverage", "the host build must emit coverage for the scope guard"),
        ("test_window_manager.cpp", "the behavioral test must be compiled"),
        ("$(WINDOW_MANAGER_SRC)", "the real pure policy must be linked"),
        ("$(WINDOW_MANAGER_ADAPTER_SRC)", "the real adapter must be linked"),
        ("$(HEADER_VIEW_SRC)", "the real header view must be linked"),
        ("$(WIFI_ICON_SRC)", "the header view dependency must be linked"),
        ("$(WIFI_INDICATOR_SRC)", "the header view dependency must be linked"),
        ("$(BATTERY_VIEW_SRC)", "the header view dependency must be linked"),
    ):
        require(token in behavioral_recipe, f"test_window_manager must keep {message}")
    require(re.search(r"\$\(CXX\)", behavioral_recipe) is not None,
            "the host build must go through the project C++ compiler")
    require("-I../../../components/cyberdeck/include" in behavioral_recipe,
            "the host build must reach the production include tree")

    contract_prerequisites = makefile_target(makefile, "test_window_manager_contract")
    require("test_window_manager_contract.py" in contract_prerequisites,
            "the contract target must depend on this contract")
    contract_recipe = makefile_recipe(makefile, "test_window_manager_contract")
    require("$(PYTHON) test_window_manager_contract.py" in contract_recipe,
            "the contract target must run this contract with the project python")
    require(re.search(r"^PYTHON\s*\?:?=\s*python3", makefile, re.M) is not None,
            "the contract target must run with a pinned python interpreter")

    # Every file this this contract inspects must be a declared
    # prerequisite, so a stale source cannot silently invalidate the checks.
    prerequisites = makefile_prerequisites(makefile, "test_window_manager_contract")
    for token in ("cyberdeck_window_manager.h", "cyberdeck_window_manager.cpp",
                  "cyberdeck_window_manager_adapter.h",
                  "cyberdeck_window_manager_adapter.cpp", "cyberdeck_ui.cpp",
                  "cyberdeck_terminal_view.cpp", "cyberdeck_header_view.cpp",
                  "cyberdeck_demo_app.cpp",
                  "components/cyberdeck/CMakeLists.txt",
                  "tools/coverage_scope_guard.py", "code-map.md", "Makefile",
                  "shim/full/lvgl.h"):
        require(token in prerequisites,
                f"the contract target must depend on {token}")

    require(re.search(r"^\.PHONY:.*\btest_window_manager\b", makefile, re.M) is not None,
            "the behavioral target must be phony")
    require(re.search(r"^\.PHONY:.*\btest_window_manager_contract\b", makefile,
                      re.M) is not None,
            "the contract target must be phony")
    require(re.search(r"^BINS\s*:=.*\btest_window_manager\b", makefile, re.M) is not None,
            "the behavioral binary must belong to the aggregated BINS list")
    aggregate = makefile_recipe(makefile, "test")
    require("./test_window_manager" in aggregate,
            "the aggregated host run must execute the behavioral binary")
    require("test_window_manager_contract" in aggregate,
            "the aggregated host run must execute this contract")
    require(aggregate.index("./test_window_manager") <
            aggregate.index("test_window_manager_contract"),
            "the behavioral binary must run before the structural contract")

    require("code-map.md" in codemap and
            all(token in codemap for token in (
                "cyberdeck_window_manager.h", "cyberdeck_window_manager.cpp",
                "cyberdeck_window_manager_adapter.h",
                "cyberdeck_window_manager_adapter.cpp",
                "test_window_manager.cpp", "test_window_manager_contract.py")),
            "code-map.md must document both window manager TUs and both tests")

    # The host tree ignores every test_* path; the sources need an explicit
    # exception or the suite would silently stop running this contract.
    for source in ("test_window_manager.cpp", "test_window_manager_contract.py"):
        visible = subprocess.run(
            ["git", "check-ignore", "-q", "--no-index", f"tests/host/keymap/{source}"],
            cwd=str(ROOT), capture_output=True).returncode
        require(visible != 0,
                f"the window manager test sources must be trackable despite the "
                f"host-test ignore rule: {source}")
    ignored = subprocess.run(
        ["git", "check-ignore", "-q", "--no-index",
         "tests/host/keymap/test_window_manager"],
        cwd=str(ROOT), capture_output=True).returncode
    require(ignored == 0,
            "the compiled window manager host binary must stay ignored")


def main() -> int:
    for path in (POLICY_HEADER, POLICY_SOURCE, ADAPTER_HEADER, ADAPTER_SOURCE,
                 UI_SOURCE, TERMINAL_VIEW_SOURCE, HEADER_VIEW_SOURCE, DEMO_SOURCE,
                 DEMO_HEADER, CMAKE, SCOPE_GUARD, CODEMAP, MAKEFILE, SHIM,
                 BEHAVIORAL_TEST):
        require(path.exists(), f"window manager contract input is missing: {path}")

    header = strip_comments(POLICY_HEADER.read_text(encoding="utf-8"))
    source = strip_comments(POLICY_SOURCE.read_text(encoding="utf-8"))
    adapter_header = strip_comments(ADAPTER_HEADER.read_text(encoding="utf-8"))
    adapter_source = strip_comments(ADAPTER_SOURCE.read_text(encoding="utf-8"))
    ui = strip_comments(UI_SOURCE.read_text(encoding="utf-8"))
    terminal_view = strip_comments(TERMINAL_VIEW_SOURCE.read_text(encoding="utf-8"))
    header_view = strip_comments(HEADER_VIEW_SOURCE.read_text(encoding="utf-8"))
    demo_header = strip_comments(DEMO_HEADER.read_text(encoding="utf-8"))
    demo_source = strip_comments(DEMO_SOURCE.read_text(encoding="utf-8"))
    shim = strip_comments(SHIM.read_text(encoding="utf-8"))
    component = strip_comments(CMAKE.read_text(encoding="utf-8"))
    guard = strip_comments(SCOPE_GUARD.read_text(encoding="utf-8"))
    makefile = strip_comments(MAKEFILE.read_text(encoding="utf-8"))
    codemap = strip_comments(CODEMAP.read_text(encoding="utf-8"))

    check_pure_policy(header, source)
    check_view_context_opaque(header)
    check_boundary_guards(source)
    check_composed_root(adapter_header, adapter_source, shim)
    check_bar_row_layout(adapter_source, terminal_view, ui, shim)
    check_bar_chrome(adapter_source, header_view, shim)
    unused_includes = check_lvgl_confined_to_adapter(ADAPTER_SOURCE, ui, demo_header,
                                                 demo_source)
    check_ui_uses_composed_root(ui)
    check_ui_teardown_before_deinit(ui)
    check_wiring(component, guard, makefile, codemap)

    # The behavioral suite must keep carrying the scenarios this contract cannot.
    behavioral = strip_comments(BEHAVIORAL_TEST.read_text(encoding="utf-8"))
    for scenario in ("TEST-WM-01", "TEST-WM-02", "TEST-WM-03", "TEST-WM-04",
                     "TEST-WM-05", "TEST-WM-06", "TEST-WM-07", "TEST-REG-8-BAR",
                     "TEST-REG-8-CHROME"):
        require(scenario in behavioral,
                f"the behavioral host suite must keep {scenario} traceable")
    for entry in ("api_shape_scenario()", "surfaces_scenario()", "focus_scenario()",
                  "teardown_scenario()", "notifications_scenario()",
                  "adapter_scenario()", "bar_layout_scenario()",
                  "bar_chrome_scenario()"):
        require(entry in behavioral,
                f"the behavioral host suite must keep running {entry}")
    require("TEST-WM-01..07" in codemap or "TEST-WM-03" in codemap,
            "code-map.md must document the TEST-WM traceability")
    require("TEST-REG-8-BAR" in codemap,
            "code-map.md must document the TEST-REG-8-BAR layout regression")
    require("TEST-REG-8-CHROME" in codemap,
            "code-map.md must document the TEST-REG-8-CHROME chrome regression")

    print("PASS: window manager structural contract "
          "(TEST-WM-03/05/06/07, TEST-REG-8-BAR, TEST-REG-8-CHROME)")
    for path in unused_includes:
        print(f"FINDING: {path} includes lvgl.h without using it; the dead "
              "graphics dependency should be dropped (production change, out of "
              "test scope)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
