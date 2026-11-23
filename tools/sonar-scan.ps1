<#
.SYNOPSIS
    Analiza RAAADIO con SonarQube: clang-tidy + sonar-scanner.

.DESCRIPTION
    1. Cobertura: compila RAAADIO_Tests en build/ con la configuración RelWithDebInfo (trae los
       .pdb) y lo corre con OpenCppCoverage (winget install OpenCppCoverage.OpenCppCoverage).
       Sin OpenCppCoverage el análisis sigue, sin cobertura.
    2. Prepara build-sonar/ (CMake + Ninja con MSVC) solo para obtener compile_commands.json y
       los encabezados que genera JUCE (JuceHeader.h, BinaryData.h). No compila la aplicación.
    3. Corre clang-tidy (el que trae Visual Studio) en paralelo sobre Source/ y Tests/ con las
       reglas de .clang-tidy y convierte el resultado al formato de issues externos de SonarQube.
    4. Ejecuta sonar-scanner (la primera vez lo descarga en %LOCALAPPDATA%\sonar-scanner y
       comprueba su SHA-256).

    El token se toma de $env:SONAR_TOKEN o del archivo .sonar-token en la raíz del repositorio
    (ignorado por git). Nunca se escribe en sonar-project.properties.

.PARAMETER SkipClangTidy
    No vuelve a correr clang-tidy: reutiliza el último informe, si existe.

.PARAMETER SkipCoverage
    No vuelve a medir la cobertura: reutiliza el último informe, si existe.

.PARAMETER ReportOnly
    Genera el informe de clang-tidy y no lo envía a SonarQube.

.PARAMETER Jobs
    Procesos de clang-tidy en paralelo. Por defecto: núcleos - 2, sin pasar de un proceso por
    cada 2,5 GB de RAM (cada uno carga los encabezados de JUCE).

.PARAMETER HostUrl
    Servidor de SonarQube. Por defecto $env:SONAR_HOST_URL o el de sonar-project.properties.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\sonar-scan.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\sonar-scan.ps1 -SkipClangTidy
#>
[CmdletBinding()]
param(
    [switch]$SkipClangTidy,
    [switch]$SkipCoverage,
    [switch]$ReportOnly,
    [int]$Jobs = 0,
    [string]$HostUrl = ''
)

Set-StrictMode -Version 3
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest es lentísimo con la barra

