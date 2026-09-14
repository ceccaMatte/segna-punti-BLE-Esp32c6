<#
.SYNOPSIS
    Compila ed esegue i test host della logica pura del segnapunti padel.

.DESCRIPTION
    I moduli match, history, button e controller non hanno dipendenze da
    ESP-IDF, quindi si compilano ed eseguono sul PC. Cosi' i bug del regolamento
    non si mescolano a quelli del display o del cablaggio.

    Serve un compilatore C nativo (gcc o clang). Il toolchain RISC-V di ESP-IDF
    non va bene: genera eseguibili per ESP32, non per Windows.

.PARAMETER CC
    Compilatore da usare. Se omesso viene cercato gcc, poi clang.

.EXAMPLE
    .\test\run_tests.ps1
    Compila ed esegue tutti i test.

.EXAMPLE
    .\test\run_tests.ps1 -CC C:\msys64\mingw64\bin\gcc.exe
    Usa un compilatore specifico.

.NOTES
    Per installare un compilatore se non ne hai nessuno:
        winget install BrechtSanders.WinLibs.POSIX.UCRT
    SPDX-License-Identifier: MIT
#>
[CmdletBinding()]
param(
    [string] $CC
)

$ErrorActionPreference = 'Stop'

$TestDir    = $PSScriptRoot
$ProjectDir = Split-Path -Parent $TestDir
$SrcDir     = Join-Path $ProjectDir 'main'
$OutExe     = Join-Path $TestDir 'padel_tests.exe'

$sources = @(
    (Join-Path $SrcDir  'match.c'),
    (Join-Path $SrcDir  'history.c'),
    (Join-Path $SrcDir  'button.c'),
    (Join-Path $SrcDir  'controller.c'),
    (Join-Path $SrcDir  'font.c'),
    (Join-Path $SrcDir  'gfx.c'),
    (Join-Path $SrcDir  'dirty.c'),
    (Join-Path $SrcDir  'ui_view.c'),
    (Join-Path $SrcDir  'led_anim.c'),
    (Join-Path $TestDir 'test_util.c'),
    (Join-Path $TestDir 'test_main.c'),
    (Join-Path $TestDir 'test_match.c'),
    (Join-Path $TestDir 'test_button.c'),
    (Join-Path $TestDir 'test_controller.c'),
    (Join-Path $TestDir 'test_font.c'),
    (Join-Path $TestDir 'test_gfx.c'),
    (Join-Path $TestDir 'test_dirty.c'),
    (Join-Path $TestDir 'test_ui_view.c'),
    (Join-Path $TestDir 'test_layout.c'),
    (Join-Path $TestDir 'test_led_anim.c')
)

foreach ($source in $sources) {
    if (-not (Test-Path -LiteralPath $source)) {
        Write-Host "Sorgente mancante: $source" -ForegroundColor Red
        exit 1
    }
}

function Resolve-Compiler {
    param([string] $Requested)

    if ($Requested) {
        $cmd = Get-Command $Requested -ErrorAction SilentlyContinue
        if (-not $cmd) {
            Write-Host "Compilatore '$Requested' non trovato." -ForegroundColor Red
            exit 1
        }
        return $cmd.Source
    }

    foreach ($name in @('gcc', 'clang')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) {
            return $cmd.Source
        }
    }

    return $null
}

$compiler = Resolve-Compiler -Requested $CC

if (-not $compiler) {
    Write-Host ''
    Write-Host 'Nessun compilatore C nativo trovato (gcc o clang).' -ForegroundColor Yellow
    Write-Host ''
    Write-Host 'I test girano sul PC: il toolchain RISC-V di ESP-IDF non va bene,'
    Write-Host 'produce eseguibili per ESP32 e non per Windows.'
    Write-Host ''
    Write-Host 'Installa un compilatore con uno di questi comandi:' -ForegroundColor Yellow
    Write-Host '    winget install BrechtSanders.WinLibs.POSIX.UCRT' -ForegroundColor Cyan
    Write-Host '    winget install LLVM.LLVM' -ForegroundColor Cyan
    Write-Host ''
    Write-Host 'Poi riapri il terminale e rilancia lo script.'
    exit 2
}

Write-Host "Compilatore : $compiler" -ForegroundColor Cyan
Write-Host "Sorgenti    : $SrcDir" -ForegroundColor Cyan
Write-Host ''

$compileArgs = @(
    '-std=c11',
    '-Wall',
    '-Wextra',
    '-O1',
    '-I', $SrcDir,
    '-I', $TestDir,
    '-o', $OutExe
) + $sources

& $compiler @compileArgs

if ($LASTEXITCODE -ne 0) {
    Write-Host ''
    Write-Host "Compilazione fallita (exit $LASTEXITCODE)." -ForegroundColor Red
    exit 1
}

Write-Host ''
& $OutExe
$exitCode = $LASTEXITCODE

Write-Host "Eseguibile: $OutExe" -ForegroundColor DarkGray

exit $exitCode
