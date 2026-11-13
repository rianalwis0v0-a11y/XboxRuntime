$ErrorActionPreference = "Stop"
$guest = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\build-guest32\out\xbe_guest32.exe"))
if (!(Test-Path $guest)) { throw "Guest helper missing: $guest" }

# Build a real 32-bit XBE image. The entry point calls only kernel ordinals
# implemented by XboxRuntime and writes a machine-readable result block.
$base = [uint32]0x10000000
$entryVa = [uint32]0x10001000
$reportVa = [uint32]0x10006000
$passedVa = [uint32]($reportVa + 4)
$progressVa = [uint32]($reportVa + 0x14)
$failedVa = [uint32]($reportVa + 8)
$failedCheckVa = [uint32]($reportVa + 0x10)
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
$thunkVa = [uint32]0x10006800
$ordinals = @(8,14,17,19,20,21,23,32,33,34,45,50,53,218,281,282,283,125,126,127,128,151,165,171,174,179,180,184,187,189,192,193,194,199,202,209,213,214,216,219,238,12,13,18,28,56,57,58,97,101,108,110,112,113,122,123,129,130,131,132,138,145,149,150,158,159,160,161,167,172,267,268,274,276,277,278,284,287,292,293,291,294,295,296,299,300,270,271,279,280,289,290,301,307,309,310,311,314,315,316,317,318,320,210,229,265,74,207,111,116,117,136)
$ordIndex = @{}
for ($i=0; $i -lt $ordinals.Count; $i++) { $ordIndex[[int]$ordinals[$i]] = $i }

