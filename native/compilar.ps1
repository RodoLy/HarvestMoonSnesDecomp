# compilar.ps1 - compila la version nativa de Harvest Moon en esta PC (Windows).
#
# Lo abre compilar_nativo.bat (doble clic). La primera vez descarga, en
# build\herramientas, un compilador de C (llvm-mingw) y SDL3 (ventana,
# sonido, controles), unos 190 MB en total; despues ya no descarga nada.
# Deja HarvestMoonNative.exe y SDL3.dll en la carpeta del proyecto.
#
# Los chips de la consola (imagen y sonido) se toman de native\chips; si esa
# carpeta no esta, de la recompilacion (HM_RECOMP, o ..\HarvestMoonSNESRecomp).

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'      # Invoke-WebRequest es lentisimo con la barra
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$root  = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $root 'build\herramientas'
New-Item -ItemType Directory -Force $tools | Out-Null

$llvmName = 'llvm-mingw-20251021-ucrt-x86_64'
$llvmUrl  = "https://github.com/mstorsjo/llvm-mingw/releases/download/20251021/$llvmName.zip"
$llvmSha  = '0a0ce4733b54de8c4858ecfc2a08447de57728dccf9fbf7fcba3ce7e0d0c6af8'
$sdlName  = 'SDL3-3.4.10'
$sdlUrl   = 'https://github.com/libsdl-org/SDL/releases/download/release-3.4.10/SDL3-devel-3.4.10-mingw.zip'
$sdlSha   = '5b2bc8589974391e7b3fa4d1bca6c4e09657254afeea57bb4dbe239932188695'

function Get-Tool($url, $sha, $zip, $what) {
  if (Test-Path $zip) {
    if ((Get-FileHash $zip -Algorithm SHA256).Hash -eq $sha) { return }
    Remove-Item $zip
  }
  Write-Host "Descargando $what..."
  Invoke-WebRequest -Uri $url -OutFile "$zip.part" -UseBasicParsing
  if ((Get-FileHash "$zip.part" -Algorithm SHA256).Hash -ne $sha) {
    Remove-Item "$zip.part"
    throw "La descarga de $what no coincide con la esperada (archivo danado o cambiado)."
  }
  Move-Item "$zip.part" $zip
}

$llvmDir = Join-Path $tools $llvmName
if (-not (Test-Path "$llvmDir\bin\x86_64-w64-mingw32-clang.exe")) {
  $zip = Join-Path $tools "$llvmName.zip"
  Get-Tool $llvmUrl $llvmSha $zip 'el compilador (llvm-mingw, 180 MB)'
  Write-Host 'Descomprimiendo el compilador (un par de minutos)...'
  tar -xf $zip -C $tools
  if ($LASTEXITCODE -ne 0) { throw 'No se pudo descomprimir el compilador.' }
}
$sdlDir = Join-Path $tools $sdlName
if (-not (Test-Path "$sdlDir\x86_64-w64-mingw32\bin\SDL3.dll")) {
  $zip = Join-Path $tools 'SDL3-devel-3.4.10-mingw.zip'
  Get-Tool $sdlUrl $sdlSha $zip 'SDL3 (5 MB)'
  tar -xf $zip -C $tools
  if ($LASTEXITCODE -ne 0) { throw 'No se pudo descomprimir SDL3.' }
}

# los chips de la consola y hm_ram.h
$chips = Join-Path $root 'native\chips'
if (-not (Test-Path "$chips\snes\ppu.c")) {
  $recomp = $env:HM_RECOMP
  if (-not $recomp) {
    foreach ($c in @('..\HarvestMoonSNESRecomp', '..\HarvestMoonRecomp')) {
      $p = Join-Path $root $c
      if (Test-Path "$p\snesrecomp\runner\src\snes\ppu.c") { $recomp = $p; break }
    }
  }
  if (-not $recomp) { throw 'Faltan los chips de la consola: pon la carpeta native\chips, o la recompilacion (HarvestMoonSNESRecomp) junto a esta carpeta.' }
  $r = Join-Path $recomp 'snesrecomp\runner\src'
  New-Item -ItemType Directory -Force "$chips\snes" | Out-Null
  foreach ($f in 'ppu','ppu_legacy','ws_shadow','dma','sdd1','apu','spc','dsp','dsp_shadow','audio_shadow','color_lut') {
    Copy-Item "$r\snes\$f.c" "$chips\snes\"
  }
  Copy-Item "$r\snes\*.h" "$chips\snes\"
  foreach ($f in 'audio_trace.c','audio_trace.h','types.h','debug_server.h') { Copy-Item "$r\$f" "$chips\" }
  Copy-Item "$recomp\src\hm_ram.h" "$chips\"
}

Push-Location $root
try {
  $cc  = "$llvmDir\bin\x86_64-w64-mingw32-clang.exe"
  $sdl = "$sdlDir\x86_64-w64-mingw32"
  $src = @('native\main.c', 'native\hw.c')
  $src += Get-ChildItem 'src\*.c' | Where-Object { $_.Name -ne 'hm_decomp.c' -and $_.Name -ne 'hm_io.c' } | ForEach-Object { "src\$($_.Name)" }
  $src += Get-ChildItem 'native\chips\snes\*.c' | ForEach-Object { "native\chips\snes\$($_.Name)" }
  $src += 'native\chips\audio_trace.c'
  Write-Host "Compilando $($src.Count) archivos..."
  & $cc -O2 -std=gnu11 -w -Inative -Isrc -Inative\chips -Inative\chips\snes "-I$sdl\include" @src "-L$sdl\lib" -lSDL3 -mwindows -o HarvestMoonNative.exe
  if ($LASTEXITCODE -ne 0) { throw 'La compilacion fallo (mira los mensajes de arriba).' }
  Copy-Item "$sdl\bin\SDL3.dll" $root -Force
  Write-Host ''
  Write-Host 'Listo: HarvestMoonNative.exe (y SDL3.dll, que va junto a el).'
} finally {
  Pop-Location
}
