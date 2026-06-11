param(
  [string]$Proxy = "",
  [string]$QtVersion = "6.9.3",
  [string]$Configuration = "Release",
  [string]$InnoSetupDir = "C:\Users\jairy\AppData\Local\Programs\Inno Setup 6",
  [switch]$Reconfigure,
  [switch]$NoInstaller,
  [switch]$SkipQtInstall,
  [switch]$CheckOnly,
  [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $RepoRoot "build"
$LogDir = Join-Path $BuildDir "logs"
$QtRoot = Join-Path $BuildDir "Qt\$QtVersion\msvc2022_64"
$QtCoreDll = Join-Path $QtRoot "bin\Qt6Core.dll"
$QtWebEngineWidgetsConfig =
  Join-Path $QtRoot "lib\cmake\Qt6WebEngineWidgets\Qt6WebEngineWidgetsConfig.cmake"
$QtWebChannelConfig =
  Join-Path $QtRoot "lib\cmake\Qt6WebChannel\Qt6WebChannelConfig.cmake"
$QtPositioningConfig =
  Join-Path $QtRoot "lib\cmake\Qt6Positioning\Qt6PositioningConfig.cmake"
$UserProfileDir = Split-Path -Parent (Split-Path -Parent $RepoRoot)
$UserHomeDrive = Split-Path -Qualifier $UserProfileDir
$UserHomePath = $UserProfileDir.Substring($UserHomeDrive.Length)
$VenvPython = Join-Path $RepoRoot ".venv\Scripts\python.exe"
$VsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$VsCMake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$VsCPack = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cpack.exe"
$Iscc = Join-Path $InnoSetupDir "ISCC.exe"
$InnoScript = Join-Path $RepoRoot "packaging\win-installer\flameshot-inno.iss"
$PackageRoot =
  Join-Path $RepoRoot "build\Package\_CPack_Packages\win64\ZIP\flameshot-14.0.0-win64"
$PackageBin = Join-Path $PackageRoot "bin"
$PackageKatexDist = Join-Path $PackageRoot "share\katex\dist"
$SourceKatexDist = Join-Path $RepoRoot "data\katex\dist"

function Write-Step {
  param([string]$Message)
  Write-Host ""
  Write-Host "==> $Message" -ForegroundColor Cyan
}

function Require-File {
  param([string]$Path, [string]$Name)
  if (-not (Test-Path -LiteralPath $Path)) {
    throw "$Name not found: $Path"
  }
}

function Get-HostPython {
  $candidates = @(
    "C:\Users\jairy\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
    "C:\Program Files\Python312\python.exe",
    "C:\Program Files\Python314\python.exe",
    "C:\Program Files\Python311\python.exe"
  )

  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate) {
      return $candidate
    }
  }

  $pythonCmd = Get-Command python.exe -ErrorAction SilentlyContinue
  if ($pythonCmd) {
    return $pythonCmd.Source
  }

  throw "Python was not found. Install Python 3.12 or keep the existing .venv directory."
}

