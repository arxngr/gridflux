param(
    [string]$BuildDirectory = 'build-vs',
    [string]$Configuration = 'Release',
    [string]$Version,
    [string]$WixPath
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repository $BuildDirectory))
$binaries = Join-Path $buildRoot $Configuration
$packages = Join-Path $buildRoot 'vcpkg_installed\x64-windows'

if (-not $Version) {
    $tag = & git -C $repository describe --tags --abbrev=0
    if ($LASTEXITCODE -ne 0) { throw 'Specify -Version when no release tag is available.' }
    $Version = $tag.TrimStart('v')
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw 'Version must contain three numeric components, such as 1.4.1.'
}
if (-not $WixPath) {
    $localWix = Join-Path $repository 'build\tools\wix4\wix.exe'
    if (Test-Path -LiteralPath $localWix) { $WixPath = $localWix }
    else { $WixPath = (Get-Command wix -ErrorAction Stop).Source }
}
$WixPath = (Resolve-Path -LiteralPath $WixPath).Path
$wixVersion = & $WixPath --version
if ($LASTEXITCODE -ne 0 -or $wixVersion -notmatch '^4\.') {
    throw 'This packager uses WiX 4. Install wix version 4.0.6 or pass -WixPath.'
}

$executables = @('gridflux.exe', 'gridflux-gui.exe', 'gridflux-cli.exe', 'gridflux-launcher.exe')
foreach ($name in $executables) {
    if (-not (Test-Path -LiteralPath (Join-Path $binaries $name))) {
        throw "$name is missing. Build the Visual Studio Release configuration first."
    }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $visualStudio) { throw 'Visual Studio C++ tools are required.' }
$compiler = Get-ChildItem -LiteralPath (Join-Path $visualStudio 'VC\Tools\MSVC') -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$dumpbin = Join-Path $compiler.FullName 'bin\Hostx64\x64\dumpbin.exe'
$runtime = Get-ChildItem -LiteralPath (Join-Path $visualStudio 'VC\Redist\MSVC') -Directory |
    Where-Object Name -Match '^\d+\.' | Sort-Object Name -Descending | Select-Object -First 1

# Use a fresh staging directory; never delete or repurpose a CMake build tree.
$artifactRoot = Join-Path $repository 'build\installer'
$stage = Join-Path $artifactRoot ('stage-' + [Guid]::NewGuid().ToString('N'))
$runtimeRoot = Join-Path $stage 'bin'
$schemaRoot = Join-Path $stage 'share\glib-2.0\schemas'
New-Item -ItemType Directory -Path $runtimeRoot, $schemaRoot -Force | Out-Null
foreach ($name in $executables) {
    Copy-Item -LiteralPath (Join-Path $binaries $name) -Destination $stage
}
$dlls = @(Get-ChildItem -LiteralPath $binaries -Filter '*.dll')
if (-not $dlls.Count) { throw 'The Release directory has no bundled dependency DLLs.' }
$dlls | Copy-Item -Destination $runtimeRoot
Get-ChildItem -LiteralPath (Join-Path $runtime.FullName 'x64') -Directory |
    Where-Object Name -Match '\.(CRT|OpenMP)$' |
    ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Filter '*.dll' } |
    Copy-Item -Destination $runtimeRoot

# GLib can load these helpers dynamically, outside the PE import graph.
$glibTools = Join-Path $packages 'tools\glib'
Get-ChildItem -LiteralPath $glibTools -Filter 'gspawn-win64-helper*.exe' |
    Copy-Item -Destination $runtimeRoot
$schemaSources = Join-Path $packages 'share\glib-2.0\schemas'
Get-ChildItem -LiteralPath $schemaSources -Filter '*.gschema.xml' |
    Copy-Item -Destination $schemaRoot
& (Join-Path $glibTools 'glib-compile-schemas.exe') --strict $schemaRoot
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $schemaRoot 'gschemas.compiled'))) {
    throw 'GTK schemas could not be compiled.'
}

