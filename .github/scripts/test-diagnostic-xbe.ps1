$ErrorActionPreference = "Stop"
$guest = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\build-guest32\out\xbe_guest32.exe"))
if (!(Test-Path $guest)) { throw "Guest helper missing: $guest" }

# Build a real 32-bit XBE image. The entry point calls only kernel ordinals
# implemented by XboxRuntime and writes a machine-readable result block.
$base = [uint32]0x10000000
$entryVa = [uint32]0x10001000
$reportVa = [uint32]0x10002200
$passedVa = [uint32]($reportVa + 4)
$failedVa = [uint32]($reportVa + 8)
$skippedVa = [uint32]($reportVa + 12)
$poolVa = [uint32]($reportVa + 0x20)
$vmBaseVa = [uint32]($reportVa + 0x24)
$vmSizeVa = [uint32]($reportVa + 0x28)
$frequencyVa = [uint32]($reportVa + 0x2C)
$systemTimeVa = [uint32]($reportVa + 0x34)
$unicodeVa = [uint32]($reportVa + 0x80)
$objectAttributesVa = [uint32]($reportVa + 0x90)
$ioStatusVa = [uint32]($reportVa + 0xB0)
$fileHandleVa = [uint32]($reportVa + 0xB8)
$readBufferVa = [uint32]($reportVa + 0xC0)
$fileOffsetVa = [uint32]($reportVa + 0xD0)
$testPathVa = [uint32]($reportVa + 0x100)
$numberTextVa = [uint32]($reportVa + 0x140)
$numberValueVa = [uint32]($reportVa + 0x148)
$smbusValueVa = [uint32]($reportVa + 0x3C)
$interlockedVa = [uint32]($reportVa + 0x40)
$contiguousVa = [uint32]($reportVa + 0x44)
$thunkVa = [uint32]0x10002A00
$ordinals = @(8,14,17,23,45,50,53,125,126,127,128,151,165,171,174,179,180,184,187,199,202,219,238,267,284,292,293,296,299,307,309,310,320)
$ordIndex = @{}
for ($i=0; $i -lt $ordinals.Count; $i++) { $ordIndex[[int]$ordinals[$i]] = $i }

$script:code = [System.Collections.Generic.List[byte]]::new()
$script:passedAddress = $passedVa
$script:failedAddress = $failedVa
$script:thunkAddress = $thunkVa
$script:ordinalSlots = $ordIndex
function EmitByte([int]$v) { [void]$script:code.Add([byte]($v -band 255)) }
function Emit32([uint32]$v) { foreach ($x in [BitConverter]::GetBytes($v)) { [void]$script:code.Add($x) } }
function EmitPush([uint32]$v) { EmitByte 0x68; Emit32 $v }
function EmitPushMemory([uint32]$address) { EmitByte 0xFF; EmitByte 0x35; Emit32 $address }
function EmitLoadEax([uint32]$address) { EmitByte 0xA1; Emit32 $address }
function EmitStoreEax([uint32]$address) { EmitByte 0xA3; Emit32 $address }
function EmitMovMemoryImmediate([uint32]$address,[uint32]$value) { EmitByte 0xC7; EmitByte 0x05; Emit32 $address; Emit32 $value }
function EmitIncrement([uint32]$address) { EmitByte 0xFF; EmitByte 0x05; Emit32 $address }
function EmitCallImport([int]$ordinal) {
    $slot = [uint32]($script:thunkAddress + 4 * [int]$script:ordinalSlots[$ordinal])
    EmitLoadEax $slot
    EmitByte 0xFF; EmitByte 0xD0
}
function EmitCheckEqual([uint32]$expected) {
    EmitByte 0x3D; Emit32 $expected
    EmitByte 0x75; EmitByte 8
    EmitIncrement $script:passedAddress
    EmitByte 0xEB; EmitByte 6
    EmitIncrement $script:failedAddress
}
function EmitCheckNonZero {
    EmitByte 0x85; EmitByte 0xC0
    EmitByte 0x74; EmitByte 8
    EmitIncrement $script:passedAddress
    EmitByte 0xEB; EmitByte 6
    EmitIncrement $script:failedAddress
}
function EmitCheckAtLeast([uint32]$minimum) {
    EmitByte 0x3D; Emit32 $minimum
    EmitByte 0x72; EmitByte 8
    EmitIncrement $script:passedAddress
    EmitByte 0xEB; EmitByte 6
    EmitIncrement $script:failedAddress
}
function EmitCheckPoolSize {
    EmitByte 0x3D; Emit32 ([uint32]4294967295)
    EmitByte 0x74; EmitByte 15
    EmitByte 0x3D; Emit32 ([uint32]64)
    EmitByte 0x72; EmitByte 8
    EmitIncrement $script:passedAddress
    EmitByte 0xEB; EmitByte 6
    EmitIncrement $script:failedAddress
}

