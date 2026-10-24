<#
Downloads the iPad .deb packages that CI built for main or for a PR and
installs them on the iPad with deploy.ps1, without asking anything: made for
sessions that run on the PC with nobody at the keyboard. Needs only a copy of
the repo and the OpenSSH client of Windows 10 and 11.

  powershell -ExecutionPolicy Bypass -File ios\toolchain\fetch-debs.ps1 [-Pr 93] [-Package chiakiport] [-Ip ipad] [-NoDeploy]

Without -Pr it takes the build of main; with -Pr N, the build of PR N. The
packages come from the public pre-release that ios-ci.yml publishes for each
(ipad-main, ipad-pr-<N>), which downloads without a GitHub sign-in. A PR from
a fork has none: for those it downloads the run's "ipad-debs" artifact with
gh, which needs "gh auth login" once.

It installs only the build of the latest commit. While that commit is still
building, or if its iPad build failed, it stops and says so; -AllowStale
installs the previous build instead.

-Package keeps only the .debs whose name contains that text, such as
chiakiport or ckdirect, and takes several. -Ip is the iPad's address or SSH
alias ("ipad" by default). -NoDeploy only downloads and prints the paths. The
.debs land in -OutDir, by default a folder under %TEMP% that each run empties
first. deploy.ps1 runs with -BatchMode, so SSH never asks for a password: the
iPad needs this PC's key (deploy.ps1 -SetupKey, once).
#>
param(
	[int]$Pr = 0,
	[string[]]$Package = @(),
	[string]$Ip = 'ipad',
	[string]$OutDir = '',
	[string]$Repo = 'HuAxX27/IpadMini-Chiakiport',
	[switch]$AllowStale,
	[switch]$NoDeploy
)

$ErrorActionPreference = 'Stop'
# Invoke-WebRequest is many times slower while it draws its progress bar.
$ProgressPreference = 'SilentlyContinue'
# Windows PowerShell 5.1 can still default to TLS 1.0, which GitHub refuses.
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

function Fail([string]$message) {
	Write-Host "fetch-debs: FALLÓ: $message" -ForegroundColor Red
	exit 1
}

# GitHub's API without a sign-in: 60 requests an hour per IP, three per run.
function Get-GitHub([string]$path) {
	try {
		return Invoke-RestMethod -UseBasicParsing -Uri "https://api.github.com/repos/$Repo/$path" -Headers @{ Accept = 'application/vnd.github+json' }
	} catch {
		$status = 0
		if ($_.Exception.Response) { $status = [int]$_.Exception.Response.StatusCode }
		if ($status -eq 404) { return $null }
		Fail "GitHub no contestó $path ($status): $($_.Exception.Message). ¿Hay internet? Con 403, se acabaron las 60 consultas por hora: espera un rato."
	}
}

function Test-Wanted([string]$name) {
	if (-not $name.EndsWith('.deb')) { return $false }
	if ($Package.Count -eq 0) { return $true }
	foreach ($p in $Package) {
		if ($name -like "*$p*") { return $true }
	}
	return $false
}

function Get-Short([string]$sha) {
	if ($sha.Length -gt 7) { return $sha.Substring(0, 7) }
	return $sha
}

if ($Pr -gt 0) {
	$tag = "ipad-pr-$Pr"
	$of = "del PR #$Pr"
	$pull = Get-GitHub "pulls/$Pr"
	if (-not $pull) { Fail "no existe el PR #$Pr en $Repo" }
	$head = $pull.head.sha
	# A fork's PR gets no pre-release; a deleted fork leaves head.repo empty.
	$fork = (-not $pull.head.repo) -or ($pull.head.repo.full_name -ne $Repo)
} else {
	$tag = 'ipad-main'
	$of = 'de main'
	$head = (Get-GitHub 'git/ref/heads/main').object.sha
	$fork = $false
}

