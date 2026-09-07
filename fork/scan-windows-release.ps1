[CmdletBinding()]
param([Parameter(Mandatory = $true)][string[]]$Files)

$ErrorActionPreference = 'Stop'
$status = Get-MpComputerStatus
if (-not $status.AMServiceEnabled -or -not $status.AntivirusEnabled) {
    throw 'Microsoft Defender Antivirus is unavailable; release scan cannot pass.'
}
Update-MpSignature
$status = Get-MpComputerStatus
Write-Host "Defender engine: $($status.AMEngineVersion), signatures: $($status.AntivirusSignatureVersion)"

$platform = Join-Path $env:ProgramData 'Microsoft\Windows Defender\Platform'
# Platform\ holds a directory per installed platform version, and newer
# Defender platforms add unversioned entries such as CurrentVersion beside
# them. Only the versioned directories take part in choosing the newest.
$scanner = Get-ChildItem -Path "$platform\*\MpCmdRun.exe" -ErrorAction SilentlyContinue |
    ForEach-Object {
        $version = $null
        if ([version]::TryParse(($_.Directory.Name -replace '-.*$', ''), [ref]$version)) {
            [pscustomobject]@{ Version = $version; Path = $_.FullName }
        }
    } |
    Sort-Object Version -Descending |
    Select-Object -First 1 -ExpandProperty Path
if (-not $scanner) {
    $scanner = Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe'
}
if (-not (Test-Path -LiteralPath $scanner -PathType Leaf)) {
    throw 'MpCmdRun.exe is missing; release scan cannot pass.'
}
foreach ($file in $Files) {
    $path = (Resolve-Path -LiteralPath $file).Path
    $before = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    Write-Host "Scanning $([IO.Path]::GetFileName($path)): SHA256 $before"
    & $scanner -Scan -ScanType 3 -File $path -DisableRemediation
    if ($LASTEXITCODE -ne 0) {
        throw "Defender detected a threat or could not complete the scan (exit $LASTEXITCODE). Publication blocked."
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $before) {
        throw 'The scanned file changed; publication blocked.'
    }
}
