#!/usr/bin/env python3
"""Regression contract for the Sensor Hub based initial IMU sample.

Kept separate from boot ordering so IMU sensor-source assertions cannot be
misreported as an SD/UI boot-order failure. The Sensor Hub handle is retained:
`sensor_handle_t` is required by `bsp_sensor_init`.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
IMU = ROOT / "components/cyberdeck/src/platform/sensors/imu_reader.cpp"


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


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


def require_before(source: str, first: str, second: str,
                   message: str, failures: list[str]) -> None:
    require(first in source and second in source and source.index(first) < source.index(second),
            message, failures)


def main() -> int:
    source = IMU.read_text(encoding="utf-8")
    start = function_body(source, 'extern "C" esp_err_t imu_reader_start(')
    stop = function_body(source, 'extern "C" void imu_reader_stop(void)')
    failures: list[str] = []
    require("imu_acquire_acce(" not in source,
            "IMU startup must not use the generic direct-read path", failures)
    require(re.search(r"void\s+sensor_event_handler\s*\(", source) is not None,
            "startup must define a Sensor Hub callback", failures)
    require("SENSOR_ACCE_DATA_READY" in source,
            "callback must gate accelerometer-ready events", failures)
    require("s_first_sample_data = data->acce;" in source,
            "callback must copy the first accelerometer payload", failures)
    require("xSemaphoreGive(s_first_sample_sem)" in source,
            "callback must release the bounded startup wait", failures)
    require("iot_sensor_handler_register(" in source and "iot_sensor_start(" in source,
            "startup must register before starting the Sensor Hub stream", failures)
    require("INITIAL_SAMPLE_TIMEOUT_TICKS" in source and "xSemaphoreTake(" in source,
            "startup must wait with a bounded timeout", failures)

    # TEST-HOST-BOOT-01: the callback may still be in flight after the bounded
    # first-sample wait.  The semaphore therefore has static storage and must
    # never be deleted by either the timeout path or teardown.
    require("StaticSemaphore_t s_first_sample_storage" in source,
            "first-sample semaphore must have static backing storage", failures)
    require("xSemaphoreCreateBinaryStatic(&s_first_sample_storage)" in source,
            "startup must create the first-sample semaphore statically", failures)
    require("vSemaphoreDelete(s_first_sample_sem)" not in source and
            "vSemaphoreDelete(sem_to_delete)" not in source,
            "timeout/teardown must not destroy a semaphore reachable by the callback", failures)
    require("xSemaphoreTake(s_first_sample_sem, 0)" in start,
            "each init generation must drain a stale first-sample token", failures)
    require("s_first_sample_captured.store(false" in start,
            "each init generation must reset first-sample capture state", failures)

    # Sensor/handler/timer ownership is explicit.  Every post-acquisition
    # failure must roll back what this start call owns; stop must be repeatable.
    require_before(start, "bsp_sensor_init", "iot_sensor_handler_register",
                   "sensor must be initialized before its handler is registered", failures)
    require_before(start, "iot_sensor_handler_register", "iot_sensor_start",
                   "handler must be registered before the sensor starts", failures)
    require("iot_sensor_delete(sensor);" in start,
            "partial init must delete the local sensor handle on failure", failures)
    require("iot_sensor_handler_unregister(sensor, s_sensor_handler);" in start,
            "sensor-start failure must unregister the owned handler", failures)
    require("s_sensor = sensor;" in start,
            "sensor ownership must be published only after sensor start succeeds", failures)
    require("s_rotation_timer = lv_timer_create" in start and
            "if (s_rotation_timer == nullptr)" in start and
            "imu_reader_stop();" in start,
            "timer allocation failure must roll back the started sensor", failures)
    require("if (s_sensor != nullptr || s_rotation_timer != nullptr)" in start,
            "duplicate start must fail closed while owned resources remain", failures)
    require("if (s_rotation_timer != nullptr)" in stop and
            "s_rotation_timer = nullptr;" in stop,
            "stop must delete and clear the owned LVGL timer", failures)
    require_before(stop, "lv_timer_del(s_rotation_timer)", "iot_sensor_stop(s_sensor)",
                   "timer must be torn down before sensor callbacks are stopped", failures)
    require_before(stop, "iot_sensor_stop(s_sensor)", "iot_sensor_handler_unregister",
                   "sensor stream must stop before handler unregister", failures)
    require_before(stop, "iot_sensor_handler_unregister", "iot_sensor_delete(s_sensor)",
                   "handler must be unregistered before sensor deletion", failures)
    require("s_sensor = nullptr;" in stop and "s_display = nullptr;" in stop,
            "repeated teardown must clear all published IMU ownership", failures)
    require("LV_DISPLAY_ROTATION_0" in source and "orientation_from_accel" in source,
            "initial orientation must use a safe rotation-0 fallback and mapping", failures)
    require(re.search(r"orientation_set_current\(\s*initial_rotation\s*\)", source) is not None,
            "initial orientation must seed debounced state", failures)
    require("s_target_rotation.store(static_cast<int>(initial_rotation)," in source,
            "initial orientation must seed the later-rotation target", failures)
    require(re.search(r"lv_display_set_rotation\(\s*display\s*,\s*initial_rotation\s*\)", source) is not None,
            "initial orientation must be applied before UI creation", failures)
    require(re.search(r"orientation_update\(\s*data->acce\.x\s*,\s*data->acce\.y\s*,\s*data->acce\.z\s*\)", source) is not None,
            "later samples must continue through orientation_update", failures)
    require(re.search(r"target\s*!=\s*lv_display_get_rotation\(\s*display\s*\)", source) is not None,
            "later rotation must remain conditional", failures)
    require(re.search(r"lv_display_set_rotation\(\s*display\s*,\s*target\s*\)", source) is not None,
            "later rotation must still be applied by the timer", failures)
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: Sensor Hub initial sample and later rotation contracts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
