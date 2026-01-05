import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';
import Meta from 'gi://Meta';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

function apply_layout(windows, area, depth, padding) {
    if (windows.length === 0)
        return;

    if (windows.length === 1) {
        let w = windows[0];

        w.unmaximize(Meta.MaximizeFlags.BOTH);
        w.move_resize_frame(
            false,
            Math.round(area.x + padding),
            Math.round(area.y + padding),
            Math.round(Math.max(100, area.width - padding * 2)),
            Math.round(Math.max(100, area.height - padding * 2))
        );
        return;
    }

    let split_vertically = (depth % 2 === 0);
    let mid = Math.floor(windows.length / 2);

    if (split_vertically) {
        let left_w = Math.floor(area.width / 2);
        let right_w = area.width - left_w;

        apply_layout(
            windows.slice(0, mid),
            { x: area.x, y: area.y, width: left_w, height: area.height },
            depth + 1,
            padding
        );
        apply_layout(
            windows.slice(mid),
            { x: area.x + left_w, y: area.y, width: right_w, height: area.height },
            depth + 1,
            padding
        );
    } else {
        let top_h = Math.floor(area.height / 2);
        let bottom_h = area.height - top_h;

        apply_layout(
            windows.slice(0, mid),
            { x: area.x, y: area.y, width: area.width, height: top_h },
            depth + 1,
            padding
        );
        apply_layout(
            windows.slice(mid),
            { x: area.x, y: area.y + top_h, width: area.width, height: bottom_h },
            depth + 1,
            padding
        );
    }
}

function tile_current_workspace(padding = 5) {
    const workspace = global.workspace_manager.get_active_workspace();
    const windows = workspace.list_windows().filter(w =>
        w.get_window_type() === Meta.WindowType.NORMAL &&
        !w.minimized &&
        !w.skip_taskbar &&
        !w.is_fullscreen() &&
        w.allows_resize() &&
        w.allows_move()
    );

    if (windows.length === 0)
        return;

    const monitor_index = global.display.get_primary_monitor();
    const work_area = Main.layoutManager.getWorkAreaForMonitor(monitor_index);

    apply_layout(windows, work_area, 0, padding);
}

export default class GridFluxExtension extends Extension {
    enable() {
        this._signals = [];
        this._window_signals = new Map();
        this._retile_timeout = null;

        // Monitor window creation
        this._signals.push(
            global.display.connect('window-created', (display, window) => {
                this._connect_window_signals(window);
                this._schedule_tile();
            })
        );

        // Monitor workspace changes
        this._signals.push(
            global.workspace_manager.connect('active-workspace-changed', () => {
                this._schedule_tile();
            })
        );

        // Monitor existing windows
        const workspace = global.workspace_manager.get_active_workspace();
        workspace.list_windows().forEach(window => {
            this._connect_window_signals(window);
        });

        // Initial tile
        this._schedule_tile();
    }

    _connect_window_signals(window) {
        if (this._window_signals.has(window))
            return;

        const signals = [];

        // Monitor window unmanaged (closed)
        signals.push(
            window.connect('unmanaged', () => {
                this._disconnect_window_signals(window);
                this._schedule_tile();
            })
        );

        // Monitor minimize/unminimize
        signals.push(
            window.connect('notify::minimized', () => {
                this._schedule_tile();
            })
        );

        // Monitor workspace changes
        signals.push(
            window.connect('workspace-changed', () => {
                this._schedule_tile();
            })
        );

        this._window_signals.set(window, signals);
    }

    _disconnect_window_signals(window) {
        const signals = this._window_signals.get(window);
        if (signals) {
            signals.forEach(id => window.disconnect(id));
            this._window_signals.delete(window);
        }
    }

    _schedule_tile() {
        if (this._retile_timeout) {
            clearTimeout(this._retile_timeout);
        }

        this._retile_timeout = setTimeout(() => {
            tile_current_workspace(5);
            this._retile_timeout = null;
        }, 100);
    }

    disable() {
        // Clear timeout
        if (this._retile_timeout) {
            clearTimeout(this._retile_timeout);
            this._retile_timeout = null;
        }

        // Disconnect global signals
        this._signals.forEach(id => {
            try {
                global.display.disconnect(id);
            } catch (e) {
                try {
                    global.workspace_manager.disconnect(id);
                } catch (e2) {}
            }
        });
        this._signals = [];

        // Disconnect window signals
        this._window_signals.forEach((signals, window) => {
            signals.forEach(id => {
                try {
                    window.disconnect(id);
                } catch (e) {}
            });
        });
        this._window_signals.clear();
    }
}
