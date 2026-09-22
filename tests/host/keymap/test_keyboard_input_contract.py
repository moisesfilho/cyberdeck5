#!/usr/bin/env python3
"""Host-side structural TDD contract for physical keyboard dispatch.

The complete UI cannot be linked on the host (it depends on LVGL/BSP/IDF), so
this test deliberately inspects the production seam instead of duplicating it.
It must fail until the physical producer is reduced to a bounded async handoff.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed function: {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    source = UI.read_text(encoding="utf-8")
    producer = function_body(source, "void cyberdeck_keyboard_input(")

    # Physical input must be a snapshot + async handoff, never UI work or a
    # display lock taken from the keyboard/I2C task.
    require("lv_async_call" in producer,
            "physical input must enqueue through lv_async_call")
    require("memcpy" in producer or "std::copy" in producer,
            "physical input must copy the callback payload before returning")
    for forbidden in ("bsp_display_lock", "bsp_display_unlock", "execute_line",
                      "render_terminal", "local_key", "ssh_client_send_data"):
        require(forbidden not in producer,
                f"physical producer must not execute UI/SSH work: {forbidden}")

    # The handoff is bounded and owns rejected snapshots.  Accept either the
    # FreeRTOS queue spelling or an explicitly named C++ queue implementation,
    # but require the capacity and both enqueue/rejection paths in source.
    require(re.search(r"(?:QUEUE|queue|fifo|FIFO)[^\n]{0,100}8|8[^\n]{0,100}(?:QUEUE|queue|fifo|FIFO)", source),
            "keyboard handoff must declare an explicit capacity of 8")
    queue_send_failure = re.search(
        r"xQueueSend\s*\([^;]+?\)\s*!=\s*pdTRUE", producer, re.S
    )
    require(queue_send_failure is not None,
            "keyboard handoff must reject snapshots when xQueueSend(...) != pdTRUE")
    require(re.search(r"(?:free|delete|release|destroy)", source, re.I),
            "overflow path must release the rejected snapshot")
    if queue_send_failure is not None:
        rejection_tail = producer[queue_send_failure.start():queue_send_failure.start() + 300]
        require(re.search(r"free\s*\(\s*event\s*\)", rejection_tail) is not None,
                "xQueueSend rejection path must release the rejected snapshot")
    require(
        (re.search(r"xQueueSend\s*\(\s*s_keyboard_event_queue", source) and
         re.search(r"xQueueReceive\s*\(\s*s_keyboard_event_queue", source)) or
        re.search(r"(?:push_back[\s\S]{0,300}pop_front|"
                  r"tail[\s\S]{0,300}head)", source),
        "keyboard handoff must expose FIFO enqueue/dequeue order")

    # A successful queue send is not enough: lv_async_call can reject the
    # callback after ownership has moved to the queue.  The rollback must
    # remove exactly that snapshot, then release it, without dropping or
    # reordering the other queued snapshots.  Keep this structural because
    # the complete UI cannot be linked by the host suite.
    discard = function_body(source, "void discard_keyboard_event_from_queue(")
    require("xQueueReceive(s_keyboard_event_queue" in discard,
            "async rollback must drain the keyboard queue")
    require(re.search(r"pending_count\s*<\s*KEYBOARD_EVENT_QUEUE_CAPACITY", discard),
            "async rollback must drain at most the queue capacity")
    require(re.search(r"if\s*\(\s*pending\[i\]\s*==\s*event\s*\)\s*continue", discard),
            "async rollback must remove only the rejected event")
    require(re.search(r"for\s*\([^)]*i\s*=\s*0[^)]*<\s*pending_count[^)]*\+\+i", discard),
            "async rollback must requeue survivors in their original order")
    require(re.search(r"xQueueSend\(s_keyboard_event_queue,\s*&pending\[i\],\s*0\)", discard),
            "async rollback must requeue survivors without changing capacity")

    async_failure = re.search(
        r"if\s*\(\s*async_result\s*!=\s*LV_RESULT_OK\s*\)\s*\{(?P<body>.*?)\}",
        producer, re.S,
    )
    require(async_failure is not None,
            "lv_async_call failure must have an explicit rollback branch")
    if async_failure is not None:
        rollback = async_failure.group("body")
        require("discard_keyboard_event_from_queue(event)" in rollback,
                "lv_async_call failure must remove the queued event")
    require(re.search(r"discard_keyboard_event_from_queue\(event\).*free\(event\)", rollback, re.S),
                "lv_async_call failure must release the removed event")

    # Exercise the dangerous interleaving as a source-level contract.  The
    # host suite cannot link the ESP-IDF queue/LVGL task, so assert the lock
    # hand-off protocol that makes this schedule deterministic:
    #
    # producer: take -> enqueue -> lv_async_call (may block/return failure)
    #           -> rollback/free if needed -> give
    # consumer: take -> dequeue -> give -> UI work/free
    #
    # In particular, a callback that is released while the producer is still
    # waiting in lv_async_call must not consume the event which the producer
    # may subsequently roll back.
    producer_take = producer.index("xSemaphoreTake(s_keyboard_async_mutex")
    producer_send = producer.index("xQueueSend(s_keyboard_event_queue", producer_take)
    producer_async = producer.index("lv_async_call(process_keyboard_event_async", producer_send)
    producer_give = producer.index("xSemaphoreGive(s_keyboard_async_mutex)", producer_async)
    require(producer_take < producer_send < producer_async < producer_give,
            "producer mutex must span enqueue, lv_async_call, rollback, and return")
    require(rollback.count("free(event)") == 1,
            "failed lv_async_call must release the rejected snapshot exactly once")
    require("xSemaphoreGive(s_keyboard_async_mutex)" not in discard,
            "rollback must not release the producer mutex behind its back")

    # The rollback is a bounded remove-and-requeue operation.  These checks
    # cover the interleaving's second invariant: unrelated producers retain
    # their FIFO order and the queue never exceeds its declared capacity.
    require(re.search(r"xQueueCreate\(KEYBOARD_EVENT_QUEUE_CAPACITY\s*,\s*sizeof\(keyboard_event_context \*\)\)", source),
            "keyboard queue must use the declared bounded pointer capacity")
    require(discard.count("xQueueReceive(s_keyboard_event_queue") == 1,
            "rollback must drain the queue exactly once before rebuilding it")
    require(re.search(r"pending_count\s*<\s*KEYBOARD_EVENT_QUEUE_CAPACITY", discard),
            "rollback drain must be bounded by queue capacity")
    require(re.search(r"pending\[i\]\s*==\s*event\s*\)\s*continue", discard),
            "rollback must discard only the rejected pointer")
    require(re.search(r"for\s*\([^)]*i\s*=\s*0[^)]*<\s*pending_count[^)]*\+\+i", discard),
            "rollback must requeue survivors from oldest to newest")
    require(discard.count("xQueueSend(s_keyboard_event_queue") == 1,
            "rollback must requeue each survivor through the bounded queue")
    require("free(" not in discard,
            "rollback must not free survivors or the rejected pointer more than once")

    # Locate the async consumer and ensure it is UI-only: taking the display
    # lock there would deadlock because lv_async_call runs on the UI task.
    async_call = re.search(r"lv_async_call\s*\(([^;]+?)\)", producer, re.S)
    require(async_call is not None,
            "lv_async_call must receive a named UI consumer")
    candidates = re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\b", async_call.group(1))
    consumer_name = next((name for name in candidates
                          if re.search(r"\b" + re.escape(name) + r"\s*\(", source)), None)
    require(consumer_name is not None,
            "lv_async_call must receive a named UI consumer")
    consumer = function_body(source, consumer_name + "(")
    consumer_code = re.sub(r"/\*.*?\*/|//[^\n]*", "", consumer, flags=re.S)
    require("bsp_display_lock" not in consumer_code and
            "bsp_display_unlock" not in consumer_code,
            "async UI consumer must not take a nested display lock")
    consumer_take = consumer.index("xSemaphoreTake(s_keyboard_async_mutex")
    consumer_receive = consumer.index("xQueueReceive(s_keyboard_event_queue", consumer_take)
    consumer_give = consumer.index("xSemaphoreGive(s_keyboard_async_mutex)", consumer_receive)
    consumer_free = consumer.index("free(event)", consumer_give)
    require(consumer_take < consumer_receive < consumer_give < consumer_free,
            "consumer must dequeue/release the mutex before processing or freeing")
    require(consumer.count("free(event)") == 1,
            "successful callback must free its dequeued snapshot exactly once")

    # Preserve the existing input paths while changing only physical dispatch.
    require(re.search(r"else\s+if\s*\(\s*event->special_key\s*\)\s*\{\s*local_key\s*\(\s*event->special_key\s*\)\s*;\s*\}", consumer_code) is not None,
            "special keys in async consumer must be routed through local_key to preserve menu/modal selection")
    terminal_changed_body = function_body(source, "void terminal_changed(")
    require("local_key(LV_KEY_ENTER)" in terminal_changed_body,
            "virtual Enter in terminal_changed must route through local_key(LV_KEY_ENTER)")
    for required in ("virtual_keyboard_changed", "terminal_insert",
                     "terminal_changed", "lv_keyboard_set_textarea",
                     "s_local_shell.execute"):
        require(required in source, f"existing shell/SSH/virtual path missing: {required}")
    require("ssh_client_start" in source or "ssh_client_connect" in source,
            "existing shell/SSH/virtual path missing: SSH connection start")

    print("PASS: keyboard async-dispatch contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
