# Build, sign and publish one fork release for Windows.
#
#   fork\release.ps1 -Counter 4
#   fork\release.ps1 -Counter 4 -NoPublish
#
# The Windows half of fork/release.sh; keep the two in step. The counter is
# the fork's build number within one upstream version: bump it to ship a
# build between two upstream releases, reset it to 1 after a rebase onto a
# newer upstream. See Telegram/SourceFiles/fork/build_counter.h.
#
# The signing key never leaves this machine. Everything that needs it happens
# here, so the update server only ever receives an already-signed package and
# a compromise there cannot produce an update a client would accept.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][int]$Counter,
    [switch]$NoPublish
)

$ErrorActionPreference = 'Stop'

function Fail($message) {
    Write-Host "[ERROR] $message" -ForegroundColor Red
    exit 1
}

$root = (git rev-parse --show-toplevel)
if (-not $root) { Fail "not inside a git repository" }
Set-Location $root

$keysDir = if ($env:SEEGRAM_KEYS_DIR) { $env:SEEGRAM_KEYS_DIR } else { "$HOME\seegram-update-keys" }
$keyId   = if ($env:SEEGRAM_KEY_ID) { $env:SEEGRAM_KEY_ID } else { "sg-2026a" }
$server  = if ($env:SEEGRAM_UPDATE_SERVER) { $env:SEEGRAM_UPDATE_SERVER } else { "root@REDACTED-HOST" }
$sshKey  = if ($env:SEEGRAM_SSH_KEY) { $env:SEEGRAM_SSH_KEY } else { "$HOME\.ssh\seegram_updates" }
$serverRoot  = "/var/www/desktop.see.tg"
$platformKey = "win64"
$buildDir    = "out\Release"

# ------------------------------------------------------------------ preflight

if ($Counter -lt 1) { Fail "the counter must be 1 or greater" }
if ((git status --porcelain --untracked-files=no)) {
    Fail "working tree is dirty - a release must be reproducible."
}
if (-not (Test-Path "$keysDir\release-private.pem")) {
    Fail "signing key missing at $keysDir\release-private.pem"
}
if (-not $env:SEEGRAM_API_ID)   { Fail "set SEEGRAM_API_ID" }
if (-not $env:SEEGRAM_API_HASH) { Fail "set SEEGRAM_API_HASH" }

$versionHeader = "Telegram\SourceFiles\core\version.h"
$baseMatch = Select-String -Path $versionHeader -Pattern 'AppVersion = (\d+);'
if (-not $baseMatch) { Fail "could not read AppVersion from $versionHeader" }
$base = [int64]$baseMatch.Matches[0].Groups[1].Value

# The client compares (base << 32 | counter), see Core::RunningUpdateVersion.
$version = ($base -shl 32) -bor [int64]$Counter

Write-Host "==> upstream version : $base"
Write-Host "    fork build       : $Counter"
Write-Host "    update version   : $version"
Write-Host "    platform         : $platformKey"

# --------------------------------------------------------------------- build

$counterFile = "Telegram\SourceFiles\fork\build_counter.h"
$current = (Select-String -Path $counterFile -Pattern '^#define SEEGRAM_BUILD_COUNTER (\d+)').Matches[0].Groups[1].Value
if ($current -ne "$Counter") {
    Write-Host "==> setting the build counter to $Counter"
    (Get-Content $counterFile) `
        -replace '^#define SEEGRAM_BUILD_COUNTER .*', "#define SEEGRAM_BUILD_COUNTER $Counter" `
        | Set-Content $counterFile -Encoding UTF8
    Write-Host "    remember to commit $counterFile"
}

