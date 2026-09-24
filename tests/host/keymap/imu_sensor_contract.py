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


def main() -> int:
    source = IMU.read_text(encoding="utf-8")
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
