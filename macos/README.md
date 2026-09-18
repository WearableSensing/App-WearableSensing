# Wearable Sensing LSL for macOS

This guide explains how to build and run the `dsi2lsl` command-line application
on Apple Silicon and Intel Macs. The latest macOS-compatible DSI API required by
the application is bundled with the project.

The build uses the official liblsl v1.17.7 universal macOS framework.

## Requirements

- macOS 11 or later
- An internet connection during the first build
- Apple Command Line Tools

Install the Apple Command Line Tools from Terminal:

```bash
xcode-select --install
```

Apple may ask you to accept the Xcode and macOS SDK license the first time the
compiler runs. Follow the displayed instructions to accept it.

## Build

Open Terminal, change into the downloaded repository folder, and run:

```bash
chmod +x macos/build-macos.sh
./macos/build-macos.sh
```

The script detects the Mac architecture and creates either
`macos/dist-arm64/` or `macos/dist-x86_64/`.

## macOS security prompt

The first time the software runs, macOS may report that it cannot verify the
developer or check the software for malicious content. To approve it:

1. Attempt to run the application once.
2. Open **System Settings > Privacy & Security**.
3. Find the blocked-software message and click **Open Anyway**.
4. Authenticate when prompted, then run the application again.

Alternatively, run this command from the repository root:

```bash
xattr -dr com.apple.quarantine "macos/dist-$(uname -m)" vendor/dsi-api/1.21.3
```

This removes the quarantine attribute only from this project's build and DSI
API files. Do not disable Gatekeeper globally.

## Run

For information about connecting your headset to macOS, please contact
[Wearable Sensing Support](mailto:support@wearablesensing.com).

Find the headset's serial port:

```bash
ls /dev/cu.*
```

Common port names are:

- Wireless: `/dev/cu.DSIXX-XXXX`
- Wired: `/dev/cu.SLAB_USBtoUART`

Change into the generated folder:

```bash
cd "macos/dist-$(uname -m)"
```

Run the application, replacing the port with the exact value shown on the Mac:

```bash
./run-dsi2lsl.sh --port=/dev/cu.YOUR_DSI_PORT --lsl-stream-name=WS-default
```

 A successful connection displays `Streaming...`. Press `Control-C` to stop.

## Wireless reconnection

After stopping a wireless recording, restart the macOS Bluetooth service before
reconnecting:

```bash
sudo pkill bluetoothd
```

Enter the Mac login password when prompted. Terminal does not display password
characters while they are entered. The headset can remain powered on. Allow
Bluetooth to restart, reconnect the headset if necessary, and verify that its
`/dev/cu.DSIXX-XXXX` port is present before running the application again.

This step is not required for wired connections.