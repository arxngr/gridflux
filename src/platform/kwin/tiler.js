.pragma library

function log(msg) {
  print("[Gridflux] " + msg);
}

// Convert QML list to JavaScript array
function toArray(qList) {
  var arr = [];
  for (var i = 0; i < qList.length; i++) {
    arr.push(qList[i]);
  }
  return arr;
}

// BSP split algorithm
function bspSplit(clients, area, depth) {
  if (clients.length === 0) return;

  if (clients.length === 1) {
    var client = clients[0];
    client.geometry = Qt.rect(area.x, area.y, area.width, area.height);
    log("Tiled: " + client.caption);
    return;
  }

  var splitVertically = depth % 2 === 0;
  var mid = Math.floor(clients.length / 2);

  if (splitVertically) {
    var leftWidth = Math.floor(area.width / 2);
    var rightWidth = area.width - leftWidth;
    bspSplit(
      clients.slice(0, mid),
      { x: area.x, y: area.y, width: leftWidth, height: area.height },
      depth + 1,
    );
    bspSplit(
      clients.slice(mid),
      {
        x: area.x + leftWidth,
        y: area.y,
        width: rightWidth,
        height: area.height,
      },
      depth + 1,
    );
  } else {
    var topHeight = Math.floor(area.height / 2);
    var bottomHeight = area.height - topHeight;
    bspSplit(
      clients.slice(0, mid),
      { x: area.x, y: area.y, width: area.width, height: topHeight },
      depth + 1,
    );
    bspSplit(
      clients.slice(mid),
      {
        x: area.x,
        y: area.y + topHeight,
        width: area.width,
        height: bottomHeight,
      },
      depth + 1,
    );
  }
}

// Get tileable clients for current desktop
function getTileableClients(workspace) {
  return toArray(workspace.clientList()).filter(function (c) {
    return (
      !c.minimized &&
      c.desktop === workspace.currentDesktop &&
      !c.dock &&
      !c.specialWindow &&
      c.normalWindow
    );
  });
}

// Main retile function
function retile(workspace, KWin) {
  var clients = getTileableClients(workspace);

  if (clients.length === 0) {
    return;
  }

  var screenArea = workspace.clientArea(
    KWin.PlacementArea,
    workspace.activeScreen,
    workspace.currentDesktop,
  );

  bspSplit(clients, screenArea, 0);
}

// Setup event handlers
function setupHandlers(workspace, retileCallback) {
  workspace.clientAdded.connect(function (client) {
    log("New window detected: " + client.caption);
    retileCallback();
  });

  workspace.clientRemoved.connect(function (client) {
    log("Window closed: " + client.caption);
    retileCallback();
  });

  workspace.clientMinimized.connect(function (client) {
    retileCallback();
  });

  workspace.clientUnminimized.connect(function (client) {
    retileCallback();
  });

  workspace.currentDesktopChanged.connect(function () {
    retileCallback();
  });

  workspace.clientActivated.connect(function (client) {
    if (client) {
      retileCallback();
    }
  });

  log("Automatic tiling enabled - all windows will be tiled automatically!");
}
