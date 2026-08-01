# GridFlux 1.4.0

Exclude any app from the tiling engine — via the control panel, the CLI, or a hotkey — and it parks out of the way instead of being arranged. Exclusions are remembered across restarts.

## ✨ What's new

### Exclude apps from arrangement
Some apps you just don't want tiled — a music player, a chat window, a game launcher. You can now exclude them three ways:

- **Control panel** — open the new **Exclusions** dialog and pick any running app.
- **Hotkey** — press **Alt + E** on the focused window to exclude it (press again to bring it back).
- **CLI** — `gridflux-cli exclude add <app>` / `exclude remove <app>` / `exclude list`.

Excluded apps are moved to a dedicated, hidden workspace and left alone — no tiling, no border. Toggle one back and it returns to your current workspace, redraws its border, and the layout re-flows to fit it in. Your exclusions are saved to the config and restored automatically the next time GridFlux starts.

Maximized and full-screen windows are respected — an app is only parked once it's back to a normal window.

## 🐛 Fixes
- Fixed a timing issue where maximizing a window could occasionally snap it straight back into the tiled layout.

## 📦 Install
- **Recommended:** download `GridFlux-1.4.0.msi` and run it (or `msiexec /i GridFlux-1.4.0.msi /qn` for a silent install).
- **Microsoft Store / sideload:** `GridFlux-1.4.0.msix`.

Windows may show a SmartScreen prompt on unsigned builds — choose *More info → Run anyway*.

### Default hotkeys
- **Ctrl + Win + ← / →** — switch workspace
- **Alt + E** — exclude / restore the focused app

## 🔐 Checksums (SHA-256)
```
GridFlux-1.4.0.msi   f26023e5521d67f64e947f1c695d1bba0a0c8820771e55d54fe7009c0a34394e
GridFlux-1.4.0.msix  4fc17270ba53617704deddd5d3367ced74d9289a369ada39d0c681ea4f6ed24d
```

**Full changelog:** https://github.com/arxngr/gridflux/compare/v1.3.0...v1.4.0
