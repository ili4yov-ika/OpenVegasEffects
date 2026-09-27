# Compile-time inventory: remove only shipped files, then empty directories.
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Output,
    [switch]$RequireVlc
)
$ErrorActionPreference = 'Stop'
$sourceDirectory = Get-Item -LiteralPath $Source
if (-not $sourceDirectory.PSIsContainer -or
    ($sourceDirectory.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
    throw 'Payload source must be a directory, not a file, link or junction.'
}
$payloadRoot = $sourceDirectory.FullName.TrimEnd('\')
function NsisLiteral([string]$Value) {
    return $Value.Replace('$', '$$').Replace('"', '$\"')
}
$pendingDirectories = New-Object 'System.Collections.Generic.Queue[string]'
$payloadFiles = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
$payloadDirectories = New-Object 'System.Collections.Generic.List[System.IO.DirectoryInfo]'
$pendingDirectories.Enqueue($payloadRoot)
while ($pendingDirectories.Count -gt 0) {
    $directory = $pendingDirectories.Dequeue()
    foreach ($entry in Get-ChildItem -LiteralPath $directory -Force) {
        if ($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            throw "Payload must not contain links or junctions: $($entry.FullName)"
        }
        if ($entry.PSIsContainer) {
            $payloadDirectories.Add($entry)
            $pendingDirectories.Enqueue($entry.FullName)
        } else { $payloadFiles.Add($entry) }
    }
}
if ($RequireVlc) {
    foreach ($library in @('libvlc.dll', 'libvlccore.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $payloadRoot "vlc\$library") -PathType Leaf)) {
            throw "VLC runtime library is missing: $library"
        }
    }
    $pluginPrefix = (Join-Path $payloadRoot 'vlc\plugins') + '\'
    if (-not ($payloadFiles | Where-Object { $_.FullName.StartsWith($pluginPrefix, [StringComparison]::OrdinalIgnoreCase) -and $_.Extension -eq '.dll' })) {
        throw 'VLC decoder/capture plugins are missing from the payload.'
    }
}
$lines = New-Object 'System.Collections.Generic.List[string]'
$lines.Add('!macro OV_INSTALL_PAYLOAD')
foreach ($file in ($payloadFiles | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($payloadRoot.Length + 1)
    $parent = Split-Path -Path $relative -Parent
    $destination = '$INSTDIR'
    if ($parent) { $destination += '\' + (NsisLiteral $parent) }
    $lines.Add('  SetOutPath "' + $destination + '"')
    # File's source is a compile-time path; $ escaping applies only to its destination.
    $lines.Add('  File "/oname=' + (NsisLiteral $file.Name) + '" "' + $file.FullName + '"')
}
$lines.Add('!macroend')
$lines.Add('!macro OV_UNINSTALL_PAYLOAD')
foreach ($file in ($payloadFiles | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($payloadRoot.Length + 1)
    $lines.Add('  Delete "$INSTDIR\' + (NsisLiteral $relative) + '"')
}
foreach ($directory in ($payloadDirectories | Sort-Object { $_.FullName.Length } -Descending)) {
    $relative = $directory.FullName.Substring($payloadRoot.Length + 1)
    $lines.Add('  RMDir "$INSTDIR\' + (NsisLiteral $relative) + '"')
}
$lines.Add('!macroend')
[System.IO.File]::WriteAllLines($Output, $lines, (New-Object System.Text.UTF8Encoding($true)))
