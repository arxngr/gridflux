.pragma library

function log(msg) {
    console.log("[GridFlux] " + msg)
}

function to_array(qList) {
    var arr = []
    for (var i = 0; i < qList.length; i++) {
        arr.push(qList[i])
    }
    return arr
}

function apply_layout(clients, area, depth, padding) {
    if (clients.length === 0) return
    
    if (clients.length === 1) {
        var client = clients[0]
        var x = area.x + padding
        var y = area.y + padding
        var width = Math.max(area.width - (padding * 2))
        var height = Math.max(area.height - (padding * 2))
        client.geometry = Qt.rect(x, y, width, height)
        log("Tiled: " + client.caption)
        return
    }
    
    var splitVertically = (depth % 2 === 0)
    var mid = Math.floor(clients.length / 2)
    
    if (splitVertically) {
        var leftWidth = Math.floor(area.width / 2)
        var rightWidth = area.width - leftWidth
        apply_layout(clients.slice(0, mid), 
                {x: area.x, y: area.y, width: leftWidth, height: area.height}, 
                depth + 1, padding)
        apply_layout(clients.slice(mid), 
                {x: area.x + leftWidth, y: area.y, width: rightWidth, height: area.height}, 
                depth + 1, padding)
    } else {
        var topHeight = Math.floor(area.height / 2)
        var bottomHeight = area.height - topHeight
        apply_layout(clients.slice(0, mid), 
                {x: area.x, y: area.y, width: area.width, height: topHeight}, 
                depth + 1, padding)
        apply_layout(clients.slice(mid), 
                {x: area.x, y: area.y + topHeight, width: area.width, height: bottomHeight}, 
                depth + 1, padding)
    }
}

function retile(workspace, padding, currentDesktop) {
    var clients = to_array(workspace.clientList()).filter(function(c) {
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
                   rc !== "spectacle" &&      // exclude Spectacle
                   rn !== "spectacle"
        })
    
    if (clients.length === 0) {
        return
    }
    
    var screenArea = workspace.clientArea(0, workspace.activeScreen, currentDesktop)
    
    apply_layout(clients, screenArea, 0, padding)
}
