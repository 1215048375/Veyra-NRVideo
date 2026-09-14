$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Set-Location -LiteralPath (Resolve-Path (Join-Path $PSScriptRoot '../..'))
& "$PSScriptRoot/toolchain.ps1"
& python scripts/ci/dependencies.py prepare
if ($LASTEXITCODE -ne 0) { throw 'Dependency preparation failed.' }
