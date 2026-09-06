[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Root)
# Consolidated local delivery acceptance authorized by the user.
& (Join-Path $PSScriptRoot 'delivery.ps1') -Root $Root
exit $LASTEXITCODE
