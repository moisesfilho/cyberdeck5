"""Structural contract for the externally-allocated SSH task stack.

BUG-SSH-001 / TEST-SSH-001..003, 008.

The SSH worker task owns a 24576-byte stack that does not fit the internal
heap, so the task (and therefore its stack) must be created with
``xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)`` and every self-deletion point
must use ``vTaskDeleteWithCaps(NULL)`` to release that external stack.  This is
a source-inspection contract: the module needs ESP-IDF/FreeRTOS/libssh and is
not host-linkable, and the runtime effect (heap/PSRAM accounting) is only
observable on the device (TEST-SSH-004..006).
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
SSH = (ROOT / "components/cyberdeck/src/apps/ssh/ssh_client.cpp").read_text()
CODE_MAP = (ROOT / "code-map.md").read_text()

# Single-line view of the module: every whitespace run (newline plus the
# continuation indentation) collapses to one space, so the assertions below pin
# tokens, argument order and punctuation without coupling to where the formatter
# happens to break lines. No searched string contains a double space or an
# in-literal newline, so the normalisation cannot invent or hide a match.
FLAT = " ".join(SSH.split())

TASK_FN = re.compile(r"void\s+ssh_client_task\s*\(\s*void\s*\*\s*pvParameters\s*\)")
CREATE_CALL = (
    'BaseType_t ret = xTaskCreateWithCaps(ssh_client_task, "ssh_client", 24576,'
    ' reinterpret_cast<void *>(static_cast<uintptr_t>(generation)), 5, &task,'
    " MALLOC_CAP_SPIRAM);"
)
CONNECT_RETURN = "return (ret == pdPASS) ? ESP_OK : ESP_FAIL;"
FAILURE_LOG = 'ESP_LOGE(TAG, "Falha ao criar a task SSH em PSRAM: ret=%d", (int)ret);'
FAILURE_BRANCH = (
    "if (ret == pdPASS) { s_task_handle.store(task, std::memory_order_release); } else { "
    "s_task_handle.store(nullptr, std::memory_order_release); "
    + FAILURE_LOG
    + " } xSemaphoreGive(s_state_mutex); "
)


def body_of(signature: "re.Pattern[str]", text: str) -> str:
    """Return the brace-balanced body that follows the matched signature."""
    match = signature.search(text)
    assert match is not None, f"missing signature {signature.pattern!r}"
    start = match.end()
    depth = 0
    for offset in range(start, len(text)):
        char = text[offset]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start:offset]
    raise AssertionError(f"unbalanced braces after {signature.pattern!r}")


# TEST-SSH-001: the task must be created in external PSRAM with an explicit
# stack size; the ESP-IDF caps-aware API is only available through these headers.
assert '#include "freertos/idf_additions.h"' in SSH, "missing freertos/idf_additions.h"
assert '#include "esp_heap_caps.h"' in SSH, "missing esp_heap_caps.h"
assert SSH.count("xTaskCreateWithCaps(") == 1, "exactly one caps-aware task creation expected"
assert CREATE_CALL in FLAT, f"SSH task must be created as {CREATE_CALL!r}"
assert "xTaskCreate(" not in SSH, "raw xTaskCreate( cannot place the stack in PSRAM"
assert "xTaskCreatePinnedToCore(" not in SSH, "raw xTaskCreatePinnedToCore( cannot place the stack in PSRAM"

# TEST-SSH-002: every self-deletion inside the worker releases the external
# stack; a raw vTaskDelete(NULL) leaks the PSRAM allocation.
task_body = body_of(TASK_FN, SSH)
assert task_body.count("vTaskDeleteWithCaps(NULL)") == 12, (
    f"expected 12 vTaskDeleteWithCaps(NULL) in ssh_client_task, found {task_body.count('vTaskDeleteWithCaps(NULL)')}"
)
assert SSH.count("vTaskDeleteWithCaps(NULL)") == 12, (
    f"expected 12 vTaskDeleteWithCaps(NULL) in the module, found {SSH.count('vTaskDeleteWithCaps(NULL)')}"
)
assert "vTaskDelete(" not in SSH, "vTaskDelete( leaks the externally allocated stack"

# TEST-SSH-003: a failed creation must be observable (log), must not publish a
# handle, must release the state mutex exactly once and must fail the call.
create_at = FLAT.index(CREATE_CALL) + len(CREATE_CALL)
connect_tail = FLAT[create_at:]
end_marker = CONNECT_RETURN + " }"
assert end_marker in connect_tail, (
    f"ssh_client_connect must translate the creation result as {CONNECT_RETURN!r}"
)
branch = connect_tail[:connect_tail.index(end_marker) + len(end_marker)]
assert branch.endswith(FAILURE_BRANCH + end_marker), f"unexpected creation/failure branch:\n{branch}"

# TEST-SSH-008: the externally allocated stack must stay documented.
map_line = next(line for line in CODE_MAP.splitlines() if "ssh_client.cpp" in line)
for token in ("xTaskCreateWithCaps", "MALLOC_CAP_SPIRAM", "vTaskDeleteWithCaps", "24576"):
    assert token in map_line, f"code-map.md ssh_client.cpp line must document {token}"

print("PASS: SSH task stack is created in PSRAM and released with the caps-aware API")