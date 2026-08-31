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
$sshKey  = if ($env:SEEGRAM_SSH_KEY) { $env:SEEGRAM_SSH_KEY } else { "$HOME\.ssh\seegram_updates" }

# Deliberately without a default: where the update server lives and which
# account reaches it are not facts a public repository should carry.
$server     = $env:SEEGRAM_UPDATE_SERVER
$serverRoot = $env:SEEGRAM_UPDATE_ROOT
$platformKey = "win64"
$buildDir    = "out\Release"

# ------------------------------------------------------------------ preflight

if ($Counter -lt 1) { Fail "the counter must be 1 or greater" }
# The counter file is the one thing a release is allowed to have changed:
# this script writes it itself, so re-running must not trip over that.
$dirty = @(git status --porcelain --untracked-files=no |
    Where-Object { $_ -notmatch 'fork/build_counter\.h$' })
if ($dirty) {
    Write-Host ($dirty -join "`n")
    Fail "working tree is dirty - a release must be reproducible."
}
if (-not (Test-Path "$keysDir\release-private.pem")) {
    Fail "signing key missing at $keysDir\release-private.pem"
}
if (-not $env:SEEGRAM_API_ID)   { Fail "set SEEGRAM_API_ID" }
if (-not $env:SEEGRAM_API_HASH) { Fail "set SEEGRAM_API_HASH" }
if (-not $NoPublish) {
    if (-not $server)     { Fail "set SEEGRAM_UPDATE_SERVER, e.g. user@host" }
    if (-not $serverRoot) { Fail "set SEEGRAM_UPDATE_ROOT, the served directory" }
}

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

# cmake, MSBuild and the toolchain only exist inside the Visual Studio
# environment, which a plain PowerShell session does not have. Import it here
# so a release does not depend on being started from a developer prompt.
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "==> entering the Visual Studio environment"
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { Fail "vswhere.exe not found - is Visual Studio installed?" }
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vsPath) { Fail "no Visual Studio install with the C++ toolchain found" }
    $devShell = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    if (-not (Test-Path $devShell)) { Fail "DevShell module missing at $devShell" }
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
    Set-Location $root
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        Fail "cmake still not on PATH after entering the VS environment"
    }
}

Write-Host "==> configuring"
# "x64" is positional and picked up by cmake/run_cmake.py, which otherwise
# defaults the Visual Studio generator to Win32 and then collides with an
# existing x64 build directory.
& "Telegram\configure.bat" x64 `
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
    # The packer takes a directory, so stage exactly what ships and nothing
    # else. d3dcompiler is part of it: upstream's own build packs it, and a
    # client that never receives it falls back to software rendering.
    Copy-Item "$buildDir\SeeGram.exe" $stage
    if (Test-Path "$buildDir\Updater.exe") { Copy-Item "$buildDir\Updater.exe" $stage }
    $d3d = Get-ChildItem -Path $buildDir -Filter "d3dcompiler_47.dll" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($d3d) {
        New-Item -ItemType Directory -Path "$stage\modules\x64\d3d" -Force | Out-Null
        Copy-Item $d3d.FullName "$stage\modules\x64\d3d\"
    } else {
        Write-Host "    note: d3dcompiler_47.dll not found, packaging without it"
    }

    Write-Host "==> packing and signing"
    Push-Location $stage
    # -target names the architecture the package is FOR. It is part of the
    # signed region, so a package built without it is stamped x86 and every
    # x64 client rejects it - the same trap as -arch on macOS.
    & "$root\$buildDir\Packer.exe" `
        -path . `
        -version $base `
        -counter $Counter `
        -target win64 `
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
    # Sent base64-encoded rather than as a here-document: PowerShell writes
    # CRLF line endings, which leave the closing delimiter unmatched and spill
    # the script into the shell. Encoding sidesteps line endings and quoting
    # both.
    $py = @'