Write-Host "==> configuring"
& "Telegram\configure.bat" `
    -D TDESKTOP_API_ID="$env:SEEGRAM_API_ID" `
    -D TDESKTOP_API_HASH="$env:SEEGRAM_API_HASH" `
    -D DESKTOP_APP_DISABLE_AUTOUPDATE=OFF `
    -D DESKTOP_APP_SPECIAL_TARGET=win64 `
    -D CMAKE_CXX_FLAGS=-DPACKER_DISABLE_PRIVATE | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "configure failed" }

Write-Host "==> building the client and the packer"
$buildLog = [System.IO.Path]::GetTempFileName()
cmake --build out --config Release --target Telegram Packer *> $buildLog
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] build failed:" -ForegroundColor Red
    Select-String -Path $buildLog -Pattern 'error' | Select-Object -First 20 | ForEach-Object { $_.Line }
    Fail "full log: $buildLog"
}
Remove-Item $buildLog -ErrorAction SilentlyContinue

# ---------------------------------------------------------------- pack, sign

$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("seegram-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    # The packer takes a directory, so stage exactly what ships and nothing else.
    Copy-Item "$buildDir\SeeGram.exe" $stage
    if (Test-Path "$buildDir\Updater.exe") { Copy-Item "$buildDir\Updater.exe" $stage }

    Write-Host "==> packing and signing"
    Push-Location $stage
    & "$root\$buildDir\Packer.exe" `
        -path . `
        -version $base `
        -counter $Counter `
        -channel stable `
        -keys-loc "$root\Telegram\Resources\update" `
        -local-key "$keysDir\release-private.pem" `
        -local-key-id $keyId
    $packed = $LASTEXITCODE
    Pop-Location
    if ($packed -ne 0) { Fail "the packer failed" }

    $package = Get-ChildItem -Path $stage -Filter "td-update-*" -File | Select-Object -First 1
    if (-not $package) { Fail "the packer produced no package" }
    Write-Host ("    package: {0} ({1} MB)" -f $package.Name, [int]($package.Length / 1MB))

    if ($NoPublish) {
        $out = Join-Path $root "seegram-$version-$platformKey.tdup"
        Copy-Item $package.FullName $out -Force
        Write-Host "==> not publishing, package left at $out"
        exit 0
    }

    # --------------------------------------------------------------- publish

    $remoteName = "seegram-$version-$platformKey.tdup"
    Write-Host "==> uploading $remoteName"
    & scp -q -i $sshKey $package.FullName "${server}:$serverRoot/packages/$remoteName"
    if ($LASTEXITCODE -ne 0) { Fail "upload failed" }

    # The feed is edited one platform at a time on purpose: rewriting the whole
    # file risks taking every other platform down with a single bad line. The
    # version has to be a JSON string - the client parses a numeric one through
    # a double, and a 64 bit version is past the point where a double is exact.
    Write-Host "==> updating the feed entry for $platformKey"
    $remoteScript = @"
SEEGRAM_PLATFORM='$platformKey' SEEGRAM_VERSION='$version' python3 - <<'PY'
import json, os, shutil
root = '$serverRoot'
path = root + '/current4'
platform = os.environ['SEEGRAM_PLATFORM']
with open(path) as f:
    feed = json.load(f)
entry = feed.setdefault(platform, {}).setdefault('stable', {})
entry['released'] = os.environ['SEEGRAM_VERSION']
entry.setdefault(
    'link', '/packages/seegram-{version}-' + platform + '.tdup')
tmp = path + '.new'
with open(tmp, 'w') as f:
    json.dump(feed, f, indent=2)
    f.write('\n')
os.replace(tmp, path)
shutil.copyfile(path, root + '/current')
for p in (path, root + '/current'):
    shutil.chown(p, 'www-data', 'www-data')
print('feed updated for ' + platform)
PY
"@
    & ssh -i $sshKey $server $remoteScript
    if ($LASTEXITCODE -ne 0) { Fail "feed update failed" }

    Write-Host "==> verifying what clients will actually see"
    $feed = Invoke-RestMethod -Uri "https://desktop.see.tg/current4"
    $served = $feed.$platformKey.stable.released
    if ($served -ne "$version") { Fail "the feed says $served, expected $version" }
    $head = Invoke-WebRequest -Method Head -Uri "https://desktop.see.tg/packages/$remoteName"
    if ($head.StatusCode -ne 200) { Fail "the package is not reachable, HTTP $($head.StatusCode)" }

    Write-Host ""
    Write-Host "==> released $base build $Counter for $platformKey"
    Write-Host "    clients on an older build pick it up within 8 hours, or at"
    Write-Host "    once through Settings - Advanced - Check for updates."
}
finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
