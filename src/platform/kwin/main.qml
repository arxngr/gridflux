import QtQuick 2.0
import org.kde.kwin 2.0
import "tiler.js" as Tiling

Item {
    id: root
    
    property int defaultPadding: 5
    property int maxWindowsPerWorkspace: 10
    property int minWindowSize: 100
    
    Timer {
        id: retileTimer
        interval: 100
        repeat: false
        onTriggered: {
            Tiling.retile(workspace, defaultPadding, minWindowSize, maxWindowsPerWorkspace, workspace.currentDesktop)
        }
    }
    
    function scheduleRetile() {
        retileTimer.restart()
    }
    
    Component.onCompleted: {
        Tiling.log("KWin script started")
        
        scheduleRetile()
        
        workspace.clientAdded.connect(function(client) {
            Tiling.log("New client: " + client.caption)
            scheduleRetile()
        })
        
        workspace.clientRemoved.connect(function(client) {
            Tiling.log("Removed client: " + client.caption)
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
        
        Tiling.log("Script initialization complete")
    }
}
