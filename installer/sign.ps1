# Signs OpenMix binaries with an Authenticode code-signing certificate (.pfx) and a
# timestamp, so the signature stays valid after the certificate expires.
#
# Certificate, in this order:
#   -PfxPath <file>              a .pfx on this machine
#   $env:SIGN_PFX_BASE64         the .pfx as base64 (how CI gets it from a secret)
# Password: $env:SIGN_PFX_PASSWORD
#
# With no certificate it prints a note and exits 0, so unsigned builds (forks, pull
# requests) still work.
#
#   $env:SIGN_PFX_PASSWORD = '...'
#   .\installer\sign.ps1 -PfxPath C:\certs\openmix.pfx build\OpenMix_artefacts\Release\OpenMix.exe

param(
    [Parameter(Mandatory, Position = 0, ValueFromRemainingArguments)] [string[]] $Files,
    [string] $PfxPath,
    [string] $TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'

if (-not $PfxPath -and -not $env:SIGN_PFX_BASE64) {
    Write-Host 'No signing certificate (SIGN_PFX_BASE64 or -PfxPath): leaving the files unsigned.'
    exit 0
}

# signtool ships with the Windows SDK; take the newest x64 one.
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
    Sort-Object { [version] $_.Directory.Parent.Name } -ErrorAction SilentlyContinue |
    Select-Object -Last 1
if (-not $signtool) { throw 'signtool.exe not found: install the Windows 10/11 SDK.' }

$tempPfx = $null
try {
    if (-not $PfxPath) {
        $tempPfx = Join-Path ([IO.Path]::GetTempPath()) ("openmix-sign-" + [guid]::NewGuid() + ".pfx")
        [IO.File]::WriteAllBytes($tempPfx, [Convert]::FromBase64String($env:SIGN_PFX_BASE64))
        $PfxPath = $tempPfx
    }

    foreach ($file in $Files) {
        & $signtool.FullName sign /fd SHA256 /tr $TimestampUrl /td SHA256 /f $PfxPath /p $env:SIGN_PFX_PASSWORD $file
        if ($LASTEXITCODE -ne 0) { throw "Signing $file failed." }
        & $signtool.FullName verify /pa $file
        if ($LASTEXITCODE -ne 0) { throw "$file is signed but doesn't verify." }
    }
}
finally {
    if ($tempPfx) { Remove-Item $tempPfx -Force -ErrorAction SilentlyContinue }   # never leave the key on disk
}
