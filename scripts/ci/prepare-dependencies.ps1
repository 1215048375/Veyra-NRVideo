# GitHub-hosted ephemeral runner only. Never called for pull requests.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $env:GITHUB_WORKSPACE -or -not $env:RUNNER_TEMP) { throw 'GitHub runner paths are required.' }
if (-not $env:VEYRA_CI_DEPS_URL -or $env:VEYRA_CI_DEPS_SHA256 -notmatch '^[A-Fa-f0-9]{64}$') {
    throw 'Configure VEYRA_CI_DEPS_URL and VEYRA_CI_DEPS_SHA256 in the windows-build environment. See docs/GITHUB_ACTIONS_BUILD.md.'
}
$uri = [Uri]$env:VEYRA_CI_DEPS_URL
if (-not $uri.IsAbsoluteUri -or $uri.Scheme -ne 'https') { throw 'The private dependency URL must use HTTPS.' }
$archivePath = Join-Path $env:RUNNER_TEMP 'veyra-ci-deps.zip'
$dependencyRoot = Join-Path $env:GITHUB_WORKSPACE 'third_party_local'
if (Test-Path -LiteralPath $dependencyRoot) { throw 'Refusing to overwrite an existing dependency directory.' }
try { Invoke-WebRequest -Uri $uri -OutFile $archivePath -TimeoutSec 600 } catch { throw 'Private dependency download failed; check the secret URL and its expiry.' }
if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $env:VEYRA_CI_DEPS_SHA256) {
    throw 'Dependency archive SHA-256 mismatch; extraction refused.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $prefix = [IO.Path]::GetFullPath($dependencyRoot) + [IO.Path]::DirectorySeparatorChar
    foreach ($entry in $zip.Entries) {
        $target = [IO.Path]::GetFullPath((Join-Path $dependencyRoot $entry.FullName))
        if (-not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or $entry.FullName.Contains(':')) {
            throw 'Dependency archive contains a path outside third_party_local.'
        }
        $name = [IO.Path]::GetFileName($target)
        if ($name -match '(?i)^(nvngx.*\.dll|nvEncodeAPI64\.dll|nvofapi64\.dll|renodx.*|.*\.addon64)$') {
            throw 'Dependency archive contains a prohibited enhancement runtime. Supply development files only.'
        }
    }
} finally { $zip.Dispose() }
[IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $dependencyRoot)
# Do not cache or upload this directory or the private archive.
