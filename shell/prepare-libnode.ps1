param(
	[string] $Version = "24.15.0",

	[string] $Url = "",

	[switch] $Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-RepoRoot {
	$scriptDir = Split-Path -Parent $PSCommandPath
	return (Resolve-Path (Join-Path $scriptDir "..")).Path
}

function Assert-PathInside {
	param(
		[string] $ChildPath,
		[string] $ParentPath
	)

	$resolvedParent = [System.IO.Path]::GetFullPath($ParentPath).TrimEnd("\", "/")
	$resolvedChild = [System.IO.Path]::GetFullPath($ChildPath).TrimEnd("\", "/")
	if (-not $resolvedChild.StartsWith($resolvedParent, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Refusing to modify path outside the expected directory: $resolvedChild"
	}
}

function Test-LibnodeLayout {
	param([string] $LibnodeDirectory)

	$requiredFiles = @(
		"include/node.h",
		"windows/x64/libnode.lib",
		"android/arm64/libnode.a"
	)

	foreach ($file in $requiredFiles) {
		if (-not (Test-Path (Join-Path $LibnodeDirectory $file))) {
			return $false
		}
	}

	return $true
}

$repoRoot = Get-RepoRoot
$libnodeDir = Join-Path $repoRoot "libnode"
$buildDepsDir = Join-Path $repoRoot "build/deps"
$archivePath = Join-Path $buildDepsDir "libnode-$Version.zip"
$extractDir = Join-Path $buildDepsDir "libnode-extract"

if (-not $Url) {
	$Url = "https://github.com/moluopro/libnode/releases/download/$Version/libnode.zip"
}

if ((-not $Force) -and (Test-LibnodeLayout -LibnodeDirectory $libnodeDir)) {
	Write-Host "libnode is ready:"
	Write-Host "  $libnodeDir"
	return
}

New-Item -ItemType Directory -Force -Path $buildDepsDir | Out-Null
Assert-PathInside -ChildPath $archivePath -ParentPath $buildDepsDir
Assert-PathInside -ChildPath $extractDir -ParentPath $buildDepsDir
Assert-PathInside -ChildPath $libnodeDir -ParentPath $repoRoot

if (-not (Test-Path $archivePath)) {
	Write-Host "Downloading libnode $Version..."
	Write-Host "  $Url"
	Invoke-WebRequest -Uri $Url -OutFile $archivePath
} else {
	Write-Host "Using cached libnode archive:"
	Write-Host "  $archivePath"
}

if (Test-Path $extractDir) {
	Remove-Item -LiteralPath $extractDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $extractDir | Out-Null

Write-Host "Extracting libnode..."
Expand-Archive -LiteralPath $archivePath -DestinationPath $extractDir -Force

$candidateDirs = @(
	(Join-Path $extractDir "libnode"),
	$extractDir
)
$sourceDir = $candidateDirs | Where-Object { Test-LibnodeLayout -LibnodeDirectory $_ } | Select-Object -First 1
if (-not $sourceDir) {
	throw "The downloaded archive does not contain the expected libnode layout."
}

if (Test-Path $libnodeDir) {
	if (-not $Force) {
		throw "Existing libnode directory is incomplete. Pass -Force to replace it: $libnodeDir"
	}
	Remove-Item -LiteralPath $libnodeDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $libnodeDir | Out-Null
Copy-Item -Path (Join-Path $sourceDir "*") -Destination $libnodeDir -Recurse -Force

if (-not (Test-LibnodeLayout -LibnodeDirectory $libnodeDir)) {
	throw "libnode was extracted, but required files are still missing."
}

Write-Host "libnode is ready:"
Write-Host "  $libnodeDir"