import json, os, shutil
root = os.environ['SEEGRAM_ROOT']
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
'@
    $py = $py -replace "`r", ""
    $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($py))
    $remote = "echo $b64 | base64 -d | " +
        "SEEGRAM_ROOT='$serverRoot' " +
        "SEEGRAM_PLATFORM='$platformKey' " +
        "SEEGRAM_VERSION='$version' python3 -"
    & ssh -i $sshKey $server $remote
    if ($LASTEXITCODE -ne 0) { Fail "feed update failed" }

    Write-Host "==> verifying what clients will actually see"
    $feed = Invoke-RestMethod -Uri "https://desktop.see.tg/current4"
    $served = $feed.$platformKey.stable.released
    if ($served -ne "$version") { Fail "the feed says $served, expected $version" }
    $head = Invoke-WebRequest -Method Head -Uri "https://desktop.see.tg/packages/$remoteName"
    if ($head.StatusCode -ne 200) { Fail "the package is not reachable, HTTP $($head.StatusCode)" }

    # ---------------------------------------------------- github release
    #
    # A second, separate channel: the updater serves people who already run
    # SeeGram, this serves people installing it for the first time. Attach a
    # portable archive rather than the .tdup, which only the updater reads.

    $versionStr = (Select-String -Path $versionHeader -Pattern 'AppVersionStr = "([^"]*)"').Matches[0].Groups[1].Value
    $tag = "v$versionStr-$Counter"
    $archive = Join-Path $stage "SeeGram-$versionStr-build$Counter-Windows-x64.zip"

    # Named explicitly: the clone also has an "upstream" remote, and gh guesses
    # from the remotes - it picked telegramdesktop/tdesktop and got a 404.
    $slug = (git remote get-url origin) -replace '.*github\.com[:/]([^/]+/[^/.]+?)(\.git)?$', '$1'

    # ------------------------------------------------------------ installer
    #
    # Built from the same staged files the update package was made of, so the
    # two can never describe different builds. Unsigned: Authenticode needs a
    # certificate tied to a real identity, and SmartScreen showing an
    # unknown-publisher prompt once is a distribution problem, not a build one.

    # Looked for in several places on purpose: winget, the standalone
    # installer and a portable copy all put ISCC somewhere different, and
    # "installer silently missing from the release" is a bad way to find out.
    $installer = $null
    $iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
    if (-not $iscc) {
        $iscc = @(
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
            "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
            "${env:ProgramFiles(x86)}\Inno Setup 5\ISCC.exe",
            "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
        ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    }
    if (-not $iscc) {
        $iscc = Get-ChildItem -Path @("${env:ProgramFiles(x86)}", "$env:ProgramFiles", "$env:LOCALAPPDATA\Programs") `
            -Filter ISCC.exe -Recurse -Depth 3 -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
    }
    if ($iscc) {
        Write-Host "    using $iscc"
        Write-Host "==> building the installer"
        $full = "$versionStr.$Counter"
        & $iscc /Q `
            "/dMyAppVersion=$versionStr" `
            "/dMyAppVersionZero=$versionStr" `
            "/dMyAppVersionFull=$full" `
            "/dReleasePath=$stage" `
            "/dMyBuildTarget=win64" `
            "$root\Telegram\build\setup.iss"
        if ($LASTEXITCODE -ne 0) { Fail "the installer build failed" }
        $installer = Get-ChildItem -Path $stage -Filter "seegram-setup-*.exe" -File |
            Select-Object -First 1
        if ($installer) {
            Write-Host ("    {0} ({1} MB)" -f $installer.Name, [int]($installer.Length / 1MB))
        }
    } else {
        Write-Host "==> Inno Setup not found, skipping the installer"
    }

    if (Get-Command gh -ErrorAction SilentlyContinue) {
        Write-Host "==> attaching builds to release $tag in $slug"
        $items = @("$buildDir\SeeGram.exe")
        if (Test-Path "$buildDir\Updater.exe") { $items += "$buildDir\Updater.exe" }
        Compress-Archive -Path $items -DestinationPath $archive -Force
        gh release view $tag --repo $slug *> $null
        if ($LASTEXITCODE -ne 0) {
            $notes = "Telegram Desktop $versionStr, SeeGram build $Counter.`n`n" +
                "Installed copies update themselves; this archive is for a first install."
            gh release create $tag --repo $slug --title "SeeGram $versionStr build $Counter" --notes $notes | Out-Null
        }
        $upload = @($archive)
        if ($installer) { $upload += $installer.FullName }
        gh release upload $tag $upload --repo $slug --clobber | Out-Null
        $upload | ForEach-Object { Write-Host "    $(Split-Path $_ -Leaf)" }
    } else {
        Write-Host "==> gh not installed, skipping the GitHub release"
    }

    Write-Host ""
    Write-Host "==> released $base build $Counter for $platformKey"
    Write-Host "    clients on an older build pick it up within 8 hours, or at"
    Write-Host "    once through Settings - Advanced - Check for updates."
}
finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
