<#
.SYNOPSIS
Drive the FLEX transmitter over its serial control link and load it with pages.

.DESCRIPTION
Open "Serial control" on the transmitter first; it takes USB CDC interface 1,
so the Flipper appears as a second COM port while that screen is up.

Three ways to address a burst:

  (default)   a fresh random capcode on every phase the mode carries, all in
              the one burst, each carrying the same message
  -Single     one capcode on one phase, as an operator assigns it
  -PerPhase   a capcode per phase, each carrying its own message (80 chars,
              one frame); needs the build with AT+PAGE

Each capcode is listed on the phase its own bits 2-3 name, so a receiver
deriving phase from the address finds the page where the number says it is.

Every send waits for the +TXDONE event rather than guessing a duration, which
is what a program driving this ought to do.

.EXAMPLE
.\flex_tx_saturate.ps1 -Port COM20 -Freq 431012500

.EXAMPLE
.\flex_tx_saturate.ps1 -Mode 3 -Chars 240 -Seconds 90

.EXAMPLE
.\flex_tx_saturate.ps1 -PerPhase -Mode 3 -Seconds 60
#>
[CmdletBinding()]
param(
    [string]$Port    = 'COM20',
    [uint32]$Freq    = 431012500,
    # 0 = 1600/2FSK (phase A), 1 = 3200/2 (A C), 2 = 3200/4 (A B), 3 = 6400/4 (A B C D)
    [ValidateRange(0, 3)][int]$Mode = 3,
    [ValidateRange(1, 240)][int]$Chars = 240,
    [int]$Seconds    = 90,
    # One capcode on one phase instead of the whole set.
    [switch]$Single,
    # A capcode per phase, each with its own message. Caps the length at 80.
    [switch]$PerPhase,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

if ($Single -and $PerPhase) { throw 'Pick one of -Single or -PerPhase.' }

# The phase slots this mode actually puts on the air. 3200/2FSK carries A and
# C, not A and B, so it is not simply the first N: flex_tx_mode() in flex_tx.c
# holds the orders, and ric[] is indexed by slot, so a capcode on a slot the
# mode skips never leaves the Flipper.
$slots = @(@(0), @(0, 2), @(0, 1), @(0, 1, 2, 3))[$Mode]
if (-not $Single -and $slots.Count -lt 4) {
    $letters = ($slots | ForEach-Object { [char](65 + $_) }) -join '/'
    Write-Warning "Mode $Mode carries $($slots.Count) phase(s), $letters. Use -Mode 3 for all four."
}
$maxChars = if ($PerPhase) { 80 } else { 240 }
if ($Chars -gt $maxChars) {
    Write-Warning "Trimming to $maxChars chars: a staged per-phase page is one frame."
    $Chars = $maxChars
}

# ------------------------------------------------------------------ the link

# Not $port: the parameter above is declared [string], and PowerShell matches
# variable names without case, so assigning the object to it would store the
# object's string form instead.
$link = New-Object System.IO.Ports.SerialPort $Port,115200,'None',8,'One'
$link.ReadTimeout  = 50
$link.WriteTimeout = 1000
$link.DtrEnable    = $true
$link.RtsEnable    = $true
$link.Open()
Start-Sleep -Milliseconds 300

$script:pending = ''

function Pump {
    while ($true) { try { $script:pending += [char]$link.ReadChar() } catch { break } }
}

# Read until something matches, instead of sleeping a guess.
function Await([string]$pattern, [int]$timeoutMs) {
    $limit = (Get-Date).AddMilliseconds($timeoutMs)
    while ((Get-Date) -lt $limit) {
        Pump
        if ($script:pending -match $pattern) {
            $out = $script:pending; $script:pending = ''; return $out
        }
        Start-Sleep -Milliseconds 15
    }
    $out = $script:pending; $script:pending = ''; return $out
}

# Every command is answered by exactly one OK or ERR, data arriving on +NAME:
# lines before it. Unsolicited + lines are events.
function Cmd([string]$text, [int]$timeoutMs = 5000) {
    $link.Write("$text`r`n")
    return Await '(^|\n)(OK|ERR )' $timeoutMs
}

function Require([string]$text) {
    $reply = Cmd $text
    if ($reply -notmatch 'OK') {
        $line = ($reply -split "`r?`n" | Where-Object { $_ -match 'ERR' } | Select-Object -First 1)
        throw "$text -> $($line.Trim())"
    }
}

# A literal backslash has to reach the Flipper doubled; the quote is the only
# other character the argument parser treats specially.
function Escape([string]$s) { return ($s -replace '\\', '\\') -replace '"', '\"' }

# Put the capcode's own embedded phase on the slot it rides, so a receiver
# deriving phase from the address finds it where it was sent. AT+CAP fills
# ric[] by position and nothing re-files it there, so the number has to be
# listed on that slot as well as carry it.
function CapForSlot([int]$slot) {
    $c = Get-Random -Minimum 100000 -Maximum 1900000
    return ($c -band (-bnot 12)) -bor ($slot -shl 2)
}

$lead = @(
 'DISPATCH ALL UNITS PROCEED TO STAGING AREA SEVEN AND AWAIT INSTRUCTION',
 'MAINTENANCE WINDOW OPENS AT 0200 LOCAL ALL NON ESSENTIAL SYSTEMS CYCLE',
 'ALARM RAISED ON BUILDING FOUR ZONE TWO SENSOR OUT OF TOLERANCE',
 'SHIFT HANDOVER COMPLETE OUTSTANDING TICKETS LISTED FOR INCOMING CREW',
 'NETWORK CAPACITY TEST IN PROGRESS EXPECT ELEVATED PAGE VOLUME',
 'UNIT 12 RESPOND TO MAIN ST', 'RTN TO BASE WHEN CLEAR', 'SYS CHECK OK'
)
$filler = 'THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789 '

function Body([int]$seq, [int]$len) {
    $s = '[{0:0000}] {1} ' -f $seq, ($lead | Get-Random)
    while ($s.Length -lt $len) { $s += $filler }
    return $s.Substring(0, $len)
}

# ----------------------------------------------------------------- the run

try {
    Cmd 'AT+ABORT'   | Out-Null   # clear a radio a previous run left keyed
    Cmd 'AT+PAGECLR' | Out-Null   # harmless on builds without it
    Require "AT+FREQ=$Freq"
    Require "AT+MODE=$Mode"
    Require ('AT+PHASE=' + $(if ($Single) { 'CAP' } else { 'GROUP' }))
    Require 'AT+CLOCK=3'

    $modeName = @('1600/2', '3200/2', '3200/4', '6400/4')[$Mode]
    $per = if ($PerPhase) { 'per-phase pages' } elseif ($Single) { 'single cap' } else { 'all phases, one message' }
    "port $Port  freq $Freq  mode $modeName  $per  $Chars chars  ${Seconds}s"
    ''

    $rounds = 0; $pages = 0; $failed = 0; $spans = @()
    $start = Get-Date; $deadline = $start.AddSeconds($Seconds)

    while ((Get-Date) -lt $deadline) {
        $seq = $rounds + 1
        $t0 = Get-Date

        # ric[] as the Flipper will hold it: a capcode on each slot this round
        # addresses, 0 on the rest to clear whatever the last round left.
        $used = if ($Single) { ,($slots | Get-Random) } else { $slots }
        $burst = @(0, 0, 0, 0)
        foreach ($slot in $used) { $burst[$slot] = CapForSlot $slot }

        if ($PerPhase) {
            $ok = $true
            foreach ($slot in $used) {
                if (-not $ok) { break }
                $text = Escape (Body ($seq * 10 + $slot) $Chars)
                $r = Cmd ('AT+PAGE={0},{1},"{2}"' -f $slot, $burst[$slot], $text)
                if ($r -notmatch 'OK') { $ok = $false }
            }
            if (-not $ok) { $failed++; "  round $seq staging refused"; continue }
            $r = Cmd 'AT+SEND'
        } else {
            if ((Cmd ('AT+CAP=' + ($burst -join ','))) -notmatch 'OK') { $failed++; continue }
            $r = Cmd ('AT+MSG="' + (Escape (Body $seq $Chars)) + '"')
        }

        if ($r -match 'ERR') {
            $failed++
            "  round $seq refused: $(($r -split "`r?`n" | Where-Object { $_ -match 'ERR' })[0].Trim())"
            continue
        }

        $evt = Await '\+TXDONE' 40000
        $span = ((Get-Date) - $t0).TotalSeconds
        if ($evt -notmatch '\+TXDONE') { $failed++; "  round $seq no TXDONE after $([math]::Round($span,1))s"; continue }

        $rounds++; $pages += $used.Count; $spans += $span
        if (-not $Quiet) {
            # Labelled by the slot it was listed on, which is the phase it rode.
            $shown = ($used | ForEach-Object { '{0}:{1}' -f [char](65 + $_), $burst[$_] }) -join ' '
            "  round {0,3}  {1,5:0.00}s  {2}" -f $seq, $span, $shown
        }
    }

    $elapsed = ((Get-Date) - $start).TotalSeconds
    ''
    "rounds     : $rounds"
    "pages      : $pages"
    "failed     : $failed"
    "elapsed    : $([math]::Round($elapsed,1))s"
    if ($spans.Count) {
        $avg = ($spans | Measure-Object -Average).Average
        $min = ($spans | Measure-Object -Minimum).Minimum
        $max = ($spans | Measure-Object -Maximum).Maximum
        "per round  : avg $([math]::Round($avg,2))s  min $([math]::Round($min,2))s  max $([math]::Round($max,2))s"
        "throughput : $([math]::Round($pages/$elapsed*60,1)) pages/min, $([math]::Round($pages*$Chars/$elapsed,0)) chars/s"
    }
    Cmd 'AT+STATUS?' | ForEach-Object { $_ -split "`r?`n" } |
        Where-Object { $_.Trim() } | ForEach-Object { "  $($_.Trim())" }
} finally {
    if ($link.IsOpen) { $link.Close() }
    $link.Dispose()
}
