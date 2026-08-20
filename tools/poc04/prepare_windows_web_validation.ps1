param(
  [string]$OutRoot = "out/poc04-windows-web-revalidation"
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
Set-Location $Repo

$Profile = "tools/skia/profiles/poc04-richtext-v2.json"
$Lock = "pocs/rich_text/skia-sdk.lock.json"
$Install = ".deps/skia-sdk-poc04"
$WindowsOut = Join-Path $OutRoot "windows"
$WebOut = Join-Path $OutRoot "web"
$ChromeOut = Join-Path $OutRoot "chrome-recorder"

Write-Host "Preparing POC-04 Windows/Chrome physical validation from $(git rev-parse HEAD)"
python tools/bootstrap_deps.py --core --windows-llvm --web

$Llvm = Join-Path $Repo ".deps/llvm/bin/clang-cl.exe"
if (-not (Test-Path $Llvm)) {
  $Installer = Resolve-Path ".deps/downloads/LLVM-22.1.8-win64.exe"
  $InstallDir = Join-Path $Repo ".deps/llvm"
  Start-Process $Installer -ArgumentList "/S", "/D=$InstallDir" -Wait
}

python tools/skia/fetch.py --profile $Profile --lock $Lock --install-root $Install --target windows-x64-d3d12
python tools/skia/fetch.py --profile $Profile --lock $Lock --install-root $Install --target web-wasm-webgl2

cmake -S pocs/rich_text -B $WindowsOut -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_C_COMPILER=$Llvm" `
  "-DCMAKE_CXX_COMPILER=$Llvm" `
  -DCANVAS_POC04_ENABLE_SKPARAGRAPH=ON `
  -DCANVAS_POC04_BUILD_WINDOWS=ON
cmake --build $WindowsOut --parallel
ctest --test-dir $WindowsOut --output-on-failure
& (Join-Path $WindowsOut "canvas_poc04_canonical_behavior_report.exe") `
  --platform=windows `
  "--output=$(Join-Path $WindowsOut 'windows-behavior.json')"

$Toolchain = Join-Path $Repo ".deps/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
$WebConfigure = "call .deps\emsdk\emsdk_env.bat >nul && cmake -S pocs\rich_text -B `"$WebOut`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=`"$Toolchain`" -DCANVAS_POC04_BUILD_TESTS=OFF -DCANVAS_POC04_ENABLE_SKPARAGRAPH=ON -DCANVAS_POC04_BUILD_WEB=ON && cmake --build `"$WebOut`" --parallel"
cmd /d /s /c $WebConfigure
if ($LASTEXITCODE -ne 0) { throw "Web/WASM build failed" }

Push-Location pocs/rich_text/platform/web
npm ci
npm run build
npm run build:physical
Pop-Location

New-Item -ItemType Directory -Force $ChromeOut | Out-Null
Copy-Item pocs/rich_text/platform/web/physical_recorder.html $ChromeOut
Copy-Item pocs/rich_text/platform/web/physical-dist/physical_recorder.js $ChromeOut
Copy-Item pocs/rich_text/platform/web/physical-dist/text_input_adapter.js $ChromeOut
Copy-Item (Join-Path $WebOut "platform/web/canvas_poc04_web.js") $ChromeOut
Copy-Item (Join-Path $WebOut "platform/web/canvas_poc04_web.wasm") $ChromeOut
Copy-Item (Join-Path $WebOut "platform/web/canvas_poc04_web.data") $ChromeOut

$WindowsDemo = (Resolve-Path (Join-Path $WindowsOut "platform/windows/canvas_poc04_windows_demo.exe")).Path
$WindowsEvidence = (Join-Path $Repo (Join-Path $WindowsOut "windows-ime.json"))
$ChromeDirectory = (Resolve-Path $ChromeOut).Path

Write-Host ""
Write-Host "READY: Windows IMM recorder"
Write-Host "  `$env:CANVAS_POC04_IME_EVIDENCE_PATH = '$WindowsEvidence'"
Write-Host "  & '$WindowsDemo'"
Write-Host ""
Write-Host "READY: Chrome Stable recorder"
Write-Host "  python -m http.server 4173 --directory '$ChromeDirectory'"
Write-Host "  Open http://127.0.0.1:4173/physical_recorder.html in Chrome Stable"
