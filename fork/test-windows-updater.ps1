[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Updater)

$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('seegram-update-test-' + [guid]::NewGuid().ToString('N'))
$install = Join-Path $testRoot 'install'
$work = Join-Path $testRoot 'work'
$ready = Join-Path $work 'tupdates\temp'
New-Item -ItemType Directory -Path $install, "$ready\tdata" -Force | Out-Null
try {
    $source = Join-Path $testRoot 'probe.cpp'
    @'
#include <windows.h>
#include <string>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    wchar_t module[32768];
    const auto length = GetModuleFileNameW(nullptr, module, 32768);
    if (!length || length >= 32768) return 1;
    const auto marker = std::wstring(module, length) + L".started";
    const auto file = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 2;
    CloseHandle(file);
    return 0;
}
'@ | Set-Content -Encoding utf8 $source
    & cl.exe /nologo /O2 /MT /EHsc $source "/Fo$testRoot\probe.obj" "/Fe$ready\SeeGram.exe" /link /SUBSYSTEM:WINDOWS *> "$testRoot\compile.log"
    if ($LASTEXITCODE -ne 0) { throw "Updater probe compilation failed: $(Get-Content -Raw "$testRoot\compile.log")" }
    $expected = (Get-FileHash "$ready\SeeGram.exe" -Algorithm SHA256).Hash
    [System.IO.File]::WriteAllText("$install\SeeGram.exe", 'old executable placeholder')
    Copy-Item $Updater "$install\Updater.exe"
    [System.IO.File]::WriteAllText("$ready\ready", '1')
    [System.IO.File]::WriteAllBytes("$ready\tdata\version", [BitConverter]::GetBytes([uint32]0x7FFFFFFE))
    $process = Start-Process -FilePath "$install\Updater.exe" -ArgumentList '-update -exename SeeGram.exe' -WorkingDirectory $work -PassThru
    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        throw 'Updater smoke test timed out'
    }
    if ((Get-FileHash "$install\SeeGram.exe" -Algorithm SHA256).Hash -ne $expected) {
        throw 'Updater did not replace the installed executable'
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path "$install\SeeGram.exe.started") -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    if (-not (Test-Path "$install\SeeGram.exe.started")) { throw 'Updater did not launch the replaced executable' }
    if (Test-Path "$ready\ready") { throw 'Updater did not clear the applied update' }
    Write-Host '    updater smoke test: replaced and launched the new EXE'
} finally {
    Remove-Item -Recurse -Force $testRoot -ErrorAction SilentlyContinue
}