$ScannerVersion = '8.5.0.16571'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$RootPrefix = $Root.TrimEnd('\') + '\'
$BuildDir = Join-Path $Root 'build-sonar'
$TidyDir = Join-Path $BuildDir 'clang-tidy'
$IssuesFile = Join-Path $TidyDir 'sonar-issues.json'
$CoverageDir = Join-Path $BuildDir 'coverage'
$CoverageFile = Join-Path $CoverageDir 'cobertura.xml'
$Utf8 = New-Object System.Text.UTF8Encoding($false)

function Write-Step([string]$Text) { Write-Host "`n== $Text" -ForegroundColor Cyan }

function ConvertTo-ArgString([string[]]$Arguments) {
    # Comillas al estilo de CommandLineToArgvW (Start-Process une los argumentos sin citarlos).
    ($Arguments | ForEach-Object {
        if ($_ -notmatch '[\s"]') { $_ }
        else { '"' + (($_ -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"' }
    }) -join ' '
}

function Wait-ForProgram([System.Diagnostics.Process]$Process) {
    # Solo ese programa: Start-Process -Wait también espera a sus hijos, y las herramientas de
    # MSVC dejan algunos vivos un buen rato después de compilar (vctip.exe, la telemetría).
    $null = $Process.Handle   # sin esto PowerShell 5.1 pierde el código de salida
    $Process.WaitForExit()
}

function Invoke-Tool([string]$Exe, [string[]]$Arguments, [string]$LogFile, [string]$What) {
    # Programa externo con salida a un registro; si falla, muestra el final y se detiene.
    $p = Start-Process -FilePath $Exe -ArgumentList (ConvertTo-ArgString $Arguments) -NoNewWindow `
        -PassThru -RedirectStandardOutput $LogFile -RedirectStandardError "$LogFile.err"
    Wait-ForProgram $p
    if ($p.ExitCode -ne 0) {
        Get-Content $LogFile, "$LogFile.err" -Tail 30 | Write-Host
        throw "$What falló (código $($p.ExitCode)). Registro: $LogFile"
    }
}

function Get-RelativeSourcePath([string]$Path, [string]$BaseDir) {
    # Ruta relativa al repositorio con '/', o $null si no está en Source/ ni en Tests/.
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $BaseDir $Path }
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($RootPrefix, [StringComparison]::OrdinalIgnoreCase)) { return $null }
    $rel = $full.Substring($RootPrefix.Length) -replace '\\', '/'
    if ($rel -match '^(Source|Tests)/') { return $rel }
    return $null
}

function Enter-VisualStudioShell {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'No se encontró Visual Studio (vswhere.exe).' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No se encontró Visual Studio con las herramientas de C++ (MSVC x64).' }
    $env:PATH = (Split-Path $vswhere) + ';' + $env:PATH   # VsDevCmd también busca vswhere
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    return $vs
}

function Get-RuleMeta([string]$Rule) {
    # Atributo Clean Code e impacto de cada familia de reglas de clang-tidy.
    $attr = 'LOGICAL'; $quality = 'RELIABILITY'; $severity = 'MEDIUM'
    if ($Rule -eq 'clang-diagnostic-error') { $severity = 'HIGH' }
    elseif ($Rule -like 'clang-analyzer-security*' -or $Rule -like 'cert-*') { $attr = 'TRUSTWORTHY'; $quality = 'SECURITY' }
    elseif ($Rule -like 'clang-analyzer-optin*') { }
    elseif ($Rule -like 'clang-analyzer-deadcode*') { $attr = 'CLEAR'; $quality = 'MAINTAINABILITY'; $severity = 'LOW' }
    elseif ($Rule -like 'clang-analyzer-*') { $severity = 'HIGH' }
    elseif ($Rule -like 'performance-*') { $attr = 'EFFICIENT'; $quality = 'MAINTAINABILITY' }
    elseif ($Rule -like 'modernize-*' -or $Rule -like 'misc-*') { $attr = 'CONVENTIONAL'; $quality = 'MAINTAINABILITY'; $severity = 'LOW' }
    elseif ($Rule -like 'readability-*') { $attr = 'CLEAR'; $quality = 'MAINTAINABILITY'; $severity = 'LOW' }

    $dash = $Rule.IndexOf('-')
    if ($Rule -eq 'clang-diagnostic-error') {
        $description = 'clang-tidy no pudo compilar esta parte del archivo; los hallazgos de ese archivo pueden estar incompletos.'
    } elseif ($Rule -like 'clang-diagnostic-*') {
        $description = 'Advertencia del compilador Clang. Documentación: https://clang.llvm.org/docs/DiagnosticsReference.html#w' + $Rule.Substring(18).ToLowerInvariant()
    } elseif ($dash -gt 0) {
        $description = 'Regla de clang-tidy. Documentación: https://clang.llvm.org/extra/clang-tidy/checks/' + $Rule.Substring(0, $dash) + '/' + $Rule.Substring($dash + 1) + '.html'
    } else {
        $description = 'Regla de clang-tidy. Documentación: https://clang.llvm.org/extra/clang-tidy/checks/list.html'
    }
    [ordered]@{
        id                 = $Rule
        name               = $Rule
        description        = $description
        engineId           = 'clang-tidy'
        cleanCodeAttribute = $attr
        impacts            = @([ordered]@{ softwareQuality = $quality; severity = $severity })
    }
}

function Invoke-ClangTidy([string]$VsPath) {
    Write-Step 'Preparando build-sonar (CMake + Ninja, solo encabezados generados)'
    foreach ($tool in 'cmake', 'ninja', 'cl') {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "No se encontró $tool en el entorno de Visual Studio." }
    }
    New-Item -ItemType Directory -Force $TidyDir | Out-Null

    # Reutiliza las dependencias que build/ ya descargó (JUCE, SQLite...) en vez de bajarlas otra vez.
    $configure = @('-S', $Root, '-B', $BuildDir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl', '-Wno-dev')
    $deps = [ordered]@{ JUCE = 'juce'; SQLITE3 = 'sqlite3'; EBUR128 = 'ebur128'; LAME = 'lame'; CHROMAPRINT = 'chromaprint' }
    foreach ($name in $deps.Keys) {
        $src = Join-Path $Root "build\_deps\$($deps[$name])-src"
        if (Test-Path $src) { $configure += "-DFETCHCONTENT_SOURCE_DIR_$name=$($src -replace '\\', '/')" }
    }
    Invoke-Tool 'cmake' $configure (Join-Path $BuildDir 'configure.log') 'La configuración de CMake'

    $generated = @(& ninja -C $BuildDir -t targets all | ForEach-Object {
        if ($_ -match '^(?<t>[^:]+/JuceLibraryCode/[^:]+\.h): CUSTOM_COMMAND$') { $Matches['t'] }
    })
    if ($generated.Count -gt 0) {
        Invoke-Tool 'ninja' (@('-C', $BuildDir) + $generated) (Join-Path $BuildDir 'generate.log') 'La generación de encabezados de JUCE'
    }

    # Base de compilación solo con Source/ y Tests/, una entrada por archivo: los .cpp que
    # también compilan las pruebas aparecen dos veces y clang-tidy los analizaría dos veces.
    $db = Get-Content (Join-Path $BuildDir 'compile_commands.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $seen = @{}
    $entries = New-Object System.Collections.ArrayList
    foreach ($e in $db) {
        $rel = Get-RelativeSourcePath $e.file $e.directory
        if ($rel -and -not $seen.ContainsKey($rel)) {
            $seen[$rel] = $true
            [void]$entries.Add([ordered]@{ directory = $e.directory; command = $e.command; file = $e.file })
        }
    }
    if ($entries.Count -eq 0) { throw 'compile_commands.json no tiene archivos de Source/ ni Tests/.' }
    [IO.File]::WriteAllText((Join-Path $TidyDir 'compile_commands.json'),
        (ConvertTo-Json -InputObject ([object[]]$entries) -Depth 3), $Utf8)

    $tidy = Join-Path $VsPath 'VC\Tools\Llvm\x64\bin\clang-tidy.exe'
    if (-not (Test-Path $tidy)) {
        $cmd = Get-Command clang-tidy -ErrorAction SilentlyContinue
        if (-not $cmd) { throw 'No se encontró clang-tidy. Instala el componente "C++ Clang tools for Windows" de Visual Studio.' }
        $tidy = $cmd.Source
    }
    $version = (& $tidy --version | Select-String 'version' | Select-Object -First 1).Line.Trim()

    if ($Jobs -le 0) {
        $ramGB = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB
        $Jobs = [Math]::Max(1, [Math]::Min([Environment]::ProcessorCount - 2, [Math]::Floor($ramGB / 2.5)))
    }
    # Los archivos grandes primero, para que el último en terminar no sea uno de 3000 líneas.
    $files = @($entries | ForEach-Object { Get-Item $_.file } | Sort-Object Length -Descending | ForEach-Object { $_.FullName })
    $total = $files.Count
    Write-Step "clang-tidy ($version): $total archivos, $Jobs en paralelo"

    $outDir = Join-Path $TidyDir 'out'
    if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
    New-Item -ItemType Directory $outDir | Out-Null

    $queue = New-Object System.Collections.Queue
    $files | ForEach-Object { $queue.Enqueue($_) }
    $running = New-Object System.Collections.ArrayList
    $failed = New-Object System.Collections.ArrayList
    $n = 0; $finished = 0
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($queue.Count -gt 0 -or $running.Count -gt 0) {
        while ($running.Count -lt $Jobs -and $queue.Count -gt 0) {
            $file = $queue.Dequeue(); $n++
            $out = Join-Path $outDir ('{0:D3}.txt' -f $n)
            $p = Start-Process -FilePath $tidy -NoNewWindow -PassThru `
                -ArgumentList (ConvertTo-ArgString @('-p', $TidyDir, '--quiet', $file)) `
                -RedirectStandardOutput $out -RedirectStandardError "$out.err"
            $null = $p.Handle   # sin esto PowerShell 5.1 pierde el código de salida
            [void]$running.Add([pscustomobject]@{ Process = $p; File = $file })
        }
        Start-Sleep -Milliseconds 250
        foreach ($r in @($running)) {
            if (-not $r.Process.HasExited) { continue }
            $r.Process.WaitForExit()
            $running.Remove($r)
            $finished++
            $rel = $r.File.Substring($RootPrefix.Length)
            if ($r.Process.ExitCode -ne 0) { [void]$failed.Add($rel) }
            Write-Host ('  [{0,3}/{1}] {2:mm\:ss}  {3}' -f $finished, $total, $clock.Elapsed, $rel)
        }
    }
    if ($failed.Count -gt 0) {
        Write-Warning "clang-tidy terminó con errores en $($failed.Count) archivo(s) (se informan como clang-diagnostic-error): $($failed -join ', ')"
    }

    Write-Step 'Convirtiendo a issues externos de SonarQube'
    $pattern = '^(?<file>(?:[A-Za-z]:)?[^:]+):(?<line>\d+):(?<col>\d+): (?<sev>warning|error): (?<msg>.*) \[(?<rule>[^\]]+)\]\s*$'
    $issues = New-Object System.Collections.ArrayList
    $rules = [ordered]@{}
    $keys = @{}
    foreach ($report in Get-ChildItem $outDir -Filter '*.txt') {
        foreach ($line in [IO.File]::ReadAllLines($report.FullName, $Utf8)) {
            if ($line -notmatch $pattern) { continue }
            $rel = Get-RelativeSourcePath $Matches['file'] $Root
            if (-not $rel) { continue }
            $rule = ($Matches['rule'] -split ',')[0]
            $lineNo = [int]$Matches['line']
            $msg = $Matches['msg']
            $key = "$rel|$lineNo|$rule|$msg"   # un encabezado se analiza una vez por cada .cpp
            if ($keys.ContainsKey($key)) { continue }
            $keys[$key] = $true
            if (-not $rules.Contains($rule)) { $rules[$rule] = Get-RuleMeta $rule }
            [void]$issues.Add([ordered]@{
                ruleId          = $rule
                effortMinutes   = 5
                primaryLocation = [ordered]@{
                    message   = $msg
                    filePath  = $rel
                    textRange = [ordered]@{ startLine = $lineNo }
                }
            })
        }
    }
    $json = ConvertTo-Json -InputObject ([ordered]@{ rules = [object[]]@($rules.Values); issues = [object[]]$issues }) -Depth 8 -Compress
    [IO.File]::WriteAllText($IssuesFile, $json, $Utf8)

    Write-Host "  $($issues.Count) hallazgos en $($rules.Count) reglas ($([int]$clock.Elapsed.TotalMinutes) min) -> $($IssuesFile.Substring($RootPrefix.Length))"
    $issues | Group-Object { $_.ruleId } | Sort-Object Count -Descending | Select-Object -First 15 |
        ForEach-Object { Write-Host ('  {0,5}  {1}' -f $_.Count, $_.Name) }
}

function Invoke-Coverage {
    # Cobertura de las pruebas: RAAADIO_Tests compilado en RelWithDebInfo (OpenCppCoverage
    # necesita los .pdb, que Release no genera) y ejecutado como lo hace ctest, desde build/.
    $occ = 'C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe'
    if (-not (Test-Path $occ)) {
        $cmd = Get-Command OpenCppCoverage -ErrorAction SilentlyContinue
        if (-not $cmd) {
            Write-Warning 'No está OpenCppCoverage (winget install OpenCppCoverage.OpenCppCoverage): el análisis sale sin cobertura.'
            return
        }
        $occ = $cmd.Source
    }
    $build = Join-Path $Root 'build'
    if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
        Write-Step 'Configurando build/ (Visual Studio 2022, como la integración continua)'
        Invoke-Tool 'cmake' @('-S', $Root, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64') (Join-Path $BuildDir 'build-configure.log') 'La configuración de build/'
    }
    New-Item -ItemType Directory -Force $CoverageDir | Out-Null

    Write-Step 'Cobertura: compilando RAAADIO_Tests (RelWithDebInfo)'
    Invoke-Tool 'cmake' @('--build', $build, '--config', 'RelWithDebInfo', '--target', 'RAAADIO_Tests', '--parallel') `
        (Join-Path $CoverageDir 'build.log') 'La compilación de las pruebas'

    Write-Step 'Cobertura: corriendo las pruebas con OpenCppCoverage'
    $tests = Join-Path $build 'RAAADIO_Tests_artefacts\RelWithDebInfo\RAAADIO_Tests.exe'
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $log = Join-Path $CoverageDir 'tests.log'
    # OpenCppCoverage corre las pruebas como un depurador, y Windows les da entonces el heap de
    # depuración (rellena la memoria liberada, revisa los bordes): se apaga para que corran
    # igual que con ctest. Para buscar errores de memoria, mejor AddressSanitizer.
    $env:_NO_DEBUG_HEAP = '1'
    # Desde build-sonar/coverage, donde OpenCppCoverage deja su LastCoverageResults.log.
    $p = Start-Process -FilePath $occ -NoNewWindow -PassThru -WorkingDirectory $CoverageDir `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err" `
        -ArgumentList (ConvertTo-ArgString @('--quiet', '--optimized_build',   # sin esto falla con código optimizado
            '--sources', (Join-Path $Root 'Source'), '--working_dir', $build,
            '--export_type', "cobertura:$CoverageFile", '--', $tests))
    Wait-ForProgram $p
    Remove-Item Env:\_NO_DEBUG_HEAP
    $summary = Get-Content $log | Where-Object { $_ -match 'comprobaciones correctas' } | Select-Object -Last 1
    Write-Host "  $summary ($([int]$clock.Elapsed.TotalMinutes) min)"
    if ($p.ExitCode -ne 0) { Write-Warning "Las pruebas fallaron (código $($p.ExitCode)); la cobertura se envía igual. Registro: $log" }
    if (-not (Test-Path $CoverageFile)) { throw "OpenCppCoverage no generó el informe. Registro: $log" }

    [xml]$xml = Get-Content $CoverageFile -Raw
    $rate = [double]::Parse($xml.coverage.'line-rate', [Globalization.CultureInfo]::InvariantCulture)
    Write-Host ('  Líneas cubiertas en Source/: {0:P1} -> {1}' -f $rate, $CoverageFile.Substring($RootPrefix.Length))
}

function Get-SonarScanner {
    $cmd = Get-Command sonar-scanner -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $dir = Join-Path $env:LOCALAPPDATA 'sonar-scanner'
    $bin = Join-Path $dir "sonar-scanner-$ScannerVersion-windows-x64\bin\sonar-scanner.bat"
    if (Test-Path $bin) { return $bin }

    Write-Step "Descargando sonar-scanner $ScannerVersion (una sola vez)"
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $url = "https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-$ScannerVersion-windows-x64.zip"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $zip = Join-Path $dir "sonar-scanner-$ScannerVersion.zip"
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $zip
    $content = (Invoke-WebRequest -UseBasicParsing -Uri "$url.sha256").Content
    if ($content -is [byte[]]) { $content = [Text.Encoding]::ASCII.GetString($content) }   # llega como binario
    $expected = ($content.Trim() -split '\s+')[0]
    $actual = (Get-FileHash $zip -Algorithm SHA256).Hash
    if ($actual -ne $expected) {
        Remove-Item $zip
        throw "La descarga de sonar-scanner no coincide con su SHA-256 ($actual en vez de $expected)."
    }
    Expand-Archive $zip -DestinationPath $dir -Force
    Remove-Item $zip
    return $bin
}

function Get-ProjectVersion {
    $cmake = Get-Content (Join-Path $Root 'CMakeLists.txt') -Raw -Encoding UTF8
    if ($cmake -match 'project\s*\(\s*RAAADIO\s+VERSION\s+([0-9.]+)') { return $Matches[1] }
    return 'sin-versión'
}

# ── Principal ────────────────────────────────────────────────────────────────────────────
Set-Location $Root

if (-not $SkipCoverage -or -not $SkipClangTidy) { $vs = Enter-VisualStudioShell }

if ($SkipCoverage) {
    if (Test-Path $CoverageFile) { Write-Host "Se reutiliza la cobertura anterior ($((Get-Item $CoverageFile).LastWriteTime))." }
    else { Write-Warning 'No hay informe de cobertura: SonarQube la verá en 0 %.' }
} else {
    Invoke-Coverage
}

if ($SkipClangTidy) {
    if (Test-Path $IssuesFile) { Write-Host "Se reutiliza el informe anterior de clang-tidy ($((Get-Item $IssuesFile).LastWriteTime))." }
    else { Write-Warning 'No hay informe de clang-tidy: SonarQube solo recibirá métricas y duplicados.' }
} else {
    Invoke-ClangTidy $vs
}
if ($ReportOnly) { return }

$token = $env:SONAR_TOKEN
$tokenFile = Join-Path $Root '.sonar-token'
if (-not $token -and (Test-Path $tokenFile)) {
    $token = (Get-Content $tokenFile -Raw -Encoding UTF8)
    if ($token) { $token = $token.Trim() }
}
if (-not $token) {
    throw 'Falta el token de SonarQube: define $env:SONAR_TOKEN o guárdalo en .sonar-token (ignorado por git).'
}
$env:SONAR_TOKEN = $token   # sonar-scanner lo lee de aquí; así no aparece en la línea de comandos

$scanner = Get-SonarScanner
$scanArgs = @("-Dsonar.projectBaseDir=$Root", "-Dsonar.projectVersion=$(Get-ProjectVersion)")
if (-not $HostUrl -and $env:SONAR_HOST_URL) { $HostUrl = $env:SONAR_HOST_URL }
if ($HostUrl) { $scanArgs += "-Dsonar.host.url=$HostUrl" }
if (Test-Path $IssuesFile) { $scanArgs += "-Dsonar.externalIssuesReportPaths=$($IssuesFile.Substring($RootPrefix.Length) -replace '\\', '/')" }
if (Test-Path $CoverageFile) { $scanArgs += "-Dsonar.cxx.cobertura.reportPaths=$($CoverageFile.Substring($RootPrefix.Length) -replace '\\', '/')" }

Write-Step 'sonar-scanner'
$ErrorActionPreference = 'Continue'   # lo que el escáner escribe en stderr no es un fallo
& $scanner @scanArgs
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($code -ne 0) { throw "sonar-scanner falló (código $code)." }

$task = Join-Path $BuildDir 'scannerwork\report-task.txt'
if (Test-Path $task) {
    $dashboard = (Get-Content $task | Where-Object { $_ -like 'dashboardUrl=*' }) -replace '^dashboardUrl=', ''
    if ($dashboard) { Write-Host "`nPanel del proyecto: $dashboard" -ForegroundColor Green }
}
