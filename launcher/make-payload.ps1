param(
	[Parameter(Mandatory = $true)][string]$Dll,
	[Parameter(Mandatory = $true)][string]$Images,
	[Parameter(Mandatory = $true)][string]$Out
)

$ErrorActionPreference = 'Stop'

function Escape-RcPath([string]$path) {
	return ((Resolve-Path -LiteralPath $path).Path.Replace('\', '\\'))
}

if (-not (Test-Path -LiteralPath $Dll)) {
	Write-Error "DLL not found: $Dll"
	exit 1
}

$lines = New-Object System.Collections.Generic.List[string]
# 资源清单中的绝对路径可能含中文，必须与下方 UTF-8 写入编码一致。
$lines.Add('#pragma code_page(65001)')
$lines.Add('101 RCDATA "' + (Escape-RcPath $Dll) + '"')
$projectLicense = Join-Path $PSScriptRoot '..\LICENSE'
if (-not (Test-Path -LiteralPath $projectLicense)) {
	Write-Error "Project license is missing"
	exit 1
}
$lines.Add('PROJECT LICENSE "' + (Escape-RcPath $projectLicense) + '"')
$icons = @(Get-ChildItem -LiteralPath $Images -Filter *.svg | Sort-Object Name)
if ($icons.Count -eq 0) {
	Write-Error "No SVG icons in $Images"
	exit 1
}
foreach ($icon in $icons) {
	$base = [System.IO.Path]::GetFileNameWithoutExtension($icon.Name)
	$lines.Add($base + ' SVG "' + (Escape-RcPath $icon.FullName) + '"')
}
$license = Join-Path $Images 'METEOCONS-LICENSE.txt'
if (Test-Path -LiteralPath $license) {
	$lines.Add('METEOCONS LICENSE "' + (Escape-RcPath $license) + '"')
}
$utf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllLines($Out, $lines, $utf8)
