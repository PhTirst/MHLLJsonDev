param([Parameter(Mandatory = $true)][string]$Archive)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archivePath = [IO.Path]::GetFullPath($Archive)
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$directory = Join-Path $root ('build-package\negative-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory | Out-Null

function Read-Bytes($Entry) {
    $stream = $Entry.Open()
    $memory = New-Object IO.MemoryStream
    try { $stream.CopyTo($memory); return ,$memory.ToArray() }
    finally { $memory.Dispose(); $stream.Dispose() }
}

function Add-Bytes($Zip, [string]$Path, [byte[]]$Bytes) {
    $entry = $Zip.CreateEntry($Path)
    $stream = $entry.Open()
    try { $stream.Write($Bytes, 0, $Bytes.Length) }
    finally { $stream.Dispose() }
}

function Expect-Rejection([string]$Name, [string]$Message, [scriptblock]$Mutate) {
    $path = Join-Path $directory ($Name + '.zip')
    Copy-Item -LiteralPath $archivePath -Destination $path
    $zip = [IO.Compression.ZipFile]::Open($path, [IO.Compression.ZipArchiveMode]::Update)
    try {
        $manifestEntry = $zip.Entries | Where-Object FullName -Like '*/release-manifest.json' | Select-Object -First 1
        $prefix = $manifestEntry.FullName.Substring(0, $manifestEntry.FullName.LastIndexOf('/') + 1)
        & $Mutate $zip $prefix $manifestEntry
    }
    finally { $zip.Dispose() }
    $errorText = $null
    try { & (Join-Path $PSScriptRoot 'verify-package.ps1') -Archive $path }
    catch { $errorText = $_.Exception.Message }
    if (-not $errorText -or $errorText -notlike "*$Message*") { throw "Wrong rejection for ${Name}: $errorText" }
    Write-Host "Rejected $Name"
}

Expect-Rejection 'bad-hash' 'Hash/size mismatch' {
    param($zip, $prefix)
    $entry = $zip.GetEntry($prefix + 'README.md')
    $entry.Delete()
    Add-Bytes $zip ($prefix + 'README.md') ([Text.Encoding]::UTF8.GetBytes('tampered'))
}
Expect-Rejection 'traversal' 'Unsafe ZIP path' {
    param($zip, $prefix)
    Add-Bytes $zip ($prefix + '../escape.txt') ([byte[]]@(1))
}
Expect-Rejection 'duplicate-file' 'Duplicate ZIP entry' {
    param($zip, $prefix)
    Add-Bytes $zip ($prefix + 'README.md') ([byte[]]@(1))
}
Expect-Rejection 'font-payload' 'Unexpected payload' {
    param($zip, $prefix)
    Add-Bytes $zip ($prefix + 'private.ttf') ([byte[]]@(1))
}
Expect-Rejection 'unlisted-file' 'files missing from the manifest' {
    param($zip, $prefix)
    Add-Bytes $zip ($prefix + 'extra.txt') ([byte[]]@(1))
}
Expect-Rejection 'missing-loader' 'Invalid manifest entry' {
    param($zip, $prefix)
    $zip.GetEntry($prefix + 'standard-loader/MHLL.asi').Delete()
}
Expect-Rejection 'wrong-architecture' 'Expected an x86 PE32 binary' {
    param($zip, $prefix, $manifestEntry)
    $entry = $zip.GetEntry($prefix + 'MHLLJsonDev.asi')
    $bytes = Read-Bytes $entry
    $offset = [BitConverter]::ToInt32($bytes, 60)
    $bytes[$offset + 4] = 0x64
    $bytes[$offset + 5] = 0x86
    $entry.Delete()
    Add-Bytes $zip ($prefix + 'MHLLJsonDev.asi') $bytes
    $manifest = [Text.Encoding]::UTF8.GetString((Read-Bytes $manifestEntry)).TrimStart([char]0xfeff) | ConvertFrom-Json
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    ($manifest.files | Where-Object path -CEQ 'MHLLJsonDev.asi').sha256 = $hash
    $manifestEntry.Delete()
    Add-Bytes $zip ($prefix + 'release-manifest.json') ([Text.Encoding]::UTF8.GetBytes(($manifest | ConvertTo-Json -Depth 6)))
}
Write-Host '7 negative package checks passed.'
