$ErrorActionPreference = "Stop"
$guest = Join-Path $PSScriptRoot "..\..\build-guest32\out\xbe_guest32.exe"
$guest = [IO.Path]::GetFullPath($guest)
if (!(Test-Path $guest)) { throw "Guest helper missing: $guest" }

$temp = Join-Path $env:RUNNER_TEMP "XboxRuntimeGuestSmoke"
New-Item -ItemType Directory -Force -Path $temp | Out-Null
$xbe = Join-Path $temp "native-ret-xbe.xbe"

# Minimal XBEH: one executable section containing RET, no kernel imports.
# This verifies real 32-bit entry execution, not a retail game's compatibility.
$b = [byte[]]::new(0x1001)
[Text.Encoding]::ASCII.GetBytes("XBEH").CopyTo($b, 0)
function Put32([int]$offset, [uint32]$value) {
    [BitConverter]::GetBytes($value).CopyTo($b, $offset)
}
Put32 0x104 0x00010000
Put32 0x108 0x1000
Put32 0x10C 0x2000
Put32 0x110 0x178
Put32 0x11C 1
Put32 0x120 0x00010178
Put32 0x128 (0x00011000 -bxor [Convert]::ToUInt32('A8FC57AB', 16))
Put32 0x158 0x5B6D40B6
# Section header at file offset 0x178.
Put32 0x178 4
Put32 0x17C 0x00011000
Put32 0x180 0x1000
Put32 0x184 0x1000
Put32 0x188 1
$b[0x1000] = 0xC3
[IO.File]::WriteAllBytes($xbe, $b)

$name = "XboxRuntimeSmoke-$PID"
$pipe = [IO.Pipes.NamedPipeServerStream]::new(
    $name, [IO.Pipes.PipeDirection]::InOut, 1,
    [IO.Pipes.PipeTransmissionMode]::Byte, [IO.Pipes.PipeOptions]::None
)
$pipePath = "\\.\pipe\$name"
$proc = Start-Process -FilePath $guest -ArgumentList @("--pipe", $pipePath, "--xbe", $xbe) -PassThru -NoNewWindow
try {
    $task = $pipe.WaitForConnectionAsync()
    if (!$task.Wait(15000)) { throw "x86 guest did not connect to its host pipe." }
    $reply = [byte[]]::new(28)
    $read = 0
    while ($read -lt $reply.Length) {
        $n = $pipe.Read($reply, $read, $reply.Length - $read)
        if ($n -le 0) { throw "Guest closed the pipe before its launch reply." }
        $read += $n
    }
    $magic = [BitConverter]::ToUInt32($reply, 0)
    $status = [BitConverter]::ToUInt32($reply, 8)
    $entry = [BitConverter]::ToUInt32($reply, 20)
    if ($magic -ne 0x47584258 -or $status -ne 0 -or $entry -ne 0x11000) {
        throw "Unexpected guest launch reply: magic=$('{0:X8}' -f $magic), status=$status, entry=$('{0:X8}' -f $entry)"
    }
    $pipe.WriteByte(0)
    $pipe.Flush()
    $pipe.Dispose()
    if (!$proc.WaitForExit(10000)) { throw "Guest helper did not shut down after the entry returned." }
    if ($proc.ExitCode -ne 0) { throw "Guest helper exited with code $($proc.ExitCode)." }
    Write-Host "PASS: native x86 XBE entry point executed and returned."
}
finally {
    if ($pipe) { $pipe.Dispose() }
    if ($proc -and !$proc.HasExited) { Stop-Process -Id $proc.Id -Force }
}