function Invoke-CleanCmd {
  param(
    [string]$Name,
    [string[]]$Lines,
    [switch]$SkipVsEnvironment
  )

  New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
  $safeName = ($Name -replace "[^A-Za-z0-9_.-]", "-")
  $cmdPath = Join-Path $LogDir "$safeName.cmd"

  $content = @(
    "@echo on",
    "set `"PATH=`"",
    "set `"Path=C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0;C:\Windows\System32\OpenSSH;C:\Program Files\Git\cmd;C:\Program Files\dotnet;$InnoSetupDir`"",
    "set `"SystemRoot=C:\Windows`"",
    "set `"WINDIR=C:\Windows`"",
    "set `"ComSpec=C:\Windows\System32\cmd.exe`"",
    "set `"TEMP=$([System.IO.Path]::GetTempPath().TrimEnd("\"))`"",
    "set `"TMP=$([System.IO.Path]::GetTempPath().TrimEnd("\"))`"",
    "set `"USERPROFILE=$UserProfileDir`"",
    "set `"HOMEDRIVE=$UserHomeDrive`"",
    "set `"HOMEPATH=$UserHomePath`"",
    "set `"APPDATA=$UserProfileDir\AppData\Roaming`"",
    "set `"LOCALAPPDATA=$UserProfileDir\AppData\Local`"",
    "set `"ProgramData=C:\ProgramData`"",
    "set `"PROCESSOR_ARCHITECTURE=AMD64`"",
    "set `"NUMBER_OF_PROCESSORS=$([Environment]::ProcessorCount)`"",
    "set `"HTTP_PROXY=`"",
    "set `"HTTPS_PROXY=`"",
    "set `"http_proxy=`"",
    "set `"https_proxy=`"",
    $(if ($Proxy) { "set `"HTTP_PROXY=$Proxy`"" }),
    $(if ($Proxy) { "set `"HTTPS_PROXY=$Proxy`"" }),
    "cd /d `"$RepoRoot`""
  )
  if (-not $SkipVsEnvironment) {
    $content += @(
      "call `"$VsDevCmd`"",
      "if errorlevel 1 exit /b %errorlevel%"
    )
  }
  $content += $Lines + @(
    "exit /b %errorlevel%"
  )
  Set-Content -LiteralPath $cmdPath -Value $content -Encoding ASCII

  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = "C:\Windows\System32\cmd.exe"
  $psi.Arguments = "/d /c `"$cmdPath`""
  $psi.WorkingDirectory = $RepoRoot
  $psi.UseShellExecute = $true

  Write-Step $Name
  $process = [System.Diagnostics.Process]::Start($psi)
  $process.WaitForExit()
  if ($process.ExitCode -ne 0) {
    throw "$Name failed with exit code $($process.ExitCode). Command file: $cmdPath"
  }
}

function Invoke-Normal {
  param(
    [string]$Name,
    [string]$Exe,
    [string[]]$ArgumentList
  )

  Write-Step $Name
  & $Exe @ArgumentList
  if ($LASTEXITCODE -ne 0) {
    throw "$Name failed with exit code $LASTEXITCODE"
  }
}

function Test-KatexDist {
  param([string]$Path)

  return (Test-Path -LiteralPath (Join-Path $Path "katex.min.js")) -and
         (Test-Path -LiteralPath (Join-Path $Path "katex.min.css")) -and
         (Test-Path -LiteralPath (Join-Path $Path "fonts"))
}

function Require-KatexSource {
  if (-not (Test-KatexDist $SourceKatexDist)) {
    throw "KaTeX assets not found: $SourceKatexDist. The package needs local KaTeX assets for offline Markdown preview."
  }
}

function Test-PackagedFile {
  param([string]$Path, [string]$Name)

  if (-not (Test-Path -LiteralPath $Path)) {
    throw "$Name was not packaged: $Path"
  }
  Write-Host "$Name`: found"
}

function Test-PackageContents {
  Write-Step "Verify package contents"

  Test-PackagedFile (Join-Path $PackageBin "Qt6WebEngineCore.dll") "Qt6WebEngineCore.dll"
  Test-PackagedFile (Join-Path $PackageBin "Qt6WebEngineWidgets.dll") "Qt6WebEngineWidgets.dll"
  Test-PackagedFile (Join-Path $PackageBin "Qt6WebChannel.dll") "Qt6WebChannel.dll"
  Test-PackagedFile (Join-Path $PackageBin "QtWebEngineProcess.exe") "QtWebEngineProcess.exe"
  Test-PackagedFile (Join-Path $PackageBin "resources\qtwebengine_resources.pak") "qtwebengine_resources.pak"

  if (-not (Test-KatexDist $PackageKatexDist)) {
    throw "Packaged KaTeX assets are incomplete: $PackageKatexDist"
  }
  $fontCount = (Get-ChildItem -LiteralPath (Join-Path $PackageKatexDist "fonts") -File).Count
  if ($fontCount -lt 1) {
    throw "Packaged KaTeX fonts are missing: $PackageKatexDist\fonts"
  }
  Write-Host "KaTeX assets:              found ($fontCount font files)"

  $zip = Join-Path $RepoRoot "build\Package\flameshot-14.0.0-win64.zip"
  $installer = Join-Path $RepoRoot "build\Package\installer\Flameshot-14.0.0-win64-setup.exe"
  Test-PackagedFile $zip "Portable ZIP"
  if (-not $NoInstaller) {
    Test-PackagedFile $installer "Inno installer"
  }
}

Set-Location -LiteralPath $RepoRoot
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Require-File $VsDevCmd "Visual Studio vcvars64.bat"
Require-File $VsCMake "Visual Studio CMake"
Require-File $VsCPack "Visual Studio CPack"
Require-KatexSource
if (-not $NoInstaller) {
  Require-File $Iscc "Inno Setup compiler"
  Require-File $InnoScript "Inno Setup script"
}

Write-Host "Repository: $RepoRoot"
if ($Proxy) {
  Write-Host "Proxy:      $Proxy"
} else {
  Write-Host "Proxy:      disabled"
}
Write-Host "Qt:         $QtRoot"
Write-Host "Config:     $Configuration"

if ($CheckOnly) {
  Write-Step "Check only"
  Write-Host "Visual Studio environment: $VsDevCmd"
  Write-Host "CMake:                     $VsCMake"
  Write-Host "CPack:                     $VsCPack"
  if (-not $NoInstaller) {
    Write-Host "Inno Setup:                $Iscc"
  }
  if (Test-Path -LiteralPath $QtCoreDll) {
    Write-Host "Qt runtime:                found"
  } else {
    Write-Host "Qt runtime:                missing; normal build will install it"
  }
  if (Test-Path -LiteralPath $QtWebEngineWidgetsConfig) {
    Write-Host "Qt WebEngineWidgets:       found"
  } else {
    Write-Host "Qt WebEngineWidgets:       missing; normal build will install it"
  }
  if (Test-Path -LiteralPath $QtWebChannelConfig) {
    Write-Host "Qt WebChannel:             found"
  } else {
    Write-Host "Qt WebChannel:             missing; normal build will install it"
  }
  if (Test-Path -LiteralPath $QtPositioningConfig) {
    Write-Host "Qt Positioning:            found"
  } else {
    Write-Host "Qt Positioning:            missing; normal build will install it"
  }
  if (Test-KatexDist $SourceKatexDist) {
    Write-Host "KaTeX source assets:       found"
  } else {
    Write-Host "KaTeX source assets:       missing"
  }
  Write-Host "Clean child environment:   enabled"
  return
}

if ($VerifyOnly) {
  Test-PackageContents
  return
}

if (-not $SkipQtInstall -and
    ((-not (Test-Path -LiteralPath $QtCoreDll)) -or
     (-not (Test-Path -LiteralPath $QtWebEngineWidgetsConfig)) -or
     (-not (Test-Path -LiteralPath $QtWebChannelConfig)) -or
     (-not (Test-Path -LiteralPath $QtPositioningConfig)))) {
  Write-Step "Install Qt $QtVersion"
  if (-not (Test-Path -LiteralPath $VenvPython)) {
    $hostPython = Get-HostPython
    Invoke-Normal "Create Python venv" $hostPython @("-m", "venv", ".venv")
  }
  Invoke-Normal "Install aqtinstall" $VenvPython @("-m", "pip", "install", "-U", "pip", "aqtinstall")
  if ($Proxy) {
    $env:HTTP_PROXY = $Proxy
    $env:HTTPS_PROXY = $Proxy
  } else {
    Remove-Item Env:HTTP_PROXY -ErrorAction SilentlyContinue
    Remove-Item Env:HTTPS_PROXY -ErrorAction SilentlyContinue
  }
  Invoke-Normal "Download Qt" $VenvPython @(
    "-m", "aqt", "install-qt", "windows", "desktop", $QtVersion,
    "win64_msvc2022_64", "-m", "qtimageformats", "qtwebchannel", "qtpositioning", "qtwebengine", "-O", "build\Qt"
  )
}

$cmakeCache = Join-Path $BuildDir "CMakeCache.txt"
if ($Reconfigure -or -not (Test-Path -LiteralPath $cmakeCache) -or
    -not (Test-Path -LiteralPath (Join-Path $BuildDir "src\flameshot_autogen"))) {
  Invoke-CleanCmd "Configure CMake" @(
    "`"$VsCMake`" -S . -B build -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_PREFIX_PATH=`"%CD%\build\Qt\$QtVersion\msvc2022_64`" -DQt6_DIR=`"%CD%\build\Qt\$QtVersion\msvc2022_64\lib\cmake\Qt6`" -DCMAKE_BUILD_TYPE=$Configuration -DUSE_PORTABLE_CONFIG=ON -DFETCHCONTENT_UPDATES_DISCONNECTED=ON",
    "if errorlevel 1 exit /b %errorlevel%"
  )
}

Invoke-CleanCmd "Build Flameshot" @(
  "`"$VsCMake`" --build build --config $Configuration",
  "if errorlevel 1 exit /b %errorlevel%"
)

Invoke-CleanCmd "Create portable ZIP" @(
  "`"$VsCPack`" --config build\CPackConfig.cmake -G ZIP -B build\Package",
  "if errorlevel 1 exit /b %errorlevel%"
)

if (-not $NoInstaller) {
  Invoke-CleanCmd "Create Inno installer" @(
    "`"$Iscc`" `"$InnoScript`"",
    "if errorlevel 1 exit /b %errorlevel%"
  ) -SkipVsEnvironment
}

Write-Step "Artifacts"
$zip = Join-Path $RepoRoot "build\Package\flameshot-14.0.0-win64.zip"
$installer = Join-Path $RepoRoot "build\Package\installer\Flameshot-14.0.0-win64-setup.exe"
if (Test-Path -LiteralPath $zip) {
  Get-Item -LiteralPath $zip | Select-Object FullName, Length, LastWriteTime
}
if (-not $NoInstaller -and (Test-Path -LiteralPath $installer)) {
  Get-Item -LiteralPath $installer | Select-Object FullName, Length, LastWriteTime
}

Test-PackageContents