# Pool allocator, size query, memory fill/zero, and free.
EmitPush 64
EmitCallImport 14
EmitStoreEax $poolVa
EmitCheckNonZero
EmitPushMemory $poolVa
EmitCallImport 23
EmitCheckPoolSize
EmitPush ([uint32]0x5A)
EmitPush 64
EmitPushMemory $poolVa
EmitCallImport 284
EmitIncrement $passedVa
EmitLoadEax $poolVa
EmitByte 0x0F; EmitByte 0xB6; EmitByte 0x00
EmitCheckEqual 0x5A
EmitPush 64
EmitPushMemory $poolVa
EmitCallImport 320
EmitIncrement $passedVa
EmitLoadEax $poolVa
EmitByte 0x0F; EmitByte 0xB6; EmitByte 0x00
EmitCheckEqual 0
EmitPushMemory $poolVa
EmitCallImport 17
EmitIncrement $passedVa

# Performance counter/frequency, interrupt time, system time, stall, and yield.
EmitCallImport 127
EmitCheckNonZero
EmitPush $frequencyVa
EmitCallImport 126
EmitCheckNonZero
EmitCallImport 125
EmitCheckNonZero
EmitPush $systemTimeVa
EmitCallImport 128
EmitLoadEax $systemTimeVa
EmitCheckNonZero
EmitPush 100
EmitCallImport 151
EmitIncrement $passedVa
EmitCallImport 238
EmitCheckEqual 0

# EEPROM SMBus read followed by a write-back of the exact byte just read.
EmitPush $smbusValueVa
EmitPush 0
EmitPush 0
EmitPush 0xA8
EmitCallImport 45
EmitCheckEqual 0
EmitPushMemory $smbusValueVa
EmitPush 0
EmitPush 0
EmitPush 0xA8
EmitCallImport 50
EmitCheckEqual 0

# Virtual memory allocate/query/free.
EmitMovMemoryImmediate $vmBaseVa 0
EmitMovMemoryImmediate $vmSizeVa 4096
EmitPush 4
EmitPush 0x3000
EmitPush $vmSizeVa
EmitPush 0
EmitPush $vmBaseVa
EmitCallImport 184
EmitCheckEqual 0
EmitLoadEax $vmBaseVa
EmitCheckNonZero
EmitPushMemory $vmBaseVa
EmitCallImport 174
EmitCheckNonZero
EmitPushMemory $vmBaseVa
EmitCallImport 179
EmitCheckEqual 4
EmitPushMemory $vmBaseVa
EmitCallImport 180
EmitCheckAtLeast 4096
EmitMovMemoryImmediate $vmSizeVa 0
EmitPush 0x8000
EmitPush $vmSizeVa
EmitPush $vmBaseVa
EmitCallImport 199
EmitCheckEqual 0

# Host-file mapping, NT open/read/close, using the XBE title directory as CD-ROM.
EmitPush 0
EmitPush 1
EmitPush $ioStatusVa
EmitPush $objectAttributesVa
EmitPush ([uint32]2147483648)
EmitPush $fileHandleVa
EmitCallImport 202
EmitCheckEqual 0
EmitPush 0
EmitPush $fileOffsetVa
EmitPush 8
EmitPush $readBufferVa
EmitPush $ioStatusVa
EmitPush 0
EmitPush 0
EmitPush 0
EmitPushMemory $fileHandleVa
EmitCallImport 219
EmitCheckEqual 0
EmitLoadEax $readBufferVa
EmitCheckEqual 0x46545258
EmitLoadEax ([uint32]($readBufferVa + 4))
EmitCheckEqual 0x21454C49
EmitPushMemory $fileHandleVa
EmitCallImport 187
EmitCheckEqual 0

# Contiguous memory allocation, address validation, and free.
EmitPush 4096
EmitCallImport 165
EmitStoreEax $contiguousVa
EmitCheckNonZero
EmitPushMemory $contiguousVa
EmitCallImport 174
EmitCheckNonZero
EmitPushMemory $contiguousVa
EmitCallImport 171
EmitIncrement $passedVa

