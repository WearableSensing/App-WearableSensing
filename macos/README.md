# macOS CLI build

This builds the tested adaptive-backfill `dsi2lsl` CLI for the Mac on which the
script is run.

## Included versions

- DSI API **v1.21.3**
  - The required public interface, loader, and compiled macOS libraries are
    included in `vendor/dsi-api/1.21.3`.
  - Includes the Apple Silicon wired-USB 921600-baud fix from v1.21.2.
- liblsl **v1.17.7**
  - Uses the official universal macOS framework (Apple Silicon + Intel).
- LSL timestamping: **Wearable Sensing adaptive backfill**
  - Nine samples are buffered per chunk.
  - One `lsl_local_clock()` value anchors the end of each chunk.
  - The nine timestamps are distributed evenly from the previous chunk's
    endpoint to the new endpoint.
  - `HW_Timestamp` is included as the final channel for validation.

## Build

On the Mac, open Terminal in the repository and run:

```bash
chmod +x macos/build-macos.sh
./macos/build-macos.sh
```

The script detects `arm64` or `x86_64`, selects the matching bundled DSI API
library, downloads the universal liblsl framework, and creates:

```text
macos/dist-arm64/
```

or:

```text
macos/dist-x86_64/
```

## Run

Find the headset's serial port:

```bash
ls /dev/cu.*
```

Then run from the generated folder:

```bash
cd macos/dist-arm64
./run-dsi2lsl.sh --port=/dev/cu.YOUR_DSI_PORT --lsl-stream-name=WS-default
```

Use `dist-x86_64` instead on an Intel Mac.

The wrapper changes into the output directory before starting the program so
the DSI dynamic library can be found reliably.
