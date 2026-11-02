# build_android.ps1 - builds the Android version (an APK) on this PC.
#
# Opened by build_android.bat (double-click). The first time it downloads,
# into build\tools: a Java JDK (Microsoft's, about 190 MB), Android's
# command-line tools, and through them the Android SDK pieces the build needs
# (platform 35, build-tools, the NDK, CMake: about 2 GB), SDL3's source and
# Gradle. Android's license is shown first: read it and answer y. Then it
# builds HarvestMoon.apk in the project folder (the first time 10-20
# minutes) and, if an Android device is connected over USB with debugging
# on, offers to install it.
#
# Everything it writes stays in build\ (and Gradle's cache in your user
# folder, .gradle). Its messages also go to build\android.log.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'      # Invoke-WebRequest is very slow with the bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$root  = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $root 'build\tools'
$old = Join-Path $root 'build\herramientas'   # the folder's name before 1.0 / su nombre antes de la 1.0
if ((Test-Path $old) -and -not (Test-Path $tools)) { Move-Item $old $tools
  # the native build's cache points at the old folder: built again
  foreach ($d in 'android\app\.cxx', 'android\app\build') { $p = Join-Path $root $d; if (Test-Path $p) { Remove-Item -Recurse -Force $p } }
}
New-Item -ItemType Directory -Force $tools | Out-Null
$log = Join-Path $root 'build\android.log'
Start-Transcript -Path $log -Force | Out-Null

function Say($en, $es) { Write-Host ''; Write-Host "== $en / $es" }

function Get-File($url, $out, $what) {
  Write-Host "Downloading / Descargando $what..."
  Invoke-WebRequest -Uri $url -OutFile "$out.part" -UseBasicParsing
  Move-Item -Force "$out.part" $out
}

function Test-Hash($file, $algo, $expected, $what) {
  $got = (Get-FileHash $file -Algorithm $algo).Hash
  if ($got -ne $expected.Trim().ToUpper()) {
    Remove-Item $file
    throw "The download of $what is not the expected file / La descarga de $what no coincide con la esperada."
  }
}

function Expand-Here($zip, $dest) {
  New-Item -ItemType Directory -Force $dest | Out-Null
  tar -xf $zip -C $dest
  if ($LASTEXITCODE -ne 0) { throw "Could not unpack / No se pudo descomprimir $zip" }
}

