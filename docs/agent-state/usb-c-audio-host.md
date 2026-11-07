# usb-c-audio-host

- Status: completed
- Date: 2026-11-07
- Risk profile: high
- Approved plan: none; user asked to investigate and implement only if feasible on stock hardware

## Objective and criteria

Determine whether the existing UAC USB-C headphone can connect directly to the Tab5 USB-C port, using online and local evidence; implement only if the stock board supports the required USB host role and power. No implementation was justified by the evidence.

## Result

Do not implement USB host firmware for the Tab5 USB-C port. The official M5Stack schematic linked from the Tab5 documentation shows 5.1 kΩ pull-downs from both CC pins to ground (Rd, sink/UFP role), VBUS connected to `USB_5VIN`, and data lines named `USBIN_DP/DM`. This is not evidence of a Type-C source path to provide VBUS to the headphone. The ESP32-P4's OTG controllers cannot replace missing Type-C source/CC and power hardware. Continue using USB-A host with a data-capable USB-A-male to USB-C-female adapter.

## Changes

No production, test, or project documentation changes. Planner note: `docs/agent-state/usb-c-audio-host/planner.md`.

## Validation

- Online: opened official M5Stack Tab5 documentation and downloaded the linked official schematic PDF `https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf`; inspected sheet 4. It identifies J8 as `USB_C_16P_Horizontal`, CC1/CC2 with R52/R53 5.1 kΩ to GND, VBUS as `USB_5VIN`, and D+/D− as `USBIN_DP/DM`.
- Local source inspection: `code-map.md:2842-2853`, `components/m5stack_tab5/src/bsp_usb.c`, `components/m5stack_tab5/include/bsp/m5stack_tab5.h`; current host initialization uses BSP USB-A host path and the separate USB-A power enable.
- Online capability references: official BSP README says USB-A (Host) and USB-C (USB 2.0 OTG); Espressif USB host documentation describes ESP32-P4 OTG controllers, but controller capability alone does not establish the board-level Type-C source circuitry.
- No code tests, firmware build, flash, or electrical/device test were run; no firmware changes were made. `HEAD` at discovery: `ab896559c4df62c12b7584bdda2c1d083b363cd9` (`develop`).

## Decisions

The official schematic resolves the uncertainty that remained in the initial online research. Recommendation applies to the stock Tab5 design represented by that schematic. Do not inject 5 V into the USB-C port or attempt role switching through firmware. A hardware modification/accessory providing compliant Type-C source behavior is a separate design and approval scope.

## Residual risks

No electrical measurement was made on the user's physical unit; production revision correspondence was not independently read from the device. This does not alter the conclusion for the documented stock schematic. USB-A plus the existing data/power adapter remains the supported route. A powered active adapter/hub could be investigated separately if a cable-free physical arrangement is desired.

## References

- Planner note: `docs/agent-state/usb-c-audio-host/planner.md`.
- Official Tab5 schematic page: https://docs.m5stack.com/en/core/Tab5?id=schematics
- Official schematic PDF: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf
- Official BSP README: https://raw.githubusercontent.com/espressif/esp-bsp/master/bsp/m5stack_tab5/README.md
- Espressif USB host docs for ESP32-P4: https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html
