#requires -Version 7
param([int]$Auto = 0, [ValidateSet(50,100,160,250)][int]$Interval = 160)
$ErrorActionPreference = 'Stop'
$kit = if ($env:DEVKITPRO) { $env:DEVKITPRO } elseif ($IsWindows) { 'C:/devkitPro' } else { '/opt/devkitpro' }
if ($IsWindows -and $kit -match '^/([a-zA-Z])/(.*)$') { $kit = $Matches[1] + ':/' + $Matches[2] }
$suffix = if ($IsWindows) { '.exe' } else { '' }
function Invoke-Tool([string]$Tool, [string[]]$Arguments) {
    & "$kit/$Tool$suffix" @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Tool exited with code $LASTEXITCODE" }
}
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null
    $arm = @('-mcpu=arm7tdmi','-mthumb','-mthumb-interwork','-O2','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',"-I$kit/libgba/include",'-Iprotocol')
    $ppc = @('-DGEKKO','-mogc','-mcpu=750','-meabi','-mhard-float','-O2','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',"-I$kit/libogc/include",'-Iprotocol')
    Invoke-Tool devkitARM/bin/arm-none-eabi-g++ ($arm + @('-std=gnu++17','-fno-exceptions','-fno-rtti','-c','gba/main.cpp','-o','build/client.o'))
    Invoke-Tool devkitARM/bin/arm-none-eabi-gcc ($arm + @('-specs=gba.specs','-Wl,--gc-sections,-Map,build/client.map','build/client.o',"-L$kit/libgba/lib",'-lgba','-o','build/client.elf'))
    Invoke-Tool devkitARM/bin/arm-none-eabi-objcopy @('-O','binary','build/client.elf','build/client.gba')
    Invoke-Tool tools/bin/gbafix @('build/client.gba','-tJOYBUS POC','-cJGCE','-m00')
    Invoke-Tool devkitPPC/bin/powerpc-eabi-gcc ($ppc + @("-DPOC_AUTO=$Auto","-DPOC_INTERVAL=$Interval",'-std=c11','-Wl,--gc-sections,-Map,build/host.map','gamecube/main.c',"-L$kit/libogc/lib/cube",'-logc','-lm','-o','build/host.elf'))
    Invoke-Tool tools/bin/elf2dol @('build/host.elf','build/host.dol')
} finally { Pop-Location }
