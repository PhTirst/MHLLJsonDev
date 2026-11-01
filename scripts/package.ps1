param(
    [string]$SourceTag = '',
    [string]$BuildDirectory = 'build',
    [ValidateRange(1, 256)][int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$cmakeText = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmakeText -notmatch 'project\(MHLLJsonDev\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\s') { throw 'Cannot read the CMake project version.' }
$version = $Matches[1]
if ($SourceTag -and $SourceTag -cne "v$version") { throw "Expected source tag v$version for this project version." }
$sourceRef = if ($SourceTag) { $SourceTag } else { 'HEAD' }
$sourceCommit = & git -C $root rev-parse "$sourceRef^{commit}"
if ($LASTEXITCODE -ne 0) { throw "Missing source ref: $sourceRef" }
$codePaths = @('CMakeLists.txt', 'src', 'inc', 'third_party', 'tests')
& git -C $root diff --exit-code $sourceCommit -- @codePaths
if ($LASTEXITCODE -ne 0) { throw 'Plugin/test sources differ from the requested source commit.' }
$untracked = & git -C $root ls-files --others --exclude-standard -- @codePaths
if ($LASTEXITCODE -ne 0 -or $untracked) { throw 'Untracked plugin/test sources cannot be included in a tagged binary release.' }
$packagingCommit = & git -C $root rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the packaging checkout.' }
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDirectory))
& cmake -S $root -B $build -G 'Visual Studio 17 2022' -A Win32 -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'Configure failed. MSVC x86 and a Windows SDK are required.' }
& cmake --build $build --config Release --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
& ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Release regression failed.' }

$output = Join-Path $root 'build-package'
$name = "MHLLJsonDev-$version-windows-x86"
$stage = Join-Path $output ('stage-' + [guid]::NewGuid().ToString('N'))
$package = Join-Path $stage $name
$release = Join-Path $output 'release'
New-Item -ItemType Directory -Force -Path $package, $release | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'bin\Release\MHLLJsonDev.asi') -Destination $package
New-Item -ItemType Directory -Path (Join-Path $package 'standard-loader'), (Join-Path $package 'diagnostics') | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'bin\Release\MHLL.asi') -Destination (Join-Path $package 'standard-loader')
Copy-Item -LiteralPath (Join-Path $build 'Release\JsonDevTests.exe') -Destination (Join-Path $package 'diagnostics')
foreach ($item in @('README.md', 'README.zh-CN.md', 'CHANGELOG.md', 'docs', 'examples')) {
    Copy-Item -LiteralPath (Join-Path $root $item) -Destination $package -Recurse
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'verify-package.ps1') -Destination $package
$prefix = $package.TrimEnd('\') + '\'
$files = @(Get-ChildItem -LiteralPath $package -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($prefix.Length).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$manifest = [ordered]@{
    application = 'MHLLJsonDev'; version = $version; sourceTag = $SourceTag
    sourceCommit = $sourceCommit.Trim(); packagingCommit = $packagingCommit.Trim()
    target = 'windows-x86'; configuration = 'Release'; staticCppRuntime = $true
    files = $files
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $package 'release-manifest.json') -Encoding UTF8
$archive = Join-Path $release ($name + '.zip')
if (Test-Path -LiteralPath $archive) { throw "Archive already exists; preserve published bytes or move the old archive first: $archive" }
$zip = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -LiteralPath $package -Recurse -File -Force) {
        $entry = $name + '/' + $file.FullName.Substring($prefix.Length).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
}
finally { $zip.Dispose() }
& (Join-Path $PSScriptRoot 'verify-package.ps1') -Archive $archive
& (Join-Path $PSScriptRoot 'test-package.ps1') -Archive $archive
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath ($archive + '.sha256') -Encoding ASCII
Write-Host "Package: $archive"
Write-Host "SHA256: $hash"