# Read every import without executing binaries. Never ship unresolved DLLs.
$inspect = @(Get-ChildItem -LiteralPath $stage -Filter '*.exe') +
    @(Get-ChildItem -LiteralPath $runtimeRoot -File)
foreach ($binary in $inspect) {
    $imports = & $dumpbin /nologo /dependents $binary.FullName
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $($binary.Name)." }
    foreach ($line in $imports) {
        if ($line -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') {
            $dependency = $Matches[1]
            if ($dependency -match '^(api-ms-|ext-ms-)') { continue }
            $bundled = Test-Path -LiteralPath (Join-Path $runtimeRoot $dependency)
            $systemDll = $dependency -notmatch '^(msvcp|vcruntime|vcomp|concrt)\d+' -and
                (Test-Path -LiteralPath (Join-Path $env:SystemRoot "System32\$dependency"))
            if (-not $bundled -and -not $systemDll) {
                throw "$($binary.Name) imports an unbundled DLL: $dependency"
            }
        }
    }
}

$namespace = 'http://wixtoolset.org/schemas/v4/wxs'
$xml = [Xml.XmlDocument]::new()
$wix = $xml.CreateElement('Wix', $namespace)
$xml.AppendChild($wix) | Out-Null
$fragment = $xml.CreateElement('Fragment', $namespace)
$wix.AppendChild($fragment) | Out-Null
$directory = $xml.CreateElement('DirectoryRef', $namespace)
$directory.SetAttribute('Id', 'INSTALLDIR')
$fragment.AppendChild($directory) | Out-Null
$group = $xml.CreateElement('ComponentGroup', $namespace)
$group.SetAttribute('Id', 'GtkRuntime')
$fragment.AppendChild($group) | Out-Null
foreach ($file in Get-ChildItem -LiteralPath $runtimeRoot -File | Sort-Object Name) {
    $id = 'Runtime_' + ($file.Name -replace '[^A-Za-z0-9_]', '_')
    $component = $xml.CreateElement('Component', $namespace)
    $component.SetAttribute('Id', $id)
    $component.SetAttribute('Guid', '*')
    $component.SetAttribute('Bitness', 'always64')
    $entry = $xml.CreateElement('File', $namespace)
    $entry.SetAttribute('Source', $file.FullName)
    $entry.SetAttribute('KeyPath', 'yes')
    $component.AppendChild($entry) | Out-Null
    $directory.AppendChild($component) | Out-Null
    $reference = $xml.CreateElement('ComponentRef', $namespace)
    $reference.SetAttribute('Id', $id)
    $group.AppendChild($reference) | Out-Null
}
$runtimeXml = Join-Path $stage 'runtime.wxs'
$xml.Save($runtimeXml)
@'
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <clear />
    <add key="nuget.org" value="https://api.nuget.org/v3/index.json" />
  </packageSources>
</configuration>
'@ | Set-Content -LiteralPath (Join-Path $stage 'NuGet.Config') -Encoding UTF8

# Keep WiX extensions inside staging, rather than modifying a global tool cache.
Push-Location $stage
try {
    foreach ($extension in @('WixToolset.UI.wixext', 'WixToolset.Util.wixext')) {
        & $WixPath extension add "$extension/4.0.6"
        if ($LASTEXITCODE -ne 0) { throw "WiX extension $extension could not be downloaded." }
    }
    $output = Join-Path $artifactRoot "GridFlux-$Version-x64.msi"
    & $WixPath build -arch x64 -d "Version=$Version" -d "PackageRoot=$stage" `
        -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext `
        (Join-Path $repository 'gridflux.wxs') $runtimeXml -out $output -bindpath $repository
    if ($LASTEXITCODE -ne 0) { throw 'MSI generation failed.' }
    $hash = Get-FileHash -LiteralPath $output -Algorithm SHA256
    "$($hash.Hash)  $([IO.Path]::GetFileName($output))" |
        Set-Content -LiteralPath ($output + '.sha256') -Encoding ASCII
    Write-Output "MSI: $output"
    Write-Output "SHA256: $($hash.Hash)"
} finally {
    Pop-Location
}