# Interlocked primitive and byte/character conversion helpers.
EmitMovMemoryImmediate $interlockedVa 0
EmitByte 0xB9; Emit32 $interlockedVa
EmitCallImport 53
EmitCheckEqual 1
EmitByte 0xB9; Emit32 0x12345678
EmitCallImport 307
EmitCheckEqual 0x78563412
EmitPush 0x41
EmitCallImport 296
EmitCheckEqual 0x61

# Integer parsing/formatting and narrow/wide string conversion.
EmitMovMemoryImmediate $numberValueVa 0
EmitPush $numberValueVa
EmitPush 10
EmitPush $numberTextVa
EmitCallImport 267
EmitCheckEqual 0
EmitLoadEax $numberValueVa
EmitCheckEqual 1234

# UnicodeString descriptor at report+0x150; its buffer is report+0x180.
$unicodeDescriptorVa = [uint32]($reportVa + 0x150)
$unicodeBufferVa = [uint32]($reportVa + 0x1A0)
$letterVa = [uint32]($reportVa + 0x180)
$formattedVa = [uint32]($reportVa + 0x190)
EmitMovMemoryImmediate $unicodeDescriptorVa 0
EmitMovMemoryImmediate ([uint32]($unicodeDescriptorVa + 4)) 16
EmitMovMemoryImmediate ([uint32]($unicodeDescriptorVa + 8)) $unicodeBufferVa
EmitPush $unicodeDescriptorVa
EmitPush 10
EmitPush 42
EmitCallImport 293
EmitCheckEqual 0
EmitPush $formattedVa
EmitPush 8
EmitPush 16
EmitPush 255
EmitCallImport 292
EmitCheckEqual 0
EmitLoadEax $formattedVa
EmitCheckEqual 0x66
EmitPush $numberValueVa
EmitPush 10
EmitPush $unicodeDescriptorVa
EmitCallImport 309
EmitCheckEqual 0
EmitLoadEax $numberValueVa
EmitCheckEqual 42

# Convert one ASCII byte to UTF-16 and back.
EmitPush 1
EmitPush $letterVa
EmitPush $numberValueVa
EmitPush 2
EmitPush $unicodeBufferVa
EmitCallImport 299
EmitCheckEqual 0
EmitPush 2
EmitPush $unicodeBufferVa
EmitPush $numberValueVa
EmitPush 4
EmitPush $formattedVa
EmitCallImport 310
EmitCheckEqual 0

# These are host-module facilities, not kernel imports callable by this guest yet.
# Mark FATX API, NV2A/D3D rendering, and Xbox networking as NOT SUPPORTED.
EmitIncrement $skippedVa
EmitIncrement $skippedVa
EmitIncrement $skippedVa
EmitByte 0x31; EmitByte 0xC0; EmitByte 0xC3

