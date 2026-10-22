<#
Installs .deb packages on the iPad over SSH from Windows: copies them with
scp, installs them with dpkg -i and refreshes the home screen with uicache.
deploy.sh does the same from Linux. Uses the OpenSSH client that comes with
Windows 10 and 11.

  powershell -ExecutionPolicy Bypass -File deploy.ps1 -Ip 192.168.0.123 [-Path paquete.deb, carpeta]

With no -Path it installs every .deb in the current folder, such as the
unzipped "ipad-debs" artifact of a PR. The iPad needs its jailbreak active
(re-run Carbon after a reboot) and OpenSSH from Cydia. Without a key, SSH
asks for root's password twice: once to copy, once to install.

-SetupKey first creates an SSH key on this PC if there is none and adds it
to the iPad, so later runs do not ask for the password.
#>
param(
	[Parameter(Mandatory = $true)][string]$Ip,
	[string[]]$Path = @('.'),
	[string]$User = 'root',
	[int]$Port = 22,
	[switch]$SetupKey
)

$ErrorActionPreference = 'Stop'

function Fail([string]$message) {
	Write-Host "deploy: FALLÓ: $message" -ForegroundColor Red
	exit 1
}

foreach ($tool in 'ssh', 'scp') {
	if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
		Fail "falta $tool. Instala el Cliente OpenSSH en Configuración > Sistema > Características opcionales."
	}
}

$debs = @()
foreach ($item in $Path) {
	if (Test-Path -LiteralPath $item -PathType Container) {
		$debs += @(Get-ChildItem -LiteralPath $item -Filter *.deb -File | ForEach-Object { $_.FullName })
	} elseif ((Test-Path -LiteralPath $item -PathType Leaf) -and $item.EndsWith('.deb')) {
		$debs += (Resolve-Path -LiteralPath $item).Path
	} else {
		Fail "$item no es un .deb ni una carpeta"
	}
}
if ($debs.Count -eq 0) {
	Fail "no hay ningún .deb en: $($Path -join ', ')"
}

# The iPad's OpenSSH only offers an ssh-rsa host key, which new clients turn off.
$hostKey = 'HostKeyAlgorithms=+ssh-rsa'
$target = "$User@$Ip"
$hint = "¿El iPad está en la misma red, con el jailbreak activo (Carbon después de reiniciar) y OpenSSH instalado?"

if ($SetupKey) {
	$key = Join-Path $HOME '.ssh\id_ed25519'
	if (-not (Test-Path -LiteralPath "$key.pub")) {
		Write-Host "==> Creando la llave $key (presiona Enter dos veces para dejarla sin frase)"
		New-Item -ItemType Directory -Force -Path (Split-Path $key) | Out-Null
		& ssh-keygen -t ed25519 -f $key
		if ($LASTEXITCODE -ne 0) { Fail 'ssh-keygen' }
	}
	Write-Host "==> Agregando la llave al iPad (pide la contraseña de $User)"
	Get-Content -LiteralPath "$key.pub" | & ssh -p $Port -o $hostKey $target 'mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys'
	if ($LASTEXITCODE -ne 0) { Fail "no se pudo agregar la llave. $hint" }
}

Write-Host "==> Copiando $($debs.Count) paquete(s) a ${target}:/tmp"
& scp -P $Port -o $hostKey @debs "${target}:/tmp/"
if ($LASTEXITCODE -ne 0) { Fail "no se pudo copiar a $Ip. $hint" }

$files = ($debs | ForEach-Object { "'/tmp/$(Split-Path $_ -Leaf)'" }) -join ' '
Write-Host '==> Instalando con dpkg -i y actualizando la pantalla de inicio (uicache)'
# cd /tmp: from root's home (/var/root) mobile cannot read its working directory.
& ssh -p $Port -o $hostKey $target "cd /tmp; dpkg -i $files; s=`$?; rm -f $files; if [ `$s -eq 0 ]; then su mobile -c uicache; s=`$?; fi; exit `$s"
if ($LASTEXITCODE -ne 0) { Fail 'dpkg -i o uicache falló en el iPad; revisa los mensajes de arriba' }

Write-Host "==> deploy: listo, $($debs.Count) paquete(s) en $Ip"
