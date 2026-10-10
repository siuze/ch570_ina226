param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [ValidateSet('Dongle', 'Probe')]
    [string]$Target = 'Dongle',
    [switch]$Diagnostic,
    [switch]$WithoutUartForTest
)

$ErrorActionPreference = 'Stop'
$projectName = if ($Target -eq 'Probe') { 'RF_Uart' } else { 'RF_UartDongle' }
if ($Diagnostic -and $Target -ne 'Dongle') { throw 'Diagnostic build is only available for Dongle' }
if ($WithoutUartForTest -and $Target -ne 'Probe') { throw 'WithoutUartForTest is only available for Probe' }
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$toolBin = Join-Path $ProjectRoot 'tools/riscv-gcc/riscv-none-elf-gcc-12-win-1.92/bin'
$gcc = Join-Path $toolBin 'riscv-wch-elf-gcc.exe'
$objcopy = Join-Path $toolBin 'riscv-wch-elf-objcopy.exe'
$size = Join-Path $toolBin 'riscv-wch-elf-size.exe'
if (-not (Test-Path -LiteralPath $gcc)) {
    # The redistributable toolchain in this repository uses the upstream
    # riscv-none-elf prefix but supports the CH57x ISA extensions used here.
    $gcc = Join-Path $toolBin 'riscv-none-elf-gcc.exe'
    $objcopy = Join-Path $toolBin 'riscv-none-elf-objcopy.exe'
    $size = Join-Path $toolBin 'riscv-none-elf-size.exe'
}
$project = Join-Path $ProjectRoot ('RF/' + $projectName)
$output = Join-Path $project 'obj'

foreach ($exe in @($gcc, $objcopy, $size)) {
    if (-not (Test-Path -LiteralPath $exe)) { throw "Missing tool: $exe" }
}

$common = @(
    '-march=rv32imc_zba_zbb_zbc_zbs_xw', '-mabi=ilp32', '-mcmodel=medany',
    '-msmall-data-limit=8', '-mno-save-restore', '-fmax-errors=20', '-Os',
    '-fmessage-length=0', '-fsigned-char', '-ffunction-sections',
    '-fdata-sections', '-fno-common', '-fstack-usage',
    '--param=highcode-gen-section-name=1', '-g'
)
$defines = @()
if ($Target -eq 'Probe') {
    $defines += ('-DPROBE_WITHOUT_UART=' + ($(if ($WithoutUartForTest) { '1' } else { '0' })))
}
$includes = @(
    'SRC/Startup', ('RF/' + $projectName + '/APP/include'),
    ('RF/' + $projectName + '/Profile/include'), 'SRC/StdPeriphDriver/inc',
    'SRC/Ld', 'RF/LIB', 'SRC/RVMSIS'
) | ForEach-Object { '-I' + (Join-Path $ProjectRoot $_) }

$objects = [System.Collections.Generic.List[string]]::new()
$sourceGroups = @(
    @{ Source = (Join-Path $project 'APP'); Destination = (Join-Path $output 'APP') },
    @{ Source = (Join-Path $ProjectRoot 'SRC/StdPeriphDriver'); Destination = (Join-Path $output 'StdPeriphDriver') }
)
foreach ($group in $sourceGroups) {
    New-Item -ItemType Directory -Path $group.Destination -Force | Out-Null
    foreach ($source in (Get-ChildItem -LiteralPath $group.Source -Filter '*.c' -File | Sort-Object Name)) {
        if ($Diagnostic -and $source.FullName -eq (Join-Path $project 'APP/main.c')) { continue }
        $object = Join-Path $group.Destination ($source.BaseName + '.o')
        Write-Host "CC $($source.Name)"
        & $gcc @common @defines @includes '-std=gnu99' '-c' '-o' $object $source.FullName
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $($source.FullName)" }
        $objects.Add($object)
    }
}

if ($Diagnostic) {
    $source = Join-Path $PSScriptRoot 'diagnostic_main.c'
    $object = Join-Path $output 'APP/diagnostic_main.o'
    Write-Host 'CC diagnostic_main.c'
    & $gcc @common @defines @includes '-std=gnu99' '-c' '-o' $object $source
    if ($LASTEXITCODE -ne 0) { throw 'Diagnostic main compile failed' }
    $objects.Add($object)
}

$startup = Join-Path $ProjectRoot 'SRC/Startup/startup_CH572.S'
$startupObject = Join-Path $output 'Startup/startup_CH572.o'
New-Item -ItemType Directory -Path (Split-Path -Parent $startupObject) -Force | Out-Null
Write-Host 'AS startup_CH572.S'
& $gcc @common '-x' 'assembler-with-cpp' '-c' '-o' $startupObject $startup
if ($LASTEXITCODE -ne 0) { throw 'Startup assembly failed' }
$objects.Add($startupObject)

$name = if ($Diagnostic) { 'RF_UartDongle_diagnostic' } elseif ($WithoutUartForTest) { 'RF_Uart_without-UART-for-test' } else { $projectName }
$elf = Join-Path $output ($name + '.elf')
$hex = Join-Path $output ($name + '.hex')
$map = Join-Path $output ($name + '.map')
Write-Host "LD $name.elf"
$linkArgs = @($common) + @(
    '-T', (Join-Path $ProjectRoot 'SRC/Ld/Link.ld'), '-nostartfiles',
    '-Wl,--gc-sections', ('-L' + (Join-Path $ProjectRoot 'RF/LIB')),
    ('-L' + (Join-Path $ProjectRoot 'SRC/StdPeriphDriver')),
    '-Wl,--print-memory-usage', ('-Wl,-Map,' + $map),
    '--specs=nano.specs', '--specs=nosys.specs', '-o', $elf
) + @($objects) + @('-lISP572', '-lm', '-lCH57xRF')
& $gcc @defines @linkArgs
if ($LASTEXITCODE -ne 0) { throw 'Link failed' }

& $objcopy '-O' 'ihex' $elf $hex
if ($LASTEXITCODE -ne 0) { throw 'HEX generation failed' }
& $size '--format=berkeley' $elf
if ($LASTEXITCODE -ne 0) { throw 'Size check failed' }
Write-Host "HEX: $hex"
