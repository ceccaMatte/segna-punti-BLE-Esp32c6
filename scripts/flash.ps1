<#
.SYNOPSIS
    Build, flash and monitor the ESP32-C5-LCD-1.47 BOOT button demo.

.DESCRIPTION
    Activates the requested ESP-IDF installation - discovered from the Espressif
    Installation Manager config at C:\Espressif\tools\eim_idf.json - and then
    runs idf.py from the project root.

.PARAMETER Action
    all         Build, flash and open the serial monitor (default)
    build       Compile only
    flash       Write the binary to the board
    monitor     Open the serial monitor only
    clean       Remove the build directory
    size        Report the firmware footprint
    setTarget   Force the target to esp32c5
    menuconfig  Open the interactive configuration editor

.PARAMETER Port
    Serial port to use, for example COM5. When omitted, the port of the
    ESP32-C5 USB Serial/JTAG controller is detected automatically. Pass an
    explicit value when several Espressif boards are connected.

.PARAMETER IdfVersion
    ESP-IDF installation to activate. Defaults to v6.0.2, the version the
    vendor CI tested this board against.

.EXAMPLE
    .\scripts\flash.ps1
    Build, flash and monitor in one go.

.EXAMPLE
    .\scripts\flash.ps1 -Action build
    Compile only.

.EXAMPLE
    .\scripts\flash.ps1 -Port COM7
    Build, flash and monitor on an explicitly chosen port.

.NOTES
    Leave the serial monitor with Ctrl+].
    SPDX-License-Identifier: MIT
#>
[CmdletBinding()]
param(
    [ValidateSet('all', 'build', 'flash', 'monitor', 'clean', 'size', 'setTarget', 'menuconfig')]
    [string] $Action = 'all',

    [string] $Port,

    [string] $IdfVersion = 'v6.0.2'
)

$ErrorActionPreference = 'Stop'

$script:EimConfigPath = 'C:\Espressif\tools\eim_idf.json'
$script:ProjectRoot = Split-Path -Parent $PSScriptRoot

function Get-EspIdfInstall {
    param([Parameter(Mandatory)][string] $Name)

    if (-not (Test-Path -LiteralPath $script:EimConfigPath)) {
        throw "Espressif Installation Manager config not found at '$script:EimConfigPath'."
    }

    $config = Get-Content -LiteralPath $script:EimConfigPath -Raw | ConvertFrom-Json
    $install = $config.idfInstalled |
        Where-Object { $_.name -eq $Name } |
        Select-Object -First 1

    if (-not $install) {
        $available = ($config.idfInstalled | ForEach-Object { $_.name }) -join ', '
        throw "ESP-IDF '$Name' is not installed. Available installations: $available"
    }

    if (-not (Test-Path -LiteralPath $install.activationScript)) {
        throw "Activation script missing: $($install.activationScript)"
    }

    return $install
}

function Get-EspPort {
    try {
        $device = Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
            Where-Object { $_.DeviceID -like 'USB\VID_303A*' -and $_.Name -match '\(COM\d+\)' } |
            Select-Object -First 1
    } catch {
        return $null
    }

    if ($device -and $device.Name -match '\((COM\d+)\)') {
        return $Matches[1]
    }

    return $null
}

$idf = Get-EspIdfInstall -Name $IdfVersion
Write-Host "Activating ESP-IDF $($idf.name)  ($($idf.path))" -ForegroundColor Cyan
. $idf.activationScript

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw 'idf.py is not on PATH after activating ESP-IDF.'
}

$idfArgs = @()
if ($Action -in @('all', 'flash', 'monitor')) {
    if (-not $Port) {
        $Port = Get-EspPort
    }

    if ($Port) {
        Write-Host "Using serial port $Port" -ForegroundColor Cyan
        $idfArgs += @('-p', $Port)
    } else {
        Write-Warning 'Could not detect the Espressif USB Serial/JTAG port; letting idf.py pick one.'
    }
}

Write-Host "Running '$Action' in $script:ProjectRoot" -ForegroundColor Cyan
Push-Location -LiteralPath $script:ProjectRoot
try {
    switch ($Action) {
        'setTarget' { idf.py set-target esp32c5 }
        'clean'     { idf.py fullclean }
        'size'      { idf.py size }
        'build'     { idf.py build }
        'flash'     { idf.py @idfArgs flash }
        'monitor'   { idf.py @idfArgs monitor }
        'all'       { idf.py @idfArgs flash monitor }
    }
} finally {
    Pop-Location
}
