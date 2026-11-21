param([int]$Port = 8848)
$ErrorActionPreference = 'Stop'
$python = 'D:\crossmux\.tools\idf-tools\python_env\idf6.1_py3.12_env\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) {
    $python = (Get-Command python -ErrorAction Stop).Source
}
& $python (Join-Path $PSScriptRoot 'server.py') --port $Port --open