try {
  $arm = ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') -or ($env:PROCESSOR_ARCHITEW6432 -eq 'ARM64')

  # 1. Java (Microsoft Build of OpenJDK 17)
  Say 'Java' 'Java'
  $jdkVer = '17.0.21.0.2'
  $jdkArch = if ($arm) { 'aarch64' } else { 'x64' }
  $jdkName = "microsoft-jdk-$jdkVer-windows-$jdkArch"
  $jdkDir = Join-Path $tools $jdkName
  $javaHome = Get-ChildItem $jdkDir -Directory -ErrorAction SilentlyContinue | Where-Object { Test-Path "$($_.FullName)\bin\java.exe" } | Select-Object -First 1
  if (-not $javaHome) {
    $zip = Join-Path $tools "$jdkName.zip"
    $url = "https://aka.ms/download-jdk/$jdkName.zip"
    Get-File $url $zip 'Java (190 MB)'
    $sum = (Invoke-WebRequest -Uri "$url.sha256sum.txt" -UseBasicParsing).Content
    if ($sum -is [byte[]]) { $sum = [Text.Encoding]::ASCII.GetString($sum) }
    Test-Hash $zip 'SHA256' (($sum -split '\s+')[0]) 'Java'
    Expand-Here $zip $jdkDir
    Remove-Item $zip
    $javaHome = Get-ChildItem $jdkDir -Directory | Where-Object { Test-Path "$($_.FullName)\bin\java.exe" } | Select-Object -First 1
    if (-not $javaHome) { throw 'Java is not where it should be / Java no quedo donde debia.' }
  }
  $env:JAVA_HOME = $javaHome.FullName
  $env:Path = "$env:JAVA_HOME\bin;$env:Path"

  # 2. Android's command-line tools (the latest, from Google's own index)
  Say 'Android SDK' 'SDK de Android'
  $sdk = Join-Path $tools 'android-sdk'
  $sdkm = Join-Path $sdk 'cmdline-tools\latest\bin\sdkmanager.bat'
  if (-not (Test-Path $sdkm)) {
    $base = 'https://dl.google.com/android/repository/'
    $raw = (Invoke-WebRequest -Uri ($base + 'repository2-3.xml') -UseBasicParsing).Content
    if ($raw -is [byte[]]) { $raw = [Text.Encoding]::UTF8.GetString($raw) }
    [xml]$repo = $raw
    $pkg = $repo.SelectNodes("//*[local-name()='remotePackage']") | Where-Object { $_.GetAttribute('path') -eq 'cmdline-tools;latest' } | Select-Object -First 1
    if (-not $pkg) { throw "Android's tools are not in Google's index / Las herramientas de Android no estan en el indice de Google." }
    $arc = $pkg.SelectNodes(".//*[local-name()='archive']") | Where-Object {
      $h = $_.SelectSingleNode("*[local-name()='host-os']"); $h -and $h.InnerText -eq 'windows' } | Select-Object -First 1
    $done = $arc.SelectSingleNode("*[local-name()='complete']")
    $file = $done.SelectSingleNode("*[local-name()='url']").InnerText
    $check = $done.SelectSingleNode("*[local-name()='checksum']")
    $zip = Join-Path $tools $file
    Get-File ($base + $file) $zip "Android's tools (150 MB)"
    $algo = if ($check.GetAttribute('type') -match '256') { 'SHA256' } else { 'SHA1' }
    Test-Hash $zip $algo $check.InnerText "Android's tools"
    $tmp = Join-Path $tools 'cmdline-tmp'
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    Expand-Here $zip $tmp
    New-Item -ItemType Directory -Force (Join-Path $sdk 'cmdline-tools') | Out-Null
    Move-Item (Join-Path $tmp 'cmdline-tools') (Join-Path $sdk 'cmdline-tools\latest')
    Remove-Item -Recurse -Force $tmp
    Remove-Item $zip
  }
  if (-not (Test-Path (Join-Path $sdk 'licenses\android-sdk-license'))) {
    Say "Android's license: read it and answer y" 'La licencia de Android: leela y contesta y'
    & $sdkm "--sdk_root=$sdk" --licenses
  }
  $ndk = '28.2.13676358'
  if (-not (Test-Path (Join-Path $sdk "ndk\$ndk\source.properties")) -or -not (Test-Path (Join-Path $sdk 'platforms\android-35')) -or
      -not (Test-Path (Join-Path $sdk 'cmake\3.22.1')) -or -not (Test-Path (Join-Path $sdk 'build-tools\35.0.0'))) {
    Say 'Android SDK pieces (about 2 GB, the first time)' 'Piezas del SDK de Android (unos 2 GB, la primera vez)'
    & $sdkm "--sdk_root=$sdk" 'platform-tools' 'platforms;android-35' 'build-tools;35.0.0' "ndk;$ndk" 'cmake;3.22.1'
    if ($LASTEXITCODE -ne 0) { throw 'The Android SDK could not be installed / No se pudo instalar el SDK de Android.' }
  }
  $env:ANDROID_HOME = $sdk
  $env:ANDROID_SDK_ROOT = $sdk

  # 3. SDL3's source (its C is built with the game; its Java goes into the app)
  Say 'SDL3' 'SDL3'
  $sdlName = 'SDL3-3.4.10'
  $sdlDir = Join-Path $root 'build\sdl'
  if (-not (Test-Path (Join-Path $sdlDir "$sdlName\android-project\app\src\main\java\org\libsdl\app\SDLActivity.java"))) {
    $tgz = Join-Path $tools "$sdlName.tar.gz"
    Get-File "https://github.com/libsdl-org/SDL/releases/download/release-3.4.10/$sdlName.tar.gz" $tgz 'SDL3 (15 MB)'
    Test-Hash $tgz 'SHA256' '12b34280415ec8418c864408b93d008a20a6530687ee613d60bfbd20411f2785' 'SDL3'
    Expand-Here $tgz $sdlDir
    Remove-Item $tgz
  }
  $env:HM_SDL3_SOURCE_DIR = Join-Path $sdlDir $sdlName

  # 4. Gradle
  Say 'Gradle' 'Gradle'
  $gName = 'gradle-8.12'
  $gradle = Join-Path $tools "$gName\bin\gradle.bat"
  if (-not (Test-Path $gradle)) {
    $zip = Join-Path $tools "$gName-bin.zip"
    $url = "https://services.gradle.org/distributions/$gName-bin.zip"
    Get-File $url $zip 'Gradle (130 MB)'
    $sum = (Invoke-WebRequest -Uri "$url.sha256" -UseBasicParsing).Content
    if ($sum -is [byte[]]) { $sum = [Text.Encoding]::ASCII.GetString($sum) }
    Test-Hash $zip 'SHA256' (($sum -split '\s+')[0]) 'Gradle'
    Expand-Here $zip $tools
    Remove-Item $zip
  }

  # 5. A key of your own to sign the app (made once; keep it: an update
  #    installs over the old app only when signed with the same key)
  $ks = Join-Path $tools 'harvestmoon.jks'
  if (-not (Test-Path $ks)) {
    & "$env:JAVA_HOME\bin\keytool.exe" -genkeypair -keystore $ks -alias harvestmoon -keyalg RSA -keysize 2048 -validity 10000 `
      -storepass harvestmoon -keypass harvestmoon -dname 'CN=Harvest Moon native build' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'The signing key could not be made / No se pudo crear la llave para firmar.' }
  }

  # 6. The app
  Say 'Building the app (the first time 10-20 minutes)' 'Compilando la app (la primera vez 10-20 minutos)'
  & $gradle -p (Join-Path $root 'android') --no-daemon assembleRelease "-PhmKeystore=$ks" '-PhmPassword=harvestmoon'
  if ($LASTEXITCODE -ne 0) { throw 'The build failed, see the messages above / La compilacion fallo, mira los mensajes de arriba.' }
  $apk = Join-Path $root 'android\app\build\outputs\apk\release\app-release.apk'
  if (-not (Test-Path $apk)) { throw 'No APK came out / No salio el APK.' }
  Copy-Item -Force $apk (Join-Path $root 'HarvestMoon.apk')
  Say 'Done: HarvestMoon.apk is in the project folder' 'Listo: HarvestMoon.apk esta en la carpeta del proyecto'
  Write-Host 'Copy it to the phone or the AYN Thor and open it to install it (allow installing from that app when asked).'
  Write-Host 'Copialo al telefono o a la AYN Thor y abrelo para instalarlo (permite instalar desde esa app si lo pide).'

  # 7. A device connected over USB (with USB debugging on): install it now?
  $adb = Join-Path $sdk 'platform-tools\adb.exe'
  if (Test-Path $adb) {
    $devs = & $adb devices | Select-String "`tdevice$"
    if ($devs) {
      $a = Read-Host 'An Android device is connected. Install it there now? / Hay un Android conectado. Instalarla ahi ahora? (y/n)'
      if ($a -match '^[yYsS]') { & $adb install -r (Join-Path $root 'HarvestMoon.apk') }
    }
  }
} catch {
  Write-Host ''
  Write-Host "ERROR: $_"
  Write-Host "The messages are also in / Los mensajes tambien estan en: $log"
} finally {
  Stop-Transcript | Out-Null
}
