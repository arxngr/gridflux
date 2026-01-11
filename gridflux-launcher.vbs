' GridFlux Background Launcher
' Runs gridflux.exe completely hidden without any window

Set WshShell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

' Get the directory where this script is located
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)

' Path to gridflux.exe
gridfluxPath = scriptDir & "\gridflux.exe"

' Check if gridflux is already running
Set objWMI = GetObject("winmgmts:\\.\root\cimv2")
Set processes = objWMI.ExecQuery("SELECT * FROM Win32_Process WHERE Name = 'gridflux.exe'")

If processes.Count = 0 Then
    ' Not running, start it hidden
    WshShell.Run """" & gridfluxPath & """", 0, False
End If

Set WshShell = Nothing
Set fso = Nothing