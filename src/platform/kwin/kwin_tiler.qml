import QtQuick 2.0
import org.kde.kwin 2.0

Item {
    id: root
    
    function log(msg) { 
        print("[Gridflux] " + msg) 
    }
    
    function toArray(qList) {
        var arr = []
        for (var i = 0; i < qList.length; i++) {
            arr.push(qList[i])
        }
        return arr
    }
    
    function bspSplit(clients, area, depth) {
        if (clients.length === 0) return
        
        if (clients.length === 1) {
            var client = clients[0]
            client.geometry = Qt.rect(area.x, area.y, area.width, area.height)
            log("Tiled: " + client.caption)
            return
        }
        
        var splitVertically = (depth % 2 === 0)
        var mid = Math.floor(clients.length / 2)
        
        if (splitVertically) {
            var leftWidth = Math.floor(area.width / 2)
            var rightWidth = area.width - leftWidth
            bspSplit(clients.slice(0, mid), 
                    {x: area.x, y: area.y, width: leftWidth, height: area.height}, 
                    depth + 1)
            bspSplit(clients.slice(mid), 
                    {x: area.x + leftWidth, y: area.y, width: rightWidth, height: area.height}, 
                    depth + 1)
        } else {
            var topHeight = Math.floor(area.height / 2)
            var bottomHeight = area.height - topHeight
            bspSplit(clients.slice(0, mid), 
                    {x: area.x, y: area.y, width: area.width, height: topHeight}, 
                    depth + 1)
            bspSplit(clients.slice(mid), 
                    {x: area.x, y: area.y + topHeight, width: area.width, height: bottomHeight}, 
                    depth + 1)
        }
    }
    
    function retile() {
        var clients = toArray(workspace.clientList()).filter(function(c) {
            var rc = (c.resourceClass || "").toLowerCase()
            var rn = (c.resourceName || "").toLowerCase()

            return !c.minimized &&
                   c.desktop === workspace.currentDesktop &&
                   c.normalWindow &&          // only normal windows
                   !c.skipTaskbar &&          // exclude windows that skip taskbar (like tooltips, menus)
                   !c.modal &&                // exclude modal dialogs if needed
                   !c.fullScreen &&           // optionally exclude fullscreen windows
                   !c.dock &&                 // exclude panels/docks
                   !c.specialWindow &&
                   !([KWin.ToolTip, KWin.SplashScreen, KWin.Notification, KWin.Dock].includes(c.windowType)) &&
                   rc !== "spectacle" &&      // exclude Spectacle
                   rn !== "spectacle"
        })
        
        if (clients.length === 0) {
            return
        }
        
        var screenArea = workspace.clientArea(
            KWin.PlacementArea, 
            workspace.activeScreen, 
            workspace.currentDesktop
        )
        
        bspSplit(clients, screenArea, 0)
    }
    
    // Timer to debounce retiling (prevents too many rapid retiles)
    Timer {
        id: retileTimer
        interval: 100
        repeat: false
        onTriggered: retile()
    }
    
    function scheduleRetile() {
        retileTimer.restart()
    }
    
    Component.onCompleted: {
        retile()
        workspace.clientAdded.connect(function(client) {
            log("New window detected: " + client.caption)
            scheduleRetile()
        })
        
        workspace.clientRemoved.connect(function(client) {
            log("Window closed: " + client.caption)
            scheduleRetile()
        })
        
        workspace.clientMinimized.connect(function(client) {
            scheduleRetile()
        })
        
        workspace.clientUnminimized.connect(function(client) {
            scheduleRetile()
        })
        
        workspace.currentDesktopChanged.connect(function() {
            scheduleRetile()
        })
        
        workspace.clientActivated.connect(function(client) {
            if (client) {
                scheduleRetile()
            }
        })
        
        log("Automatic tiling enabled - all windows will be tiled automatically!")
    }
}
