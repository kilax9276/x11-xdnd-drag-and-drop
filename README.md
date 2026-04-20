# x11-xdnd-drag-and-drop

A lightweight command-line utility written in C++ for simulating file drag-and-drop to X11 applications using the XDND protocol.

This project is useful when you need to programmatically drop one or more files into a GUI application at specific screen coordinates. It creates a temporary X11 window, becomes the owner of the XDND selection, resolves the target window under the given coordinates, and sends the required XDND events to complete the drop operation.

## Features

- Works with X11 and the XDND protocol
- Accepts one or more files as input
- Converts file paths into `file://` URIs
- Detects the deepest visible window under the given screen coordinates
- Resolves XDND-aware windows and XDND proxy targets
- Sends `XdndEnter`, `XdndPosition`, and `XdndDrop`
- Responds to selection requests with `text/uri-list`, `UTF8_STRING`, and `text/plain`

## Requirements

- Linux
- X11 session
- C++17-compatible compiler
- Xlib development package

## Build

Compile with:

```bash
g++ -std=c++17 xdnd_send.cpp -o xdnd_send -lX11
```

If X11 development headers are missing, install them first.

### Debian / Ubuntu

```bash
sudo apt install libx11-dev
```

### Arch Linux

```bash
sudo pacman -S libx11
```

### Fedora

```bash
sudo dnf install libX11-devel
```

## Usage

```bash
./xdnd_send <x> <y> <file1> [file2 ...]
```

### Arguments

- `x` — X coordinate on the screen
- `y` — Y coordinate on the screen
- `file1 ...` — one or more existing files to drop

## Example

```bash
./xdnd_send 840 460 ./example.txt ./image.png
```

This command attempts to drop `example.txt` and `image.png` onto the window located at screen position `840,460`.

## How it works

1. Opens the current X11 display
2. Creates a tiny temporary source window
3. Registers itself as the owner of the XDND selection
4. Locates the deepest visible window under the specified coordinates
5. Resolves the actual XDND target window or proxy
6. Sends XDND enter and position events
7. Waits for the target to accept the drop
8. Sends the drop event
9. Serves the file list as a `text/uri-list` selection
10. Waits for completion confirmation

## Notes

- This tool works only in an X11 session.
- It does not support Wayland directly.
- Target applications must support XDND.
- Coordinates must match the actual screen position of the target window.

## GitHub Short Description

A lightweight C++ utility for simulating file drag-and-drop to X11 applications via XDND.

## Copyright

Copyright (c) 2026 @kilax9276 Kolobov Aleksei

## License

This project is released under the MIT License. See the `LICENSE` file for details.
