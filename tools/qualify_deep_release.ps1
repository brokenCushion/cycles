# SPDX-License-Identifier: Apache-2.0
# Run existing numerical/Gaffer suites sequentially against one installed build.
param(
    [string]$Cycles = 'builds/install-m6/cycles.exe',
    [string]$Gaffer = 'builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd',
    [string]$Output = 'builds/validation/m8-release',
    [ValidateSet('Surface', 'Volume', 'Storage', 'All')][string]$Group = 'All'
)
$ErrorActionPreference = 'Stop'
$cyclesPath = (Resolve-Path $Cycles).Path
$gafferPath = (Resolve-Path $Gaffer).Path
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$outputPath = (Resolve-Path $Output).Path
$results = [ordered]@{
    cycles = $cyclesPath
    cycles_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $cyclesPath).Hash
    started_utc = [DateTime]::UtcNow.ToString('o')
    group = $Group
    suites = @()
    passed = $false
}
function Run-Suite([string]$Name, [string]$Script, [string[]]$Arguments) {
    $directory = Join-Path $outputPath $Name
    if (Test-Path -LiteralPath $directory) { throw "Use a fresh suite directory: $directory" }
    New-Item -ItemType Directory -Path $directory | Out-Null
    $commandArgs = @('env', 'python', "src/deep/$Script", $cyclesPath, $directory) + $Arguments
    $start = Get-Date
    Write-Output "Running $Name"
    # The wrapper supplies the CUDA toolchain used by runtime kernel loading.
    & builds/build-m6/run-cuda.cmd $gafferPath @commandArgs *> "$directory/suite.log"
    $code = $LASTEXITCODE
    $results.suites += [ordered]@{
        name = $Name; script = $Script; arguments = $Arguments
        seconds = ((Get-Date) - $start).TotalSeconds; exit_code = $code
    }
    $results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$outputPath/$Group.json"
    if ($code -ne 0) {
        Get-Content -LiteralPath "$directory/suite.log" -Tail 25
        throw "Qualification failed: $Name"
    }
    Write-Output "Passed $Name"
}
if ($Group -in @('Surface', 'All')) {
    Run-Suite 'transparency' 'validate_transparency_gaffer.py' @()
    Run-Suite 'cuda-surface' 'validate_cuda_gaffer.py' @()
    Run-Suite 'adaptive-CPU' 'validate_adaptive_gaffer.py' @('CPU')
    Run-Suite 'adaptive-CUDA' 'validate_adaptive_gaffer.py' @('CUDA', "$outputPath/adaptive-CPU")
    Run-Suite 'dof' 'validate_dof_gaffer.py' @()
    Run-Suite 'motion-CPU' 'validate_motion_gaffer.py' @('CPU')
    Run-Suite 'motion-CUDA' 'validate_motion_gaffer.py' @('CUDA', "$outputPath/motion-CPU")
}
if ($Group -in @('Volume', 'All')) {
    Run-Suite 'volume-CPU' 'validate_volume_capture_gaffer.py' @('CPU')
    Run-Suite 'volume-CUDA' 'validate_volume_capture_gaffer.py' @('CUDA', "$outputPath/volume-CPU")
}
if ($Group -in @('Storage', 'All')) {
    Run-Suite 'storage' 'validate_production_gaffer.py' @()
    $lifecycleStart = Get-Date
    & $gafferPath env python src/deep/check_production_lifecycle.py $cyclesPath "$outputPath/storage" *> "$outputPath/storage/lifecycle.log"
    if ($LASTEXITCODE -ne 0) { throw 'Publication/lifecycle qualification failed' }
    $results.lifecycle = @{ passed = $true; seconds = ((Get-Date) - $lifecycleStart).TotalSeconds }
    $driverTest = Join-Path (Split-Path $cyclesPath) 'cycles_deep_output_driver_test.exe'
    $driverArgs = @((Split-Path $cyclesPath),
        (Resolve-Path src/app/deep_output_driver_test.xml).Path,
        (Resolve-Path src/app/deep_output_driver_transparent_test.xml).Path,
        (Resolve-Path src/app/deep_output_driver_volume_test.xml).Path, 'CUDA')
    $driverStart = Get-Date
    & builds/build-m6/run-cuda.cmd $driverTest @driverArgs *> "$outputPath/storage/cuda-lifecycle.log"
    if ($LASTEXITCODE -ne 0) { throw 'CUDA session/lifecycle qualification failed' }
    $results.cuda_lifecycle = @{ passed = $true; seconds = ((Get-Date) - $driverStart).TotalSeconds
        executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $driverTest).Hash }
}
$results.finished_utc = [DateTime]::UtcNow.ToString('o')
$results.passed = $true
$results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$outputPath/$Group.json"