if ($script:code.Count -gt 0x1000) { throw "Diagnostic XBE code overlaps its data region." }
$b = [byte[]]::new(0x3000)
function Put32([int]$offset,[uint32]$value) { [BitConverter]::GetBytes($value).CopyTo($b,$offset) }
[Text.Encoding]::ASCII.GetBytes("XBEH").CopyTo($b,0)
Put32 0x104 $base
Put32 0x108 0x1000
Put32 0x10C 0x3000
Put32 0x110 0x178
Put32 0x11C 1
Put32 0x120 0x10000178
Put32 0x128 ($entryVa -bxor [Convert]::ToUInt32('A8FC57AB',16))
Put32 0x154 ([uint32]($thunkVa -bxor [uint32]0x5B6D40B6))
# Executable, writable section: code at VA 0x10001000, data at 0x10002200.
Put32 0x178 5
Put32 0x17C $entryVa
Put32 0x180 0x2000
Put32 0x184 0x1000
Put32 0x188 0x2000
$script:code.ToArray().CopyTo($b,0x1000)
# The guest import resolver replaces these encoded ordinal slots with thunks.
for ($i=0; $i -lt $ordinals.Count; $i++) {
    Put32 (0x2A00 + 4*$i) ([uint32](2147483648 + [int64]$ordinals[$i]))
}
Put32 (0x2A00 + 4*$ordinals.Count) 0
Put32 0x2200 0x54535258
Put32 0x2228 4096
# Guest-side OBJECT_ATTRIBUTES + UNICODE_STRING for \Device\Cdrom0\xbe-test.txt.
$guestPath = "\Device\Cdrom0\xbe-test.txt"
$guestPathBytes = [Text.Encoding]::Unicode.GetBytes($guestPath + [char]0)
$guestPathLength = [uint32]($guestPath.Length * 2)
Put32 0x2280 ([uint32]($guestPathLength -bor (($guestPathLength + 2) -shl 16)))
Put32 0x2284 $testPathVa
Put32 0x2290 24
Put32 0x2294 0
Put32 0x2298 $unicodeVa
Put32 0x229C 0
Put32 0x22A0 0
Put32 0x22A4 0
Put32 0x22B0 0
Put32 0x22B4 0
Put32 0x22B8 0
$guestPathBytes.CopyTo($b,0x2300)
[Text.Encoding]::ASCII.GetBytes("1234" + [char]0).CopyTo($b,0x2340)
[Text.Encoding]::ASCII.GetBytes("A").CopyTo($b,0x2380)
$testDir = Join-Path $env:RUNNER_TEMP "XboxRuntimeDiagnosticXbe"
New-Item -ItemType Directory -Force -Path $testDir | Out-Null
$xbe = Join-Path $testDir "xboxruntime-diagnostic.xbe"
[IO.File]::WriteAllBytes($xbe,$b)
[IO.File]::WriteAllBytes((Join-Path $testDir "xbe-test.txt"),[Text.Encoding]::ASCII.GetBytes("XRTFILE!"))
$artifactXbe = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\build\out\xboxruntime-diagnostic.xbe"))
Copy-Item $xbe $artifactXbe -Force
$eeprom = Join-Path $testDir "eeprom.bin"
$eepromBytes = [byte[]]::new(256)
for ($i=0; $i -lt $eepromBytes.Length; $i++) { $eepromBytes[$i] = 0xFF }
[IO.File]::WriteAllBytes($eeprom,$eepromBytes)

$name = "XboxRuntimeDiagnostic-$PID"
$pipe = [IO.Pipes.NamedPipeServerStream]::new(
    $name,[IO.Pipes.PipeDirection]::InOut,1,
    [IO.Pipes.PipeTransmissionMode]::Byte,[IO.Pipes.PipeOptions]::None
)
$pipePath = "\\.\pipe\$name"
$proc = Start-Process -FilePath $guest -ArgumentList @(
    "--pipe",$pipePath,"--xbe",$xbe,"--eeprom",$eeprom,
    "--report-va","0x10002200"
) -PassThru -NoNewWindow
try {
    $task = $pipe.WaitForConnectionAsync()
    if (!$task.Wait(15000)) { throw "x86 guest did not connect to the diagnostic pipe." }
    $reply = [byte[]]::new(28)
    $read = 0
    while ($read -lt $reply.Length) {
        $n = $pipe.Read($reply,$read,$reply.Length-$read)
        if ($n -le 0) { throw "Guest closed the pipe before its launch reply." }
        $read += $n
    }
    $magic = [BitConverter]::ToUInt32($reply,0)
    $status = [BitConverter]::ToUInt32($reply,8)
    if ($magic -ne 0x47584258 -or $status -ne 0) {
        throw "Diagnostic XBE launch failed: magic=$('{0:X8}' -f $magic), status=$status"
    }
    $pipe.WriteByte(0)
    $pipe.Flush()
    $report = [byte[]]::new(16)
    $read = 0
    while ($read -lt $report.Length) {
        $n = $pipe.Read($report,$read,$report.Length-$read)
        if ($n -le 0) { throw "Guest ended without returning diagnostic results." }
        $read += $n
    }
    $reportMagic = [BitConverter]::ToUInt32($report,0)
    $passed = [BitConverter]::ToUInt32($report,4)
    $failed = [BitConverter]::ToUInt32($report,8)
    $skipped = [BitConverter]::ToUInt32($report,12)
    if ($reportMagic -ne 0x54535258) { throw "Invalid diagnostic report marker." }
    Write-Host "XboxRuntime Diagnostic XBE: $passed PASS, $failed FAIL, $skipped NOT SUPPORTED"
    if ($failed -ne 0 -or $passed -lt 20 -or $skipped -ne 3) {
        throw "Diagnostic XBE reported an unexpected result."
    }
    if (!$proc.WaitForExit(10000)) { throw "x86 guest did not exit after diagnostic completion." }
    if ($proc.ExitCode -ne 0) { throw "x86 guest exited with code $($proc.ExitCode)." }
}
finally {
    $pipe.Dispose()
    if ($proc -and !$proc.HasExited) { Stop-Process -Id $proc.Id -Force }
}
