param(
	[ValidateSet("x64", "arm64")]
	[string] $Architecture = "x64",

	[string] $Generator = "",

	[int] $Jobs = 0,

	[switch] $Fresh
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-RepoRoot {
	$scriptDir = Split-Path -Parent $PSCommandPath
	return (Resolve-Path (Join-Path $scriptDir "..")).Path
}

function Get-VisualStudioCppTools {
	$vswhereCandidates = @(
		"${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
		"${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
	)

	$vswhere = $vswhereCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
	if (-not $vswhere) {
		return $null
	}

	$json = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json
	if ($LASTEXITCODE -ne 0 -or -not $json) {
		return $null
	}

	$instances = $json | ConvertFrom-Json
	if (-not $instances -or $instances.Count -eq 0) {
		return $null
	}

	return $instances[0]
}

function Find-Ninja {
	param(
		[object] $VisualStudioInstance
	)

	$pathNinja = Get-Command ninja.exe -ErrorAction SilentlyContinue
	if ($pathNinja) {
		return $pathNinja.Source
	}

	if ($VisualStudioInstance) {
		$vsNinja = Join-Path $VisualStudioInstance.installationPath "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
		if (Test-Path $vsNinja) {
			return $vsNinja
		}
	}

	return ""
}

function Select-CMakeGenerator {
	param(
		[string] $RequestedGenerator,
		[object] $VisualStudioInstance
	)

	if ($RequestedGenerator) {
		return @{
			Name = $RequestedGenerator
			NinjaPath = ""
			SupportsParallel = $RequestedGenerator -like "Ninja*"
		}
	}

	$ninjaPath = Find-Ninja -VisualStudioInstance $VisualStudioInstance
	if ($ninjaPath) {
		return @{
			Name = "Ninja"
			NinjaPath = $ninjaPath
			SupportsParallel = $true
		}
	}

	return @{
		Name = "NMake Makefiles"
		NinjaPath = ""
		SupportsParallel = $false
	}
}

function Import-VisualStudioEnvironment {
	param(
		[string] $InstallationPath,
		[string] $Arch
	)

	$vsDevCmd = Join-Path $InstallationPath "Common7/Tools/VsDevCmd.bat"
	if (-not (Test-Path $vsDevCmd)) {
		throw "VsDevCmd.bat was not found: $vsDevCmd"
	}

	$cmdArch = if ($Arch -eq "arm64") { "arm64" } else { "x64" }
	$cmd = "`"$vsDevCmd`" -arch=$cmdArch -host_arch=x64 && set"
	$environment = & cmd.exe /s /c $cmd
	if ($LASTEXITCODE -ne 0) {
		throw "Failed to initialize the Visual Studio developer environment."
	}

	foreach ($line in $environment) {
		if ($line -match "^([^=]+)=(.*)$") {
			[Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
		}
	}
}

function Get-VisualStudioToolPath {
	param(
		[object] $VisualStudioInstance,
		[string] $Arch
	)

	$hostArch = "HostX64"
	$targetArch = if ($Arch -eq "arm64") { "arm64" } else { "x64" }
	$msvcRoot = Join-Path $VisualStudioInstance.installationPath "VC/Tools/MSVC"
	$toolset = Get-ChildItem -Path $msvcRoot -Directory |
		Sort-Object Name -Descending |
		Select-Object -First 1

	if (-not $toolset) {
		throw "No MSVC toolset was found under: $msvcRoot"
	}

	$toolPath = Join-Path $toolset.FullName "bin/$hostArch/$targetArch"
	if (-not (Test-Path (Join-Path $toolPath "cl.exe"))) {
		throw "cl.exe was not found under: $toolPath"
	}

	return $toolPath
}

function Get-WindowsSdkBinPath {
	$sdkBinRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
	if (-not (Test-Path $sdkBinRoot)) {
		return ""
	}

	$sdkBin = Get-ChildItem -Path $sdkBinRoot -Directory |
		Sort-Object Name -Descending |
		ForEach-Object { Join-Path $_.FullName "x64" } |
		Where-Object { Test-Path (Join-Path $_ "mt.exe") } |
		Select-Object -First 1

	if ($sdkBin) {
		return $sdkBin
	}

	return ""
}

function Get-DefaultJobCount {
	$cpuCount = [Environment]::ProcessorCount
	if ($cpuCount -le 2) {
		return 1
	}

	return [Math]::Max(1, $cpuCount - 1)
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

function Clear-CMakeConfigureState {
	param([string] $BuildDirectory)

	Assert-PathInside -ChildPath $BuildDirectory -ParentPath (Join-Path $repoRoot "build")

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
$visualStudioInstance = Get-VisualStudioCppTools
$selectedGenerator = Select-CMakeGenerator -RequestedGenerator $Generator -VisualStudioInstance $visualStudioInstance
$buildDir = Join-Path $repoRoot "build/windows/$Architecture"
$binDir = Join-Path $repoRoot "example/addons/gode/binary/windows/$Architecture"
$expectedLibrary = Join-Path $binDir "libgode.dll"
$libnodeLibrary = Join-Path $repoRoot "libnode/windows/$Architecture/libnode.lib"
$jobCount = if ($Jobs -gt 0) { $Jobs } else { Get-DefaultJobCount }
$configuration = "Release"

if (-not (Test-Path $libnodeLibrary)) {
	throw "Missing libnode import library: $libnodeLibrary"
}

if (-not $visualStudioInstance) {
	throw "Visual Studio C++ tools were not found. Install the Desktop development with C++ workload."
}
Import-VisualStudioEnvironment -InstallationPath $visualStudioInstance.installationPath -Arch $Architecture

$visualStudioToolPath = Get-VisualStudioToolPath -VisualStudioInstance $visualStudioInstance -Arch $Architecture
$windowsSdkBinPath = Get-WindowsSdkBinPath
$pathPrefix = @($visualStudioToolPath, $windowsSdkBinPath) | Where-Object { $_ }

if ($selectedGenerator.NinjaPath) {
	$ninjaDir = Split-Path -Parent $selectedGenerator.NinjaPath
	$pathPrefix = @($ninjaDir) + $pathPrefix
}
$env:PATH = (($pathPrefix | Select-Object -Unique) -join ";") + ";$env:PATH"

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
if ($Fresh) {
	Clear-CMakeConfigureState -BuildDirectory $buildDir
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
	"-DCMAKE_C_COMPILER=$(Join-Path $visualStudioToolPath 'cl.exe')",
	"-DCMAKE_CXX_COMPILER=$(Join-Path $visualStudioToolPath 'cl.exe')",
	"-DGODE_TARGET_ARCH=$Architecture"
)

Write-Host "Configuring gode ($configuration, windows/$Architecture) with $($selectedGenerator.Name)..."
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
	throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host "Building gode ($configuration, windows/$Architecture)..."
if ($selectedGenerator.SupportsParallel) {
	& cmake --build $buildDir --target gode --parallel $jobCount
} else {
	Write-Host "NMake does not support parallel builds; install/use Ninja for multi-threaded builds."
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
