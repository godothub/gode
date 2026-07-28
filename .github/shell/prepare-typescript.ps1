param(
	[string] $OutputDirectory = "",
	[string] $Url = "https://github.com/microsoft/TypeScript/releases/download/v6.0.3/typescript-6.0.3.tgz",
	[string] $Sha256 = "33cd0ee1beaa8c9e9d15a9da836c62ddea4c34a42d7c2d349dbc80d94165d22a",
	[string] $Version = "6.0.3",
	[switch] $Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-RepoRoot {
	$scriptDir = Split-Path -Parent $PSCommandPath
	return (Resolve-Path (Join-Path $scriptDir "../..")).Path
}

$repoRoot = Get-RepoRoot
if (-not $OutputDirectory) {
	$OutputDirectory = Join-Path $repoRoot "example/addons/gode/tsc"
}

$packageJson = Join-Path $OutputDirectory "package.json"
$typescriptRuntime = Join-Path $OutputDirectory "lib/typescript.js"

if (-not $Force -and (Test-Path $packageJson) -and (Test-Path $typescriptRuntime)) {
	$packageText = Get-Content -LiteralPath $packageJson -Raw
	if ($packageText -match ('"version"\s*:\s*"' + [Regex]::Escape($Version) + '"')) {
		Write-Host "TypeScript $Version already prepared at $OutputDirectory"
		exit 0
	}
}

$tar = Get-Command tar -CommandType Application -ErrorAction SilentlyContinue
if (-not $tar) {
	throw "tar was not found. Install bsdtar/Git for Windows or run .github/shell/prepare-typescript.sh from a bash environment."
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("gode-typescript-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null

try {
	$archive = Join-Path $tmpDir "typescript.tgz"
	Write-Host "Downloading TypeScript $Version..."
	$previousProgressPreference = $ProgressPreference
	$ProgressPreference = "SilentlyContinue"
	try {
		$downloadArgs = @{
			Uri = $Url
			OutFile = $archive
		}
		if ($PSVersionTable.PSVersion.Major -lt 6) {
			$downloadArgs.UseBasicParsing = $true
		}
		Invoke-WebRequest @downloadArgs
	} finally {
		$ProgressPreference = $previousProgressPreference
	}

	$actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()
	if ($actualSha256 -ne $Sha256.ToLowerInvariant()) {
		throw "TypeScript archive checksum mismatch.`nexpected: $Sha256`nactual:   $actualSha256"
	}

	if (Test-Path $OutputDirectory) {
		Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
	}
	New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

	& $tar.Source -xzf $archive -C $OutputDirectory --strip-components=1
	if ($LASTEXITCODE -ne 0) {
		throw "Failed to extract TypeScript archive with exit code $LASTEXITCODE."
	}

	if (-not (Test-Path $typescriptRuntime)) {
		throw "TypeScript compiler was not extracted correctly: $typescriptRuntime"
	}

	Write-Host "Prepared TypeScript $Version at $OutputDirectory"
} finally {
	if (Test-Path $tmpDir) {
		Remove-Item -LiteralPath $tmpDir -Recurse -Force
	}
}
