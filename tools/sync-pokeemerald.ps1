# Copies the platform-independent protocol code into a pokeemerald-expansion
# checkout (include/gc_ext, src/gc_ext). Use -Check to verify the copy is current.
#
# The default set is the generic connector (frames, messages, party records),
# meant for the pokeemerald-expansion master line. -Battle also copies the
# battle application protocol, which belongs only in game-content branches
# (e.g. frontier-hack) that start GC_MODE_BATTLE.
param([Parameter(Mandatory)][string]$Target, [switch]$Check, [switch]$Battle)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$commit = (git -C $root rev-parse --short=12 HEAD).Trim()
$stale = @()
$names = @('wire', 'message', 'pkmn_protocol', 'round_trip')
if ($Battle) { $names += 'battle_protocol' }
foreach ($name in $names) {
    foreach ($ext in 'h', 'c') {
        if ($name -eq 'round_trip' -and $ext -eq 'c') { continue }
        $source = Join-Path $root "protocol/$name.$ext"
        $dir = if ($ext -eq 'h') { 'include/gc_ext' } else { 'src/gc_ext' }
        $dest = Join-Path $Target "$dir/$name.$ext"
        $text = (Get-Content -Raw $source) -replace '#include "(wire|message|pkmn_protocol|round_trip|battle_protocol)\.h"', '#include "gc_ext/$1.h"'
        $text = "// Vendored from gba-gc-extension protocol/$name.$ext. Do not edit here;`n" +
                "// change the extension repository and run tools/sync-pokeemerald.ps1.`n" + $text
        $text = $text -replace "`r`n", "`n"
        if ($Check) {
            if (-not (Test-Path $dest) -or ((Get-Content -Raw $dest) -replace "`r`n", "`n") -ne $text) { $stale += $dest }
        } else {
            New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
            [IO.File]::WriteAllText($dest, $text)
        }
    }
}
if ($Check) {
    if ($stale) { $stale | ForEach-Object { Write-Error "Stale vendored protocol file: $_" -ErrorAction Continue }; exit 1 }
    "Vendored protocol matches gba-gc-extension $commit"
} else { "Synced protocol from gba-gc-extension $commit" }