if (-not $OutDir) {
	$OutDir = Join-Path ([IO.Path]::GetTempPath()) (Join-Path 'ck-debs' $tag)
	if (Test-Path -LiteralPath $OutDir) {
		Get-ChildItem -LiteralPath $OutDir -Filter *.deb -File | Remove-Item -Force
	}
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$files = @()
$release = Get-GitHub "releases/tags/$tag"
if ($release) {
	$ref = Get-GitHub "git/ref/tags/$tag"
	$built = ''
	if ($ref) { $built = $ref.object.sha }
	Write-Host "==> $($release.name): $($release.html_url)"
	if ($built -ne $head) {
		$why = "lo último $of es $(Get-Short $head), pero el pre-release tiene $(Get-Short $built): el CI todavía lo está armando (tarda unos 3 minutos) o su build del iPad falló"
		if (-not $AllowStale) { Fail "$why. Vuelve a correrlo en unos minutos, o agrega -AllowStale para instalar el build anterior." }
		Write-Host "AVISO: $why. Se instala el build anterior (-AllowStale)." -ForegroundColor Yellow
	}

	$assets = @($release.assets | Where-Object { Test-Wanted $_.name })
	if ($assets.Count -eq 0) {
		Fail "ningún .deb de $tag coincide con '$($Package -join "', '")'. Hay: $(($release.assets | ForEach-Object { $_.name }) -join ', ')"
	}
	foreach ($asset in $assets) {
		$file = Join-Path $OutDir $asset.name
		Write-Host "==> Bajando $($asset.name) ($([math]::Ceiling($asset.size / 1KB)) KB)"
		try {
			Invoke-WebRequest -UseBasicParsing -Uri $asset.browser_download_url -OutFile $file
		} catch {
			Fail "no se pudo bajar $($asset.name): $($_.Exception.Message)"
		}
		if ((Get-Item -LiteralPath $file).Length -ne $asset.size) {
			Fail "$($asset.name) llegó incompleto; vuelve a correrlo"
		}
		# GitHub lists the sha256 of each asset it stores.
		if ($asset.digest -and $asset.digest.StartsWith('sha256:')) {
			$hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
			if ($hash -ne $asset.digest.Substring(7)) { Fail "el sha256 de $($asset.name) no coincide con el de GitHub" }
		}
		$files += $file
	}
} elseif (-not $fork) {
	Fail "todavía no hay pre-release $tag. El CI $of no ha terminado (tarda unos 3 minutos) o su build del iPad falló: vuelve a correrlo en unos minutos."
} else {
	if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
		Fail "El PR #$Pr viene de un fork, que no publica pre-release, y para bajar su artefacto hace falta gh: instálalo (winget install GitHub.cli) y corre gh auth login una vez."
	}
	Write-Host "==> El PR #$Pr viene de un fork: bajando el artefacto ipad-debs de $(Get-Short $head) con gh"
	$runId = & gh run list -R $Repo --workflow ios-ci.yml --commit $head --limit 1 --json databaseId --jq '.[0].databaseId'
	if ($LASTEXITCODE -ne 0) { Fail 'gh run list falló; si dice que falta iniciar sesión, corre gh auth login una vez' }
	if (-not $runId) { Fail "no hay una corrida de iOS CI para $(Get-Short $head) $of" }
	$download = Join-Path ([IO.Path]::GetTempPath()) "ck-debs-gh-$runId"
	if (Test-Path -LiteralPath $download) { Remove-Item -LiteralPath $download -Recurse -Force }
	& gh run download $runId -R $Repo -n ipad-debs -D $download
	if ($LASTEXITCODE -ne 0) { Fail "gh no pudo bajar ipad-debs de la corrida $runId (arriba dice por qué): sigue corriendo, su build del iPad falló o falta gh auth login" }
	foreach ($deb in @(Get-ChildItem -LiteralPath $download -Filter *.deb -File)) {
		if (Test-Wanted $deb.Name) {
			$file = Join-Path $OutDir $deb.Name
			Move-Item -LiteralPath $deb.FullName -Destination $file -Force
			$files += $file
		}
	}
	Remove-Item -LiteralPath $download -Recurse -Force
	if ($files.Count -eq 0) { Fail "ningún .deb de la corrida $runId coincide con '$($Package -join "', '")'" }
}

Write-Host "==> $($files.Count) paquete(s) $of ($(Get-Short $head)) en $OutDir"
$files | ForEach-Object { Write-Host "    $(Split-Path $_ -Leaf)" }
if ($NoDeploy) { exit 0 }

& (Join-Path $PSScriptRoot 'deploy.ps1') -Ip $Ip -Path $files -BatchMode
exit $LASTEXITCODE
