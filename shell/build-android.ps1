param(
	[ValidateSet("arm64")]
	[string] $Architecture = "arm64",

	[int] $ApiLevel = 28,

	[string] $Generator = "",

	[int] $Jobs = 0,

	[switch] $Fresh,

	[switch] $SkipCodegen,

	[switch] $SkipNdkDownload
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ndkVersion = "r28b"
$ndkRevision = "28.1.13356709"
$ndkArchiveName = "android-ndk-$ndkVersion-windows.zip"
$ndkDownloadUrl = "https://dl.google.com/android/repository/$ndkArchiveName"
$ndkSha1 = "c7d82072807fcabbd6ee356476761d8729307185"

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

function Get-DefaultJobCount {
	$cpuCount = [Environment]::ProcessorCount
	if ($cpuCount -le 2) {
		return 1
	}

	return [Math]::Max(1, $cpuCount - 1)
}

function Get-AndroidAbi {
	param([string] $Arch)

	switch ($Arch) {
		"arm64" { return "arm64-v8a" }
		default { throw "Unsupported Android architecture: $Arch" }
	}
}

function Get-NdkRevision {
	param([string] $NdkDirectory)

	$propertiesFile = Join-Path $NdkDirectory "source.properties"
	if (-not (Test-Path $propertiesFile)) {
		return ""
	}

	$revisionLine = Get-Content -Path $propertiesFile |
		Where-Object { $_ -match "^Pkg\.Revision\s*=\s*(.+)$" } |
		Select-Object -First 1

	if ($revisionLine -match "^Pkg\.Revision\s*=\s*(.+)$") {
		return $Matches[1].Trim()
	}

	return ""
}

function Test-AndroidNdk {
	param([string] $NdkDirectory)

	$toolchainFile = Join-Path $NdkDirectory "build/cmake/android.toolchain.cmake"
	if (-not (Test-Path $toolchainFile)) {
		return $false
	}

	return (Get-NdkRevision -NdkDirectory $NdkDirectory) -eq $ndkRevision
}

function Invoke-NdkDownload {
	param(
		[string] $ArchivePath,
		[string] $NdkDirectory,
		[string] $BuildRoot
	)

	Assert-PathInside -ChildPath $ArchivePath -ParentPath $BuildRoot
	Assert-PathInside -ChildPath $NdkDirectory -ParentPath $BuildRoot

	if (-not (Test-Path $ArchivePath)) {
		Write-Host "Downloading Android NDK $ndkVersion ($ndkRevision)..."
		Write-Host "  $ndkDownloadUrl"
		Invoke-WebRequest -Uri $ndkDownloadUrl -OutFile $ArchivePath
	} else {
		Write-Host "Using existing NDK archive:"
		Write-Host "  $ArchivePath"
	}

	$actualSha1 = (Get-FileHash -Algorithm SHA1 -Path $ArchivePath).Hash.ToLowerInvariant()
	if ($actualSha1 -ne $ndkSha1) {
		throw "NDK archive SHA1 mismatch. Expected $ndkSha1, got $actualSha1. Delete the archive and rerun this script."
	}

	if (Test-Path $NdkDirectory) {
		$existingRevision = Get-NdkRevision -NdkDirectory $NdkDirectory
		throw "Existing NDK directory has revision '$existingRevision', expected '$ndkRevision': $NdkDirectory"
	}

	Write-Host "Extracting Android NDK to build directory..."
	Expand-Archive -LiteralPath $ArchivePath -DestinationPath $BuildRoot -Force
}

function Ensure-AndroidNdk {
	param(
		[string] $BuildRoot,
		[bool] $SkipDownload
	)

	$ndkDirectory = Join-Path $BuildRoot "android-ndk-$ndkVersion"
	$archivePath = Join-Path $BuildRoot $ndkArchiveName

	if (Test-AndroidNdk -NdkDirectory $ndkDirectory) {
		return $ndkDirectory
	}

	if ($SkipDownload) {
		throw "Android NDK $ndkVersion ($ndkRevision) was not found at: $ndkDirectory"
	}

	Invoke-NdkDownload -ArchivePath $archivePath -NdkDirectory $ndkDirectory -BuildRoot $BuildRoot

	if (-not (Test-AndroidNdk -NdkDirectory $ndkDirectory)) {
		$actualRevision = Get-NdkRevision -NdkDirectory $ndkDirectory
		throw "Downloaded NDK revision '$actualRevision' does not match expected revision '$ndkRevision'."
	}

	return $ndkDirectory
}

function Find-VisualStudioNinja {
	$vswhereCandidates = @(
		"${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
		"${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
	)

	$vswhere = $vswhereCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
	if (-not $vswhere) {
		return ""
	}

	$json = & $vswhere -latest -products * -format json
	if ($LASTEXITCODE -ne 0 -or -not $json) {
		return ""
	}

	$instances = $json | ConvertFrom-Json
	if (-not $instances -or $instances.Count -eq 0) {
		return ""
	}

	$ninjaPath = Join-Path $instances[0].installationPath "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
	if (Test-Path $ninjaPath) {
		return $ninjaPath
	}

	return ""
}

function Find-Ninja {
	$pathNinja = Get-Command ninja.exe -ErrorAction SilentlyContinue
	if ($pathNinja) {
		return $pathNinja.Source
	}

	return Find-VisualStudioNinja
}

function Select-CMakeGenerator {
	param([string] $RequestedGenerator)

	if ($RequestedGenerator) {
		return @{
			Name = $RequestedGenerator
			NinjaPath = ""
			SupportsParallel = $RequestedGenerator -like "Ninja*"
		}
	}

	$ninjaPath = Find-Ninja
	if (-not $ninjaPath) {
		throw "Ninja was not found. Install Ninja or pass -Generator with an available CMake generator."
	}

	return @{
		Name = "Ninja"
		NinjaPath = $ninjaPath
		SupportsParallel = $true
	}
}

function Clear-CMakeConfigureState {
	param(
		[string] $BuildDirectory,
		[string] $BuildRoot
	)

	Assert-PathInside -ChildPath $BuildDirectory -ParentPath $BuildRoot

	$cacheFile = Join-Path $BuildDirectory "CMakeCache.txt"
	$cmakeFiles = Join-Path $BuildDirectory "CMakeFiles"

	if (Test-Path $cacheFile) {
		Remove-Item -LiteralPath $cacheFile -Force
	}
	if (Test-Path $cmakeFiles) {
		Remove-Item -LiteralPath $cmakeFiles -Recurse -Force
	}
}

function Get-CachedGenerator {
	param([string] $BuildDirectory)

	$cacheFile = Join-Path $BuildDirectory "CMakeCache.txt"
	if (-not (Test-Path $cacheFile)) {
		return ""
	}

	$generatorLine = Get-Content -Path $cacheFile | Where-Object { $_ -match "^CMAKE_GENERATOR:INTERNAL=" } | Select-Object -First 1
	if ($generatorLine -match "^CMAKE_GENERATOR:INTERNAL=(.+)$") {
		return $Matches[1]
	}

	return ""
}

$repoRoot = Get-RepoRoot
$buildRoot = Join-Path $repoRoot "build"
$ndkDir = Ensure-AndroidNdk -BuildRoot $buildRoot -SkipDownload:$SkipNdkDownload
$androidAbi = Get-AndroidAbi -Arch $Architecture
$buildDir = Join-Path $repoRoot "build/android/$Architecture"
$binDir = Join-Path $repoRoot "example/addons/gode/binary/android/$Architecture"
$expectedLibrary = Join-Path $binDir "libgode.so"
$libnodeLibrary = Join-Path $repoRoot "libnode/android/$Architecture/libnode.a"
$toolchainFile = Join-Path $ndkDir "build/cmake/android.toolchain.cmake"
$selectedGenerator = Select-CMakeGenerator -RequestedGenerator $Generator
$jobCount = if ($Jobs -gt 0) { $Jobs } else { Get-DefaultJobCount }
$configuration = "Release"

if (-not (Test-Path $libnodeLibrary)) {
	throw "Missing libnode static library: $libnodeLibrary"
}

if ($selectedGenerator.NinjaPath) {
	$ninjaDir = Split-Path -Parent $selectedGenerator.NinjaPath
	$env:PATH = "$ninjaDir;$env:PATH"
}

New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
if ($Fresh) {
	Clear-CMakeConfigureState -BuildDirectory $buildDir -BuildRoot $buildRoot
} elseif (-not $Generator) {
	$cachedGenerator = Get-CachedGenerator -BuildDirectory $buildDir
	if ($cachedGenerator) {
		$selectedGenerator.Name = $cachedGenerator
		$selectedGenerator.SupportsParallel = $cachedGenerator -like "Ninja*"
	}
}

$configureArgs = @(
	"-S", $repoRoot,
	"-B", $buildDir,
	"-G", $selectedGenerator.Name,
	"-DCMAKE_BUILD_TYPE=$configuration",
	"-DCMAKE_SUPPRESS_REGENERATION=ON",
	"-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
	"-DANDROID_ABI=$androidAbi",
	"-DANDROID_PLATFORM=android-$ApiLevel",
	"-DANDROID_STL=c++_shared",
	"-DGODE_RUN_CODEGEN=$((-not $SkipCodegen).ToString().ToUpperInvariant())",
	"-DGODE_TARGET_ARCH=$Architecture"
)

if ($selectedGenerator.NinjaPath -and $selectedGenerator.Name -like "Ninja*") {
	$configureArgs += "-DCMAKE_MAKE_PROGRAM=$($selectedGenerator.NinjaPath)"
}

Write-Host "Configuring gode ($configuration, android/$Architecture, API $ApiLevel) with $($selectedGenerator.Name)..."
Write-Host "Using Android NDK:"
Write-Host "  $ndkDir"
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
	throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host "Building gode ($configuration, android/$Architecture)..."
if ($selectedGenerator.SupportsParallel) {
	& cmake --build $buildDir --target gode --parallel $jobCount
} else {
	& cmake --build $buildDir --target gode
}
if ($LASTEXITCODE -ne 0) {
	throw "CMake build failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path $expectedLibrary)) {
	throw "Build finished, but expected GDExtension library was not found: $expectedLibrary"
}

Write-Host "Built GDExtension library:"
Write-Host "  $expectedLibrary"
