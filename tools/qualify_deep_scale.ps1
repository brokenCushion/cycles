# SPDX-License-Identifier: Apache-2.0
# Sequential production qualification: paired beauty, two retained deep renders,
# resource targets, repeat identity and independent Gaffer reader checks.
param(
    [Parameter(Mandatory=$true)][string]$Scene,
    [string]$Output = 'builds/validation/m8-release-scale',
    [string]$Blender = 'builds/blender/install/blender.exe',
    [string]$Gaffer = 'builds/build-gaffer/gaffer-1.7.2.0-windows/bin/gaffer.cmd'
)
$ErrorActionPreference = 'Stop'
$blenderPath = (Resolve-Path $Blender).Path
$gafferPath = (Resolve-Path $Gaffer).Path
$pythonPath = Join-Path (Split-Path $gafferPath) 'python.exe'
$scenePath = (Resolve-Path $Scene).Path
$env:BLENDER_USER_RESOURCES = (Resolve-Path builds/blender/user-resources).Path
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$root = (Resolve-Path $Output).Path
$report = [ordered]@{ scene = $scenePath; runs = @(); repeats = @{}; passed = $false
    renderer_colour_configuration = 'Blender bundled config; child OCIO unset' }
foreach ($device in @('CPU', 'CUDA')) {
    $prefix = $device.ToLowerInvariant()
    $runs = if ($device -eq 'CUDA') { @('beauty', 'beauty-repeat', '1', '2') } else { @('beauty', '1', '2') }
    foreach ($run in $runs) {
        $name = "$prefix-$run"
        $directory = Join-Path $root $name
        $commandArgs = @('tools/measure_deep_render.py', $directory, '--', $blenderPath,
            '--factory-startup', '--background', '--disable-autoexec',
            '--log', 'cycles', '--log-level', 'info', $scenePath,
            '--python-exit-code', '1', '--python', 'tools/render_blender_deep_scene.py',
            '--', '--output', $directory, '--samples', '4', '--percentage', '100', '--device', $device)
        if (!$run.StartsWith('beauty')) {
            $commandArgs += @('--deep', '--deep-volume', '--deep-memory-mb', '1024')
        }
        Write-Output "Production render $name"
        $previousOCIO = $env:OCIO
        try {
            $env:OCIO = $null
            & builds/build-m6/run-cuda.cmd $pythonPath @commandArgs *> "$root/$name-measurement.log"
            $renderExitCode = $LASTEXITCODE
        }
        finally { $env:OCIO = $previousOCIO }
        if ($renderExitCode -ne 0) { throw "Production render failed: $directory" }
    }
    foreach ($run in @('1', '2')) {
        $directory = Join-Path $root "$prefix-$run"
        $validationArgs = @('env', 'python', 'src/deep/validate_native_vdb_gaffer.py',
            $directory, (Join-Path $root "$prefix-beauty"))
        if ($device -eq 'CUDA') {
            $validationArgs += @('--beauty-repeat', (Join-Path $root "$prefix-beauty-repeat"))
        }
        & $gafferPath @validationArgs *> "$directory/gaffer.log"
        if ($LASTEXITCODE -ne 0) { throw "Production Gaffer check failed: $directory" }
        $measure = Get-Content -Raw -LiteralPath "$directory/measurement.json" | ConvertFrom-Json
        $render = Get-Content -Raw -LiteralPath "$directory/render.json" | ConvertFrom-Json
        $validation = Get-Content -Raw -LiteralPath "$directory/gaffer_validation.json" | ConvertFrom-Json
        $gates = [ordered]@{
            process = $measure.passed_process
            monitor = ($measure.monitor_errors.Count -eq 0)
            resolution = ($render.resolution[0] -eq 1024 -and $render.resolution[1] -eq 768)
            samples = ($render.samples -eq 4)
            reader = $validation.passed
            curve = ($validation.max_accepted_camera_error -le 1e-6 -and $validation.accepted_camera_probes -gt 0)
            time = ($measure.wall_seconds -le 2400)
            file_size = ($render.deep_bytes -le 4000000000)
            host_memory = ($measure.peak_working_set -le 4GB)
        }
        if ($device -eq 'CUDA') {
            $gates.device_memory = ($null -ne $measure.max_device_memory_mib -and $measure.max_device_memory_mib -le 8192)
        }
        $report.runs += [ordered]@{ device = $device; run = $run; gates = $gates
            seconds = $measure.wall_seconds; bytes = $render.deep_bytes
            host_peak = $measure.peak_working_set; device_peak_mib = $measure.max_device_memory_mib
            max_curve_error = $validation.max_accepted_camera_error
            max_slice_error = $validation.max_slice_error }
        $report | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath "$root/report.json"
        if ($gates.Values -contains $false) { throw "Production gate failed: $directory" }
    }
    $hashes = @(1,2 | ForEach-Object { (Get-FileHash -Algorithm SHA256 -LiteralPath "$root/$prefix-$_/scene.deep.exr").Hash })
    $report.repeats[$device] = @{ identical = ($hashes[0] -eq $hashes[1]); sha256 = $hashes }
    if ($hashes[0] -ne $hashes[1]) { throw "$device deep repeats differ" }
    $report | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath "$root/report.json"
}
& $gafferPath env python src/deep/validate_vdb_backend_gaffer.py "$root/cpu-1/scene.deep.exr" "$root/cuda-1/scene.deep.exr" "$root/backend.json" *> "$root/backend.log"
if ($LASTEXITCODE -ne 0) { throw 'Production CPU/CUDA curve comparison failed' }
$report.backend = Get-Content -Raw -LiteralPath "$root/backend.json" | ConvertFrom-Json
$report.passed = $true
$report | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath "$root/report.json"
Write-Output "Production qualification passed: $root/report.json"
