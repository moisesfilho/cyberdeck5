#!/usr/bin/env python3
"""Structural contract for generic USB Audio Class host discovery and playback."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
HOST = ROOT / "components/cyberdeck/src/platform/usb/cyberdeck_usb_audio_host.cpp"
HEADER = ROOT / "components/cyberdeck/include/platform/usb/cyberdeck_usb_audio_host.h"
SYSTEM_APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
HTTP = ROOT / "components/cyberdeck/src/apps/screenshot/screenshot_server.cpp"
BSP_CMAKE = ROOT / "components/m5stack_tab5/CMakeLists.txt"
BSP_USB = ROOT / "components/m5stack_tab5/src/bsp_usb.c"
MAIN_MANIFEST = ROOT / "main/idf_component.yml"
LOCAL_UAC = ROOT / "components/usb_host_uac/uac_host.c"
AUDIO_OUTPUT = ROOT / "components/cyberdeck/src/platform/audio/cyberdeck_audio_output.cpp"


def check_local_driver(driver, manifest):
    """The class driver is a local copy with two corrections of its own.

    The upstream 1.5.0 open() error path released the ring buffer only after
    deleting the interface, so a failed open (a headset that left the bus
    between enumeration and the call) dereferenced NULL and rebooted the Tab5,
    while the surviving path used the interface after it was freed.
    """
    checks = {
        # The dependency still pins the released version, but resolves locally.
        "generic UAC driver dependency": 'espressif/usb_host_uac:' in manifest and '"1.5.0"' in manifest,
        "class driver is resolved from the workspace": 'override_path: "../components/usb_host_uac"' in manifest,
        "local class driver present": driver is not None,
    }
    if driver is None:
        return checks

    open_error_path = driver[driver.find("esp_err_t uac_host_device_open("):]
    open_error_path = open_error_path[open_error_path.find("fail:"):]
    open_error_path = open_error_path[: open_error_path.find("\n}")]
    checks["open error path tolerates a missing interface"] = "if (uac_iface) {" in open_error_path
    checks["open error path releases before deleting"] = (
        open_error_path.find("vRingbufferDelete(uac_iface->ringbuf)") <
        open_error_path.find("uac_host_interface_delete(uac_iface)"))
    checks["open error path never reads a deleted interface"] = (
        open_error_path.rfind("uac_iface->ringbuf") <
        open_error_path.find("uac_host_interface_delete(uac_iface)"))
    return checks


def main() -> int:
    host = HOST.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    apps = SYSTEM_APPS.read_text(encoding="utf-8")
    http = HTTP.read_text(encoding="utf-8")
    bsp = BSP_CMAKE.read_text(encoding="utf-8")
    bsp_usb = BSP_USB.read_text(encoding="utf-8")
    manifest = MAIN_MANIFEST.read_text(encoding="utf-8")
    driver = LOCAL_UAC.read_text(encoding="utf-8") if LOCAL_UAC.is_file() else None
    audio = AUDIO_OUTPUT.read_text(encoding="utf-8")

    playback_start = host[host.find("bool playback_start("):]
    playback_start = playback_start[: playback_start.find("\n}")]

    checks = {
        "USB-A host BSP compiled": "src/bsp_usb.c" in bsp and "usb)" in bsp,
        "USB UAC TX only": "UAC_HOST_DRIVER_EVENT_TX_CONNECTED" in host,
        "no device identity filter": "VID ==" not in host and "PID ==" not in host,
        "bounded formats": "k_max_formats = 8" in header,
        "bounded descriptor strings": "k_max_string_bytes = 48" in header,
        "worker-owned descriptor queries": "uac_host_get_device_alt_param" in host and "worker_task" in host,
        "device presence is independent of the class driver": "usb_host_client_register" in host and
        "USB_HOST_CLIENT_EVENT_NEW_DEV" in host,
        "audio class version is read from descriptors": "bcd_adc" in host and "k_cs_interface_header" in host,
        "unsupported devices stop waiting": "state::unsupported" in host and "audio_spec >= 2" in host,
        "passive client is released before the host stops": host.find("usb_host_client_deregister") <
        host.find("uac_host_uninstall"),
        "idle USB tick is not fatal": "result == ESP_ERR_TIMEOUT" in bsp_usb,
        "supervised optional lifecycle": '"cyberdeck.usb_audio"' in apps and "stop_usb_audio_host" in apps,
        "local-only USB telemetry": '"/usb/audio"' in http and "is_local_peer(req)" in http,
        "cached telemetry only": "get_snapshot(value)" in http,
        # A paused player holds no UAC handle, so the removal is invisible to the
        # class driver: the bus itself has to be polled or the snapshot keeps
        # advertising a headset that is gone.
        "removal is detected without an open stream": "usb_host_device_addr_list_fill" in host and
        "address_present" in host,
        "worker retires a device that left the bus": "left the bus" in host and "forget_device()" in host,
        "stream is only opened for a present address": "address_present(value.address)" in playback_start,
        # Closing the driver interface is the owner's job and cannot happen
        # inside the driver callback.
        "disconnect callback defers the close": "event_kind::playback_lost" in host and
        "playback_stop();" in host,
        # A hot-unplug mid-stream must not abort the player.
        "audio output falls back after a lost USB stream": "start_route(rate, channels, volume, false, nullptr)" in audio,
        # Inserting or removing a headset pauses playback: presence comes from
        # the published snapshot (never from an unreadable port) and a failed
        # write is classified with the driver's own disconnect notification
        # instead of the write error alone.
        "presence is derived from the published view": "playback_presence()" in host and
        "state::stopped || value.status == state::starting || value.status == state::error" in host,
        "presence counts UAC headsets only": "value.audio_control || value.playback_streams != 0" in host,
        "disconnect notification is recorded atomically": "s_playback_lost.store(true" in host and
        "s_playback_lost.store(false" in host,
        "presence hook is installed for the audio route": "playback_device_present}" in host,
        "lost route is distinguishable from a write error": "route_lost = true" in audio and
        "usb_hooks.device_present()" in audio,
    }
    checks.update(check_local_driver(driver, manifest))

    failures = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(f"{'PASS' if passed else 'FAIL'}: {name}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
