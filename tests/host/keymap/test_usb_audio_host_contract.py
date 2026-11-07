#!/usr/bin/env python3
"""Structural contract for generic USB Audio Class host discovery."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
HOST = ROOT / "components/cyberdeck/src/platform/usb/cyberdeck_usb_audio_host.cpp"
HEADER = ROOT / "components/cyberdeck/include/platform/usb/cyberdeck_usb_audio_host.h"
SYSTEM_APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
HTTP = ROOT / "components/cyberdeck/src/apps/screenshot/screenshot_server.cpp"
BSP_CMAKE = ROOT / "components/m5stack_tab5/CMakeLists.txt"
BSP_USB = ROOT / "components/m5stack_tab5/src/bsp_usb.c"
MAIN_MANIFEST = ROOT / "main/idf_component.yml"


def main() -> int:
    host = HOST.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    apps = SYSTEM_APPS.read_text(encoding="utf-8")
    http = HTTP.read_text(encoding="utf-8")
    bsp = BSP_CMAKE.read_text(encoding="utf-8")
    bsp_usb = BSP_USB.read_text(encoding="utf-8")
    manifest = MAIN_MANIFEST.read_text(encoding="utf-8")
    checks = {
        "generic UAC driver dependency": 'espressif/usb_host_uac: "1.5.0"' in manifest,
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
    }
    failures = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(f"{'PASS' if passed else 'FAIL'}: {name}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
