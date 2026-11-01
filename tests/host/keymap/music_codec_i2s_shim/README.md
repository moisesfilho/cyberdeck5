# Vendor I2S host harness shims

These declarations provide only the IDF 5.5 boundary needed to compile the
actual `esp_codec_dev` 1.6.2 C translation units. Vendor codec/data interfaces
and sample types come from managed component headers; I2S driver calls are
implemented by test-owned fakes with per-handle direction, peer, active state,
ordered handle-aware call trace, and one-shot per-channel failure injection.
The acceptance target covers per-handle active operations, duplex exact traces
and direction/stage failures, half-duplex preference/full-duplex fallback/no-peer
selection, channel state, and public codec open/close/retry with distinct formats.
Private vendor format snapshots have no public getter and are not inspected. Run
`make -C tests/host/keymap test_music_codec_i2s_vendor` for the green
characterization/control scenario against the local fork's corrected behavior.
This is not an upstream baseline: both focused targets compile and execute the
local fork, and neither runs the original unpatched package. Run the executable
acceptance contract with:

```sh
make -C tests/host/keymap test_music_codec_i2s_vendor_acceptance
```

It is intentionally separate from the aggregate and passes against the local
fork. The original upstream behavior was established by prior source inspection,
not by this harness; do not infer an upstream comparison from either target.
This is deterministic host behavior, not an ESP-IDF scheduler, DMA, I2C,
codec-register, or hardware validation.
