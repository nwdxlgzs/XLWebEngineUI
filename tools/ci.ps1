$ErrorActionPreference = 'Stop'
& python -B (Join-Path $PSScriptRoot 'ci.py') @args
exit $LASTEXITCODE
