# angel-foto

A fast, minimal photo viewer for Windows. Built to fix the pain points of the default Photos app.

## Features

- **Fast** - Hardware-accelerated rendering with Direct2D
- **Auto-fit** - Images automatically fit to window size
- **Rapid browsing** - Hold arrow keys to quickly scan through photos
- **Animated GIF** - Full playback with pause/play support
- **Native look** - Dark mode support, follows system theme

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Left/Right | Previous/Next image |
| Home/End | First/Last image |
| Space | Pause/play GIF |
| F | Fit to window |
| 1 | Actual size (100%) |
| +/- or Scroll | Zoom in/out |
| Delete | Delete file (recycle bin) |
| F11 | Toggle fullscreen |
| Esc | Exit fullscreen / Close |

## Building

Requires:
- Visual Studio 2022+ with C++ workload
- CMake 3.20+
- Windows SDK

```powershell
cmake -B build -A x64
cmake --build build --config Release
```

Output: `build\Release\angel-foto.exe`

## Usage

```powershell
angel-foto.exe "C:\path\to\image.jpg"
```

Or drag-drop an image onto the executable.

## Tech Stack

- C++20
- Win32 API
- Direct2D / Direct3D 11
- Windows Imaging Component (WIC)

## License

MIT
