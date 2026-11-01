param([Parameter(Mandatory = $true)][string]$Archive)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Read-Entry($Entry) {
    $stream = $Entry.Open()
    $memory = New-Object IO.MemoryStream
    try { $stream.CopyTo($memory); return ,$memory.ToArray() }
    finally { $memory.Dispose(); $stream.Dispose() }
}

function Assert-X86([byte[]]$Bytes, [bool]$Dll) {
    if ($Bytes.Length -lt 64 -or $Bytes[0] -ne 0x4d -or $Bytes[1] -ne 0x5a) { throw 'Invalid DOS header.' }
    $offset = [BitConverter]::ToInt32($Bytes, 60)
    if ($offset -lt 64 -or $offset -gt $Bytes.Length - 26) { throw 'Invalid PE header offset.' }
    if ([BitConverter]::ToUInt32($Bytes, $offset) -ne 0x4550 -or
        [BitConverter]::ToUInt16($Bytes, $offset + 4) -ne 0x14c -or
        [BitConverter]::ToUInt16($Bytes, $offset + 24) -ne 0x10b) { throw 'Expected an x86 PE32 binary.' }
    $isDll = ([BitConverter]::ToUInt16($Bytes, $offset + 22) -band 0x2000) -ne 0
    if ($isDll -ne $Dll) { throw 'Unexpected PE DLL/executable type.' }
}

$zip = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Archive))
$testBytes = $null
try {
    $entries = @{}
    $packageName = $null
    foreach ($entry in $zip.Entries) {
        $path = $entry.FullName
        if ($path -match '\\|:|^/|(^|/)\.\.?(/|$)' -or (($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000) { throw "Unsafe ZIP path: $path" }
        $split = $path.IndexOf('/')
        if ($split -lt 1) { throw 'Each archive entry must be in one package folder.' }
        $folder = $path.Substring(0, $split)
        if ($null -eq $packageName) { $packageName = $folder }
        if ($folder -cne $packageName) { throw 'Multiple package roots.' }
        if ($path.EndsWith('/')) { continue }
        $relative = $path.Substring($split + 1)
        if ($entries.ContainsKey($relative)) { throw "Duplicate ZIP entry: $relative" }
        if ($relative -match '(?i)(^|/)(bin|obj|\.git|build[^/]*)/|\.(ttf|otf|gxt|dat|txd|pdb|lib|exp|dll|log|zip|7z)$') { throw "Unexpected payload: $relative" }
        if ($relative -match '(?i)\.(asi|exe)$' -and $relative -cnotin @('MHLLJsonDev.asi', 'standard-loader/MHLL.asi', 'diagnostics/JsonDevTests.exe')) { throw "Unexpected binary: $relative" }
        $entries[$relative] = $entry
    }
    if (-not $entries.ContainsKey('release-manifest.json')) { throw 'Missing release manifest.' }
    $manifest = [Text.Encoding]::UTF8.GetString((Read-Entry $entries['release-manifest.json'])).TrimStart([char]0xfeff) | ConvertFrom-Json
    if ($manifest.application -cne 'MHLLJsonDev' -or $manifest.target -cne 'windows-x86' -or
        $manifest.configuration -cne 'Release' -or $manifest.staticCppRuntime -ne $true -or
        $manifest.sourceCommit -notmatch '^[0-9a-f]{40}$' -or $manifest.packagingCommit -notmatch '^[0-9a-f]{40}$' -or
        ($manifest.sourceTag -and $manifest.sourceTag -cne ('v' + $manifest.version)) -or $packageName -cne ('MHLLJsonDev-' + $manifest.version + '-windows-x86')) { throw 'Unexpected manifest identity.' }
    $seen = @{}
    foreach ($file in $manifest.files) {
        $path = [string]$file.path
        if ($path -ceq 'release-manifest.json' -or $seen.ContainsKey($path) -or -not $entries.ContainsKey($path)) { throw "Invalid manifest entry: $path" }
        $seen[$path] = $true
        $bytes = Read-Entry $entries[$path]
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
        finally { $sha.Dispose() }
        if ($bytes.LongLength -ne $file.bytes -or $hash -cne $file.sha256) { throw "Hash/size mismatch: $path" }
        if ($path -match '(?i)\.(asi|exe)$') { Assert-X86 $bytes ($path -match '(?i)\.asi$') }
        if ($path -ceq 'diagnostics/JsonDevTests.exe') { $testBytes = $bytes }
    }
    if ($seen.Count -ne $entries.Count - 1) { throw 'Archive has files missing from the manifest.' }
    foreach ($required in @('MHLLJsonDev.asi', 'standard-loader/MHLL.asi', 'diagnostics/JsonDevTests.exe', 'README.md', 'README.zh-CN.md', 'docs/PROVENANCE.md', 'docs/WINDOWS_PACKAGE.md', 'verify-package.ps1')) {
        if (-not $seen.ContainsKey($required)) { throw "Missing package file: $required" }
    }
}
finally { $zip.Dispose() }

# Run the standalone synthetic tests, never LoadLibrary on an ASI outside the game.
$temp = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('mhll-package-check-' + [guid]::NewGuid().ToString('N'))))
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
if (-not $temp.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $temp)) { throw 'Unsafe test directory.' }
New-Item -ItemType Directory -Path $temp | Out-Null
$executable = Join-Path $temp 'JsonDevTests.exe'
[IO.File]::WriteAllBytes($executable, $testBytes)
$process = Start-Process -FilePath $executable -WorkingDirectory $temp -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $temp 'stdout.txt') -RedirectStandardError (Join-Path $temp 'stderr.txt')
try {
    if (-not $process.WaitForExit(120000)) { $process.Kill(); $process.WaitForExit(); throw "Packaged test timed out. Diagnostics: $temp" }
    $process.WaitForExit()
    $stdout = Get-Content -LiteralPath (Join-Path $temp 'stdout.txt') -Raw
    $stderr = Get-Content -LiteralPath (Join-Path $temp 'stderr.txt') -Raw
    if ($process.ExitCode -ne 0 -or $stdout -notmatch '[0-9]+ assertions passed') { throw "Packaged test failed: $stdout $stderr; diagnostics: $temp" }
    Write-Host $stdout.Trim()
}
finally { $process.Dispose() }
Write-Host "Verified manifest hashes, x86 binaries and packaged regression: $Archive"
Write-Host "Test logs: $temp"