$script:code = [System.Collections.Generic.List[byte]]::new()
$script:passedAddress = $passedVa
$script:failedAddress = $failedVa
$script:failedCheckAddress = $failedCheckVa
$script:checkIndex = 0
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
    $script:checkIndex++
    EmitByte 0x3D; Emit32 $expected
    EmitByte 0x75; EmitByte 8
    EmitIncrement $script:passedAddress
    EmitByte 0xEB; EmitByte 16
    EmitMovMemoryImmediate $script:failedCheckAddress ([uint32]$script:checkIndex)
    EmitIncrement $script:failedAddress
}
function EmitCheckNonZero {
    $script:checkIndex++
    EmitByte 0x85; EmitByte 0xC0
    EmitByte 0x74; EmitByte 8
    EmitIncrement $script:passedAddress
    EmitByte 0xEB; EmitByte 16
    EmitMovMemoryImmediate $script:failedCheckAddress ([uint32]$script:checkIndex)
    EmitIncrement $script:failedAddress
}
function EmitCheckAtLeast([uint32]$minimum) {
    $script:checkIndex++
    EmitByte 0x3D; Emit32 $minimum
    EmitByte 0x72; EmitByte 8
    EmitIncrement $script:passedAddress
    EmitByte 0xEB; EmitByte 16
    EmitMovMemoryImmediate $script:failedCheckAddress ([uint32]$script:checkIndex)
    EmitIncrement $script:failedAddress
}
function EmitCheckPoolSize {
    $script:checkIndex++
    EmitByte 0x3D; Emit32 ([uint32]4294967295)
    EmitByte 0x74; EmitByte 15
    EmitByte 0x3D; Emit32 ([uint32]64)
    EmitByte 0x72; EmitByte 8
    EmitIncrement $script:passedAddress
    EmitByte 0xEB; EmitByte 16
    EmitMovMemoryImmediate $script:failedCheckAddress ([uint32]$script:checkIndex)
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

# Query FILE_STANDARD_INFORMATION and verify the eight-byte diagnostic file size.
$fileInfoVa = [uint32]($reportVa + 0x380)
EmitPush 5
EmitPush 24
EmitPush $fileInfoVa
EmitPush $ioStatusVa
EmitPushMemory $fileHandleVa
EmitCallImport 211
EmitCheckEqual 0
EmitLoadEax ([uint32]($fileInfoVa + 8))
EmitCheckEqual 8

# Set the file pointer to zero, then query FILE_POSITION_INFORMATION.
EmitMovMemoryImmediate $fileInfoVa 0
EmitMovMemoryImmediate ([uint32]($fileInfoVa + 4)) 0
EmitPush 14
EmitPush 8
EmitPush $fileInfoVa
EmitPush $ioStatusVa
EmitPushMemory $fileHandleVa
EmitCallImport 226
EmitCheckEqual 0
EmitPush 14
EmitPush 8
EmitPush $fileInfoVa
EmitPush $ioStatusVa
EmitPushMemory $fileHandleVa
EmitCallImport 211
EmitCheckEqual 0
EmitLoadEax $fileInfoVa
EmitCheckEqual 0

# Query the mounted volume's volume, size, device, and filesystem metadata.
$volumeInfoVa = [uint32]($reportVa + 0x400)
$volumeSizeVa = [uint32]($reportVa + 0x480)
$volumeDeviceVa = [uint32]($reportVa + 0x4A0)
$volumeAttributesVa = [uint32]($reportVa + 0x4C0)
EmitPush 1
EmitPush 256
EmitPush $volumeInfoVa
EmitPush $ioStatusVa
EmitPushMemory $fileHandleVa
EmitCallImport 218
EmitCheckEqual 0
EmitLoadEax ([uint32]($ioStatusVa + 4))
EmitCheckAtLeast 18
EmitPush 3
EmitPush 24
EmitPush $volumeSizeVa
EmitPush $ioStatusVa
EmitPushMemory $fileHandleVa
EmitCallImport 218
EmitCheckEqual 0
EmitLoadEax ([uint32]($volumeSizeVa + 16))
EmitCheckNonZero
EmitLoadEax ([uint32]($volumeSizeVa + 20))
EmitCheckNonZero
EmitPush 4
EmitPush 8
EmitPush $volumeDeviceVa
EmitPush $ioStatusVa
EmitPushMemory $fileHandleVa
EmitCallImport 218
EmitCheckEqual 0
EmitLoadEax $volumeDeviceVa
EmitCheckEqual 7
EmitPush 5
EmitPush 128
EmitPush $volumeAttributesVa
EmitPush $ioStatusVa
EmitPushMemory $fileHandleVa
EmitCallImport 218
EmitCheckEqual 0
EmitLoadEax ([uint32]($volumeAttributesVa + 4))
EmitCheckNonZero
EmitLoadEax ([uint32]($volumeAttributesVa + 8))
EmitCheckNonZero

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

# 64-bit interlocked operations: atomic add and compare/exchange.
$largeIntegerVa = [uint32]($reportVa + 0x4E0)
$exchangeVa = [uint32]($reportVa + 0x4F0)
$comparandVa = [uint32]($reportVa + 0x4F8)
$destinationVa = [uint32]($reportVa + 0x500)
EmitMovMemoryImmediate $largeIntegerVa 10
EmitMovMemoryImmediate ([uint32]($largeIntegerVa + 4)) 0
EmitByte 0xB9; Emit32 $largeIntegerVa
EmitByte 0xBA; Emit32 5
EmitCallImport 20
EmitLoadEax $largeIntegerVa
EmitCheckEqual 15
EmitLoadEax ([uint32]($largeIntegerVa + 4))
EmitCheckEqual 0
EmitMovMemoryImmediate $destinationVa 15
EmitMovMemoryImmediate ([uint32]($destinationVa + 4)) 0
EmitMovMemoryImmediate $exchangeVa 99
EmitMovMemoryImmediate ([uint32]($exchangeVa + 4)) 0
EmitMovMemoryImmediate $comparandVa 15
EmitMovMemoryImmediate ([uint32]($comparandVa + 4)) 0
EmitByte 0xB9; Emit32 $destinationVa
EmitByte 0xBA; Emit32 $exchangeVa
EmitPush $comparandVa
EmitCallImport 21
EmitCheckEqual 15
EmitLoadEax $destinationVa
EmitCheckEqual 99
EmitLoadEax ([uint32]($destinationVa + 4))
EmitCheckEqual 0

# Interlocked primitive and byte/character conversion helpers.
EmitMovMemoryImmediate $interlockedVa 0
EmitByte 0xB9; Emit32 $interlockedVa
EmitCallImport 53
EmitCheckEqual 1
EmitByte 0xB9; Emit32 0x12345678
EmitCallImport 307
EmitCheckEqual 0x78563412
EmitByte 0xB9; Emit32 0x1234
EmitCallImport 318
EmitByte 0x25; Emit32 0xFFFF
EmitCheckEqual 0x3412
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

# 64-bit RTL arithmetic: 7 * 6 = 42, and 100 / 7 = 14 remainder 2.
EmitPush 6
EmitPush 0
EmitPush 7
EmitCallImport 281
EmitCheckEqual 42
EmitPush $numberValueVa
EmitPush 7
EmitPush 0
EmitPush 100
EmitCallImport 282
EmitCheckEqual 14
EmitLoadEax $numberValueVa
EmitCheckEqual 2
# Magic reciprocal: floor(12 * 0x5555555555555556 / 2^64) = 4.
EmitPush 0
EmitPush 0x55555555
EmitPush 0x55555556
EmitPush 0
EmitPush 12
EmitCallImport 283
EmitCheckEqual 4

# UnicodeString descriptor at report+0x150; its buffer is report+0x180.
$unicodeDescriptorVa = [uint32]($reportVa + 0x150)
$unicodeBufferVa = [uint32]($reportVa + 0x1A0)
$letterVa = [uint32]($reportVa + 0x180)
$formattedVa = [uint32]($reportVa + 0x190)
EmitMovMemoryImmediate $unicodeDescriptorVa 0x00100000
EmitMovMemoryImmediate ([uint32]($unicodeDescriptorVa + 4)) $unicodeBufferVa
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
EmitByte 0x25; Emit32 0xFF
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
EmitLoadEax $formattedVa
EmitByte 0x25; Emit32 0xFF
EmitCheckEqual 0x41


# Unicode case conversion and conversion-size queries.
$caseSourceDescriptorVa = [uint32]($reportVa + 0x160)
$caseOutputDescriptorVa = [uint32]($reportVa + 0x170)
$caseOutputBufferVa = [uint32]($reportVa + 0x1C0)
EmitMovMemoryImmediate $caseSourceDescriptorVa 0x00020002
EmitMovMemoryImmediate ([uint32]($caseSourceDescriptorVa + 4)) $unicodeBufferVa
EmitMovMemoryImmediate $caseOutputDescriptorVa 0x00080000
EmitMovMemoryImmediate ([uint32]($caseOutputDescriptorVa + 4)) $caseOutputBufferVa
EmitPush 0
EmitPush $caseSourceDescriptorVa
EmitPush $caseOutputDescriptorVa
EmitCallImport 314
EmitCheckEqual 0
EmitLoadEax $caseOutputBufferVa
EmitByte 0x25; Emit32 0xFFFF
EmitCheckEqual 0x41
EmitPush 0
EmitPush $caseSourceDescriptorVa
EmitPush $caseOutputDescriptorVa
EmitCallImport 276
EmitCheckEqual 0
EmitLoadEax $caseOutputBufferVa
EmitByte 0x25; Emit32 0xFFFF
EmitCheckEqual 0x61

EmitPush 1
EmitPush $letterVa
EmitPush $numberValueVa
EmitCallImport 300
EmitCheckEqual 0
EmitLoadEax $numberValueVa
EmitCheckEqual 2
EmitPush 2
EmitPush $unicodeBufferVa
EmitPush $numberValueVa
EmitCallImport 311
EmitCheckEqual 0
EmitLoadEax $numberValueVa
EmitCheckEqual 1



# Additional RTL APIs: compare memory, allocate/free Unicode strings, uppercase.
EmitPush 1
EmitPush $letterVa
EmitPush $letterVa
EmitCallImport 268
EmitCheckEqual 1

$createdUnicodeVa = [uint32]($reportVa + 0x1D0)
EmitPush $unicodeBufferVa
EmitPush $createdUnicodeVa
EmitCallImport 274
EmitCheckNonZero
EmitByte 0x0F; EmitByte 0xB7; EmitByte 0x05; Emit32 $createdUnicodeVa
EmitCheckEqual 2
EmitPush $createdUnicodeVa
EmitCallImport 287
EmitIncrement $passedVa

# RtlUpperString: lowercase "ff" in formattedVa becomes "FF".
$ansiSourceVa = [uint32]($reportVa + 0x1E0)
$ansiDestVa = [uint32]($reportVa + 0x1F0)
EmitMovMemoryImmediate $ansiSourceVa 0x00020002
EmitMovMemoryImmediate ([uint32]($ansiSourceVa + 4)) $formattedVa
EmitMovMemoryImmediate $ansiDestVa 0x00080000
EmitMovMemoryImmediate ([uint32]($ansiDestVa + 4)) $caseOutputBufferVa
EmitPush $ansiSourceVa
EmitPush $ansiDestVa
EmitCallImport 317
EmitLoadEax $caseOutputBufferVa
EmitByte 0x25; Emit32 0xFF
EmitCheckEqual 0x46

# Upcase Unicode-to-multibyte conversion.
EmitPush 2
EmitPush $unicodeBufferVa
EmitPush $numberValueVa
EmitPush 1
EmitPush $formattedVa
EmitCallImport 315
EmitCheckEqual 0
EmitLoadEax $formattedVa
EmitByte 0x25; Emit32 0xFF
EmitCheckEqual 0x41

# ANSI/Unicode descriptor initialization, comparison, equality, and status mapping.
$extraAnsiDescVa = [uint32]($reportVa + 0x2A0)
$extraUnicodeDescVa = [uint32]($reportVa + 0x2B0)
EmitPush $letterVa
EmitPush $extraAnsiDescVa
EmitCallImport 289
EmitByte 0x0F; EmitByte 0xB7; EmitByte 0x05; Emit32 $extraAnsiDescVa
EmitCheckEqual 2
EmitPush 0
EmitPush $extraAnsiDescVa
EmitPush $extraAnsiDescVa
EmitCallImport 270
EmitCheckEqual 0
EmitPush 1
EmitPush $extraAnsiDescVa
EmitPush $extraAnsiDescVa
EmitCallImport 279
EmitCheckEqual 1
EmitPush $unicodeBufferVa
EmitPush $extraUnicodeDescVa
EmitCallImport 290
EmitByte 0x0F; EmitByte 0xB7; EmitByte 0x05; Emit32 $extraUnicodeDescVa
EmitCheckEqual 2
EmitPush 0
EmitPush $extraUnicodeDescVa
EmitPush $caseSourceDescriptorVa
EmitCallImport 271
EmitCheckEqual 0
EmitPush 1
EmitPush $extraUnicodeDescVa
EmitPush $caseSourceDescriptorVa
EmitCallImport 280
EmitCheckEqual 1
EmitPush 0x61
EmitCallImport 316
EmitCheckEqual 0x41
EmitPush ([uint32]3221225485)
EmitCallImport 301
EmitCheckEqual 87


# Kernel dispatcher event lifecycle: initialize, signal, wait, reset, timeout.
$eventObjectVa = [uint32]($reportVa + 0x220)
$zeroTimeoutVa = [uint32]($reportVa + 0x230)
EmitPush 0
EmitPush 0
EmitPush $eventObjectVa
EmitCallImport 108
EmitPush 0
EmitPush 0
EmitPush 0
EmitPush 0
EmitPush $eventObjectVa
EmitCallImport 145
EmitCheckEqual 0
EmitPush 0
EmitPush 0
EmitPush 0
EmitPush 0
EmitPush $eventObjectVa
EmitCallImport 159
EmitCheckEqual 0
EmitPush $eventObjectVa
EmitCallImport 138
EmitCheckEqual 1
EmitPush $zeroTimeoutVa
EmitPush 0
EmitPush 0
EmitPush 0
EmitPush $eventObjectVa
EmitCallImport 159
EmitCheckEqual 0x102

# NT object query APIs: verify event, semaphore, mutant, and timer state.
$nativeObjectHandleVa = [uint32]($reportVa + 0x520)
$nativeObjectInfoVa = [uint32]($reportVa + 0x530)
$nativeReturnLengthVa = [uint32]($reportVa + 0x550)

# Create a signaled notification event and query EVENT_BASIC_INFORMATION.
EmitPush 1
EmitPush 0
EmitPush 0
EmitPush 0x1F0003
EmitPush $nativeObjectHandleVa
EmitCallImport 189
EmitCheckEqual 0
EmitPush $nativeReturnLengthVa
EmitPush 8
EmitPush $nativeObjectInfoVa
EmitPush 0
EmitPushMemory $nativeObjectHandleVa
EmitCallImport 209
EmitCheckEqual 0
EmitLoadEax ([uint32]($nativeObjectInfoVa + 4))
EmitCheckEqual 1
EmitPushMemory $nativeObjectHandleVa
EmitCallImport 187
EmitCheckEqual 0

# Query semaphore count and maximum after creating it at 2/5.
EmitPush 5
EmitPush 2
EmitPush 0
EmitPush 0
EmitPush $nativeObjectHandleVa
EmitCallImport 193
EmitCheckEqual 0
EmitPush $nativeReturnLengthVa
EmitPush 8
EmitPush $nativeObjectInfoVa
EmitPush 0
EmitPushMemory $nativeObjectHandleVa
EmitCallImport 214
EmitCheckEqual 0
EmitLoadEax $nativeObjectInfoVa
EmitCheckEqual 2
EmitLoadEax ([uint32]($nativeObjectInfoVa + 4))
EmitCheckEqual 5
EmitPushMemory $nativeObjectHandleVa
EmitCallImport 187
EmitCheckEqual 0

# An unowned mutant reports count 1 and false owner/abandoned flags.
EmitPush 0
EmitPush 0
EmitPush 0
EmitPush $nativeObjectHandleVa
EmitCallImport 192
EmitCheckEqual 0
EmitPush $nativeReturnLengthVa
EmitPush 8
EmitPush $nativeObjectInfoVa
EmitPush 0
EmitPushMemory $nativeObjectHandleVa
EmitCallImport 213
EmitCheckEqual 0
EmitLoadEax $nativeObjectInfoVa
EmitCheckEqual 1
EmitByte 0x0F; EmitByte 0xB6; EmitByte 0x05; Emit32 ([uint32]($nativeObjectInfoVa + 4))
EmitCheckEqual 0
EmitByte 0x0F; EmitByte 0xB6; EmitByte 0x05; Emit32 ([uint32]($nativeObjectInfoVa + 5))
EmitCheckEqual 0
EmitPushMemory $nativeObjectHandleVa
EmitCallImport 187
EmitCheckEqual 0

# A newly created notification timer is not signaled.
EmitPush 0
EmitPush 0
EmitPush 0
EmitPush $nativeObjectHandleVa
EmitCallImport 194
EmitCheckEqual 0
EmitPush $nativeReturnLengthVa
EmitPush 16
EmitPush $nativeObjectInfoVa
EmitPush 0
EmitPushMemory $nativeObjectHandleVa
EmitCallImport 216
EmitCheckEqual 0
EmitByte 0x0F; EmitByte 0xB6; EmitByte 0x05; Emit32 ([uint32]($nativeObjectInfoVa + 8))
EmitCheckEqual 0
# Arm the timer for a short relative due time; APC callbacks are deliberately null.
$timerDueVa = [uint32]($reportVa + 0x580)
$timerPreviousVa = [uint32]($reportVa + 0x590)
EmitMovMemoryImmediate $timerDueVa 4294957296
EmitMovMemoryImmediate ([uint32]($timerDueVa + 4)) 4294967295
EmitMovMemoryImmediate $timerPreviousVa 0
EmitPush $timerPreviousVa
EmitPush 0
EmitPush 0
EmitPush 0
EmitPush 0
EmitPush 0
EmitPush $timerDueVa
EmitPushMemory $nativeObjectHandleVa
EmitCallImport 229
EmitCheckEqual 0
EmitLoadEax $timerPreviousVa
EmitCheckEqual 0
EmitPushMemory $nativeObjectHandleVa
EmitCallImport 187
EmitCheckEqual 0

# Executive device queue lifecycle and keyed ordering.
$deviceQueueVa = [uint32]($reportVa + 0x300)
$deviceEntry1Va = [uint32]($reportVa + 0x340)
$deviceEntry2Va = [uint32]($reportVa + 0x350)
$deviceEntry3Va = [uint32]($reportVa + 0x360)
EmitPush $deviceQueueVa
EmitCallImport 106
# First insertion claims an idle device and is not queued.
EmitPush $deviceEntry1Va
EmitPush $deviceQueueVa
EmitCallImport 115
EmitCheckEqual 0
# A second entry is queued and can be removed.
EmitPush $deviceEntry2Va
EmitPush $deviceQueueVa
EmitCallImport 115
EmitCheckEqual 1
EmitPush $deviceQueueVa
EmitCallImport 134
EmitCheckEqual $deviceEntry2Va
EmitPush $deviceQueueVa
EmitCallImport 134
EmitCheckEqual 0

# Keyed insertion is stable in ascending key order; remove-by-key selects
# the first key >= requested key, then falls back to the head.
EmitPush $deviceQueueVa
EmitCallImport 106
EmitPush 0
EmitPush $deviceEntry1Va
EmitPush $deviceQueueVa
EmitCallImport 114
EmitCheckEqual 0
EmitPush 20
EmitPush $deviceEntry2Va
EmitPush $deviceQueueVa
EmitCallImport 114
EmitCheckEqual 1
EmitPush 10
EmitPush $deviceEntry3Va
EmitPush $deviceQueueVa
EmitCallImport 114
EmitCheckEqual 1
EmitPush 15
EmitPush $deviceQueueVa
EmitCallImport 133
EmitCheckEqual $deviceEntry2Va
EmitPush $deviceEntry2Va
EmitCallImport 135
EmitCheckEqual 0
EmitPush $deviceQueueVa
EmitCallImport 134
EmitCheckEqual $deviceEntry3Va
EmitPush $deviceQueueVa
EmitCallImport 134
EmitCheckEqual 0


# IRQL and critical-region bookkeeping.
EmitCallImport 103
EmitCheckEqual 0
EmitCallImport 129
EmitCheckEqual 0
EmitCallImport 103
EmitCheckEqual 2
EmitByte 0xB9; Emit32 0
EmitCallImport 161
EmitCallImport 103
EmitCheckEqual 0
EmitCallImport 101
EmitCallImport 122
EmitCallImport 130
EmitCheckEqual 0
EmitCallImport 103
EmitCheckEqual 1
EmitByte 0xB9; Emit32 0
EmitCallImport 161
EmitCallImport 103
EmitCheckEqual 0


# Executive read-write lock: exclusive and shared acquisition/release.
$rwLockVa = [uint32]($reportVa + 0x250)
EmitPush $rwLockVa
EmitCallImport 18
EmitPush $rwLockVa
EmitCallImport 12
EmitPush $rwLockVa
EmitCallImport 28
EmitIncrement $passedVa
EmitPush $rwLockVa
EmitCallImport 13
EmitPush $rwLockVa
EmitCallImport 28
EmitIncrement $passedVa

# RTL critical-section APC-region wrappers.
$criticalSectionVa = [uint32]($reportVa + 0x260)
EmitPush $criticalSectionVa
EmitCallImport 291
EmitPush $criticalSectionVa
EmitCallImport 278
EmitCheckEqual 0
EmitPush $criticalSectionVa
EmitCallImport 295
EmitIncrement $passedVa


# System-memory allocation and release.
$systemMemoryVa = [uint32]($reportVa + 0x270)
EmitPush 4
EmitPush 256
EmitCallImport 167
EmitStoreEax $systemMemoryVa
EmitCheckNonZero
EmitPush 256
EmitPushMemory $systemMemoryVa
EmitCallImport 172
EmitCheckNonZero

# Interlocked doubly-linked list operations use a guest spin lock.
$listHeadVa = [uint32]($reportVa + 0x300)
$listEntryAVa = [uint32]($reportVa + 0x310)
$listEntryBVa = [uint32]($reportVa + 0x320)
$listLockVa = [uint32]($reportVa + 0x330)
EmitMovMemoryImmediate $listHeadVa $listHeadVa
EmitMovMemoryImmediate ([uint32]($listHeadVa + 4)) $listHeadVa
EmitMovMemoryImmediate $listLockVa 0
EmitPush $listLockVa
EmitPush $listEntryAVa
EmitPush $listHeadVa
EmitCallImport 32
EmitCheckEqual 0
EmitPush $listLockVa
EmitPush $listEntryBVa
EmitPush $listHeadVa
EmitCallImport 33
EmitCheckEqual $listEntryAVa
EmitPush $listLockVa
EmitPush $listHeadVa
EmitCallImport 34
EmitCheckEqual $listEntryAVa
EmitPush $listLockVa
EmitPush $listHeadVa
EmitCallImport 34
EmitCheckEqual $listEntryBVa
EmitPush $listLockVa
EmitPush $listHeadVa
EmitCallImport 34
EmitCheckEqual 0

EmitMovMemoryImmediate $progressVa 1
# Initialize a KQUEUE, insert at tail/head, remove in FIFO order, then time out.
$queueVa = [uint32]($reportVa + 0xA00)
$queueTailEntryVa = [uint32]($reportVa + 0xA40)
$queueHeadEntryVa = [uint32]($reportVa + 0xA48)
$queueTimeoutVa = [uint32]($reportVa + 0xA60)
EmitPush 1
EmitPush $queueVa
EmitCallImport 111
EmitPush $queueTailEntryVa
EmitPush $queueVa
EmitCallImport 117
EmitCheckEqual 0
EmitPush $queueHeadEntryVa
EmitPush $queueVa
EmitCallImport 116
EmitCheckEqual 1
EmitPush 0
EmitPush 0
EmitPush $queueVa
EmitCallImport 136
EmitCheckEqual $queueHeadEntryVa
EmitPush 0
EmitPush 0
EmitPush $queueVa
EmitCallImport 136
EmitCheckEqual $queueTailEntryVa
EmitMovMemoryImmediate $queueTimeoutVa 4294957296
EmitMovMemoryImmediate ([uint32]($queueTimeoutVa + 4)) 4294967295
EmitPush $queueTimeoutVa
EmitPush 0
EmitPush $queueVa
EmitCallImport 136
EmitCheckEqual 258

EmitMovMemoryImmediate $progressVa 2
# Open the title directory and enumerate a real fixture entry.
$directoryUnicodeVa = [uint32]($base + 0x61C0)
$directoryObjectAttributesVa = [uint32]($base + 0x61D0)
$directoryPathVa = [uint32]($base + 0x6200)
$directoryHandleVa = [uint32]($reportVa + 0x7B0)
$directoryInfoVa = [uint32]($reportVa + 0x700)
$directoryIoVa = [uint32]($reportVa + 0x7A0)
EmitPush 1
EmitPush 7
EmitPush 0
EmitPush $directoryObjectAttributesVa
EmitPush 2147483648
EmitPush $directoryHandleVa
EmitCallImport 202
EmitCheckEqual 0
EmitPush 1
EmitPush 0
EmitPush 1
EmitPush 256
EmitPush $directoryInfoVa
EmitPush $directoryIoVa
EmitPush 0
EmitPush 0
EmitPush 0
EmitPushMemory $directoryHandleVa
EmitCallImport 207
EmitCheckEqual 0
EmitLoadEax ([uint32]($directoryInfoVa + 60))
EmitCheckNonZero
EmitLoadEax ([uint32]($directoryInfoVa + 56))
EmitCheckNonZero
EmitPushMemory $directoryHandleVa
EmitCallImport 187
EmitCheckEqual 0

EmitMovMemoryImmediate $progressVa 3
# The generic invalid-device dispatch routine must return its documented status.
EmitPush 0
EmitPush 0
EmitCallImport 74
EmitCheckEqual 3221225488

# Capture the live x86 guest context; EIP and ESP must be populated.
$guestContextVa = [uint32]($reportVa + 0x600)
EmitPush $guestContextVa
EmitCallImport 265
EmitLoadEax ([uint32]($guestContextVa + 184))
EmitCheckNonZero
EmitLoadEax ([uint32]($guestContextVa + 196))
EmitCheckNonZero

# Query full file attributes through the Xbox object-attributes path.
$fullAttributesVa = [uint32]($reportVa + 0x570)
$objectAttributesVa = [uint32]($base + 0x6090)
EmitPush $fullAttributesVa
EmitPush $objectAttributesVa
EmitCallImport 210
EmitCheckEqual 0
# FILE_NETWORK_OPEN_INFORMATION.EndOfFile is at byte offset 40.
EmitLoadEax ([uint32]($fullAttributesVa + 40))
EmitCheckEqual 8
# The test file must be reported as a nonzero-attribute regular file.
EmitLoadEax ([uint32]($fullAttributesVa + 48))
EmitCheckNonZero

# Interlocked singly-linked list push, pop, and flush.
$slistHeaderVa = [uint32]($reportVa + 0x280)
$slistEntryVa = [uint32]($reportVa + 0x290)
EmitMovMemoryImmediate $slistHeaderVa 0
EmitMovMemoryImmediate ([uint32]($slistHeaderVa + 4)) 0
EmitMovMemoryImmediate $slistEntryVa 0
EmitByte 0xB9; Emit32 $slistHeaderVa
EmitByte 0xBA; Emit32 $slistEntryVa
EmitCallImport 58
EmitCheckEqual 0
EmitByte 0xB9; Emit32 $slistHeaderVa
EmitCallImport 57
EmitCheckEqual $slistEntryVa
EmitByte 0xB9; Emit32 $slistHeaderVa
EmitCallImport 56
EmitCheckEqual 0

# These are host-module facilities, not kernel imports callable by this guest yet.
# Mark FATX API, NV2A/D3D rendering, and Xbox networking as NOT SUPPORTED.
EmitIncrement $skippedVa
EmitIncrement $skippedVa
EmitIncrement $skippedVa
EmitMovMemoryImmediate $progressVa 4
EmitByte 0x31; EmitByte 0xC0
EmitCheckEqual 0
Write-Host "Generated diagnostic assertions: $script:checkIndex"
EmitByte 0x31; EmitByte 0xC0; EmitByte 0xC3

$dataOffset = 0x6000
if ($script:code.Count -gt ($dataOffset - 0x1000)) { throw "Diagnostic XBE code overlaps its data region." }
$b = [byte[]]::new(0x8000)
function Put32([int]$offset,[uint32]$value) { [BitConverter]::GetBytes($value).CopyTo($b,$offset) }
[Text.Encoding]::ASCII.GetBytes("XBEH").CopyTo($b,0)
Put32 0x104 $base
Put32 0x108 0x1000
Put32 0x10C 0x8000
Put32 0x110 0x178
Put32 0x11C 1
Put32 0x120 0x10000178
Put32 0x128 ($entryVa -bxor [Convert]::ToUInt32('A8FC57AB',16))
Put32 0x158 ([uint32]($thunkVa -bxor [uint32]0x5B6D40B6))
# Executable, writable section: code at VA 0x10001000, data at 0x10002200.
Put32 0x178 5
Put32 0x17C $entryVa
Put32 0x180 0x7000
Put32 0x184 0x1000
Put32 0x188 0x7000
$script:code.ToArray().CopyTo($b,0x1000)
# The guest import resolver replaces these encoded ordinal slots with thunks.
for ($i=0; $i -lt $ordinals.Count; $i++) {
    Put32 (0x6800 + 4*$i) ([uint32](2147483648 + [int64]$ordinals[$i]))
}
Put32 (0x6800 + 4*$ordinals.Count) 0
Put32 0x6000 0x54535258
Put32 0x6028 4096
# Guest-side OBJECT_ATTRIBUTES + UNICODE_STRING for \Device\Cdrom0\xbe-test.txt.
$guestPath = "\Device\Cdrom0\xbe-test.txt"
$guestPathBytes = [Text.Encoding]::Unicode.GetBytes($guestPath + [char]0)
$guestPathLength = [uint32]($guestPath.Length * 2)
Put32 0x6080 ([uint32]($guestPathLength -bor (($guestPathLength + 2) -shl 16)))
Put32 0x6084 $testPathVa
Put32 0x6090 24
Put32 0x6094 0
Put32 0x6098 $unicodeVa
Put32 0x609C 0
Put32 0x60A0 0
Put32 0x60A4 0
Put32 0x60B0 0
Put32 0x60B4 0
Put32 0x60B8 0
$guestPathBytes.CopyTo($b,0x6100)
[Text.Encoding]::ASCII.GetBytes("1234" + [char]0).CopyTo($b,0x6140)
$directoryPath = "\Device\Cdrom0"
$directoryPathBytes = [Text.Encoding]::Unicode.GetBytes($directoryPath + [char]0)
$directoryPathLength = [uint32]($directoryPath.Length * 2)
$directoryPathVa = [uint32]($base + 0x6200)
Put32 0x61C0 ([uint32]($directoryPathLength -bor (($directoryPathLength + 2) -shl 16)))
Put32 0x61C4 $directoryPathVa
Put32 0x61D0 24
Put32 0x61D4 0
Put32 0x61D8 $directoryUnicodeVa
Put32 0x61DC 0
Put32 0x61E0 0
Put32 0x61E4 0
$directoryPathBytes.CopyTo($b,0x6200)

[Text.Encoding]::ASCII.GetBytes("A").CopyTo($b,0x6180)
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
    "--report-va","0x10006000"
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
    try {
        $pipe.WriteByte(0)
        $pipe.Flush()
    } catch {
        $exit = if ($proc.HasExited) { $proc.ExitCode } else { "still running" }
        throw "Guest pipe closed before diagnostic release; process exit=$exit. $($_.Exception.Message)"
    }
    $report = [byte[]]::new(24)
    $read = 0
    while ($read -lt $report.Length) {
        $n = $pipe.Read($report,$read,$report.Length-$read)
        if ($n -le 0) {
            $proc.Refresh()
            $exit = if ($proc.HasExited) { '0x{0:X8}' -f ($proc.ExitCode -band 0xFFFFFFFFL) } else { 'still running' }
            throw "Guest ended without returning diagnostic results; process exit=$exit."
        }
        $read += $n
    }
    $reportMagic = [BitConverter]::ToUInt32($report,0)
    $passed = [BitConverter]::ToUInt32($report,4)
    $failed = [BitConverter]::ToUInt32($report,8)
    $skipped = [BitConverter]::ToUInt32($report,12)
    $failedCheck = [BitConverter]::ToUInt32($report,16)
    $progress = [BitConverter]::ToUInt32($report,20)
    if ($reportMagic -ne 0x54535258) { throw "Invalid diagnostic report marker." }
    Write-Host "XboxRuntime Diagnostic XBE: $passed PASS, $failed FAIL, $skipped NOT SUPPORTED; first failed check #$failedCheck; progress marker $progress"
    if ($failed -ne 0 -or $passed -lt 20 -or $skipped -gt 3) {
        throw "Diagnostic XBE reported an unexpected result."
    }
    if (!$proc.WaitForExit(10000)) { throw "x86 guest did not exit after diagnostic completion." }
    if ($proc.ExitCode -ne 0) { throw "x86 guest exited with code $($proc.ExitCode)." }
}
finally {
    $pipe.Dispose()
    if ($proc -and !$proc.HasExited) { Stop-Process -Id $proc.Id -Force }
}
