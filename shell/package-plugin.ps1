param(
	[string] $Version = "",

	[string] $OutputDirectory = "",

	[switch] $SkipBinaryValidation
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

function Get-GitVersion {
	$versionText = & git describe --tags --always 2>$null
	if ($LASTEXITCODE -eq 0 -and $versionText) {
		return $versionText.Trim()
	}

	return "dev"
}

$repoRoot = Get-RepoRoot
if (-not $Version) {
	$Version = Get-GitVersion
}
if (-not $OutputDirectory) {
	$OutputDirectory = Join-Path $repoRoot "dist"
}

$addonRoot = Join-Path $repoRoot "example/addons/gode"
$requiredFiles = @(
	"plugin.cfg",
	"gode.gd",
	"binary/.gdextension"
)
$requiredBinaries = @(
	"binary/windows/x64/libgode.dll",
	"binary/android/arm64/libgode.so"
)

foreach ($file in $requiredFiles) {
	if (-not (Test-Path (Join-Path $addonRoot $file))) {
		throw "Missing plugin file: $file"
	}
}

if (-not $SkipBinaryValidation) {
	foreach ($file in $requiredBinaries) {
		if (-not (Test-Path (Join-Path $addonRoot $file))) {
			throw "Missing built plugin binary: $file"
		}
	}
}

$stagingRoot = Join-Path $repoRoot "build/package-staging"
$stagedAddonRoot = Join-Path $stagingRoot "addons/gode"
$archiveName = "gode-plugin-$Version.zip"
$archivePath = Join-Path $OutputDirectory $archiveName

Assert-PathInside -ChildPath $stagingRoot -ParentPath (Join-Path $repoRoot "build")
Assert-PathInside -ChildPath $OutputDirectory -ParentPath $repoRoot

if (Test-Path $stagingRoot) {
	Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagedAddonRoot | Out-Null
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

Copy-Item -Path (Join-Path $addonRoot "*") -Destination $stagedAddonRoot -Recurse -Force

$unneededBinaryPatterns = @("*.lib", "*.exp", "*.pdb", "*.ilk")
foreach ($pattern in $unneededBinaryPatterns) {
	Get-ChildItem -Path (Join-Path $stagedAddonRoot "binary") -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue |
		Remove-Item -Force
}

if (Test-Path $archivePath) {
	Remove-Item -LiteralPath $archivePath -Force
}

Compress-Archive -Path (Join-Path $stagingRoot "addons") -DestinationPath $archivePath -CompressionLevel Optimal

Write-Host "Packaged plugin:"
Write-Host "  $archivePath"
