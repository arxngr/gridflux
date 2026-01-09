# Windows Platform Quick Reference

## File Locations

| File | Purpose |
|------|---------|
| `src/platform/windows/platform.h` | Windows platform interface |
| `src/platform/windows/platform.c` | Windows implementation |
| `src/platform/windows/ipc.c` | Named pipes IPC (already exists) |
| `CMakeLists.txt` | Cross-platform build configuration |
| `src/config.c` | Cross-platform config paths |
| `build-windows.bat` | Batch build script |
| `build-windows.ps1` | PowerShell build script |
| `BUILDING_WINDOWS.md` | Detailed build guide |
| `WINDOWS_IMPLEMENTATION.md` | Technical documentation |

## Key Functions

### Initialization
```c
gf_error_code_t gf_platform_init(gf_platform_interface_t *platform, gf_display_t *display)
// Returns: GF_SUCCESS on success
// Note: *display is set to NULL on Windows (not needed)
```

### Window Enumeration
```c
gf_error_code_t gf_platform_get_windows(
    gf_display_t display,           // NULL on Windows
    gf_workspace_id_t *workspace_id,// Ignored on Windows
    gf_window_info_t **windows,     // Output: array of windows
    uint32_t *count                 // Output: number of windows
)
```

### Window Management
```c
// Geometry operations
gf_error_code_t gf_platform_set_window_geometry(
    gf_display_t display,
    gf_native_window_t window,      // HWND on Windows
    const gf_rect_t *geometry,
    gf_geometry_flags_t flags,
    gf_config_t *cfg
)

// Window state
gf_error_code_t gf_platform_minimize_window(gf_display_t display, gf_native_window_t window)
gf_error_code_t gf_platform_unminimize_window(gf_display_t display, gf_native_window_t window)
gf_error_code_t gf_platform_unmaximize_window(gf_display_t display, gf_native_window_t window)
```

### Queries
```c
bool gf_platform_is_window_valid(gf_display_t display, gf_native_window_t window)
bool gf_platform_is_window_excluded(gf_display_t display, gf_native_window_t window)
gf_window_id_t gf_platform_active_window(gf_display_t display)
gf_error_code_t gf_platform_get_screen_bounds(gf_display_t display, gf_rect_t *bounds)
```

## API Mapping: Windows to Platform Interface

| Concept | Windows API |
|---------|------------|
| Window Handle | HWND |
| Window Rect | RECT (x, y, right, bottom) |
| Display | NULL (not needed) |
| Workspace | Single (ID 0) |
| Window List | GetTopWindow/GetNextWindow |
| Window Geometry | GetWindowRect/MoveWindow |
| Window State | IsZoomed, IsIconic, ShowWindow |
| Active Window | GetForegroundWindow |
| Screen Bounds | GetSystemMetrics + AppBar |

## Building

### With batch script (cmd.exe):
```cmd
cd gridflux
build-windows.bat
cd build
start Release\gridflux.exe
```

### With PowerShell:
```powershell
cd gridflux
.\build-windows.ps1 -BuildType Release
cd build
.\Release\gridflux.exe
```

### Manual CMake (MSVC):
```powershell
cd gridflux
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
.\Release\gridflux.exe
```

## Configuration

**Config File Location**: `%APPDATA%\gridflux\config.json`

**Example Path**: `C:\Users\YourName\AppData\Roaming\gridflux\config.json`

**Development Mode** (from project directory):
- CMake: `-DGF_DEV_MODE=ON`
- Looks for: `./config.json`

## Common Issues & Fixes

### Issue: DLL not found (json-c.dll)
**Fix**: 
- Add json-c lib folder to PATH
- Or rebuild with static linking: `-DBUILD_SHARED_LIBS=OFF`

### Issue: HWND is invalid after enumeration
**Fix**: 
- Verify window with `gf_platform_is_window_valid()` before operations
- Windows may close unexpectedly

### Issue: Window won't move to specified geometry
**Fix**: 
- Unmaximize first: `gf_platform_unmaximize_window()`
- Check window is not excluded by `gf_platform_is_window_excluded()`

### Issue: System/notification windows being managed
**Fix**: 
- These are filtered by `gf_platform_is_window_excluded()`
- Check DWM_WINDOW_ATTRIBUTE for cloaking

### Issue: High DPI scaling looks wrong
**Fix**: 
- Current implementation uses screen-space pixels
- Future: May need DPI-aware manifest

## Architecture Diagram

```
User Application (main.c)
    |
    v
Window Manager (window_manager.c)
    |
    v
Platform Interface (platform.h)
    |
    +-- Unix/Linux (platform/unix/)
    |       X11 API -> XLib
    |
    +-- Windows (platform/windows/)
    |       Windows API -> user32.dll, dwmapi.dll, shell32.dll
    |
    +-- macOS (platform/macos/)
            Cocoa API -> Cocoa.framework

IPC Layer
    |
    +-- Unix/Linux: Unix domain sockets
    +-- Windows: Named pipes (\\.\pipe\gridflux_<sessionid>)
```

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Enumerate windows | O(n) | GetTopWindow/GetNextWindow traversal |
| Move window | O(1) | Single MoveWindow call |
| Get window rect | O(1) | GetWindowRect |
| Check if excluded | O(1)* | DWM attribute + state checks |
| Get screen bounds | O(m) | m = number of windows (for strut) |

*O(n) if checking multiple state atoms

## Debug Output

Enable debug logging:
```bash
# Environment variable
set GF_LOG_LEVEL=DEBUG

# Or compile with
-DGF_DEBUG=ON
```

Log locations:
- Console output (stderr)
- Windows Event Viewer (future)

## Testing Commands

```powershell
# List all managed windows
.\gridflux-cli.exe list

# List workspace info
.\gridflux-cli.exe info

# Monitor window events
.\gridflux-cli.exe monitor

# Manual window tiling (if implemented)
.\gridflux-cli.exe tile
```

## Limitations & Workarounds

| Limitation | Reason | Workaround |
|-----------|--------|-----------|
| Single workspace | Undocumented virtual desktop API | Use Windows+Ctrl+D manually |
| No window snapping animation | DWM API limitation | Windows 11 snap layouts may help |
| Taskbar conflicts | System reserved area | Use screen bounds calculation |
| Certain UWP apps ignore moves | API restrictions | Some apps (Store, etc.) can't be tiled |

## Performance Tips

1. **Minimize window enumeration frequency**: Cache window list when possible
2. **Batch window operations**: Group multiple MoveWindow calls
3. **Use IsWindow() before operations**: Prevent crashes from invalid handles
4. **Monitor memory usage**: Large window counts may impact performance

## Future Enhancements

See WINDOWS_IMPLEMENTATION.md for planned features

## Testing Checklist

- [ ] Builds without errors (MSVC and MinGW)
- [ ] Enumerates all visible windows
- [ ] Excludes system windows and toolbars
- [ ] Moves windows to specified positions
- [ ] Respects taskbar positioning
- [ ] Handles window close/open events
- [ ] IPC communication works
- [ ] Configuration loading works
- [ ] No memory leaks over extended use
- [ ] Handles high DPI correctly

---

For detailed information, see:
- BUILDING_WINDOWS.md - Build instructions
- WINDOWS_IMPLEMENTATION.md - Technical details
- src/platform/windows/platform.h - Function declarations
- src/platform/windows/platform.c - Implementation source
