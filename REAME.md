# CTMP Proxy — Operation WIRE STORM

A high-speed TCP proxy in C++17 for the CoreTech Message Protocol (CTMP).
Supports:
- Stage 1 (original 8-byte header)
- Stage 2 (options + checksum for sensitive messages)

Ports:
- Source (one client): TCP 33333
- Destinations (many clients): TCP 44444

-----------------------------------------------------------------------

## Overview

The proxy accepts CTMP messages from a single source on 33333 and broadcasts
each valid message, in arrival order, to all destination clients connected on
44444.

Validation performed:
- Magic byte (0xCC)
- Length (must match payload length; length excludes header)
- Padding (final 2 bytes in header must be 0x00 0x00)
- If sensitive (options bit 1 set: 0x40), validate checksum

-----------------------------------------------------------------------

## Protocol (Stage 2 header, 8 bytes)

Byte layout:
- [0] MAGIC: 0xCC
- [1] OPTIONS: bit 1 (0x40) = sensitive message
- [2..3] LENGTH (uint16, big-endian) — number of DATA bytes (not including header)
- [4..5] CHECKSUM (uint16, big-endian)
- [6..7] PADDING = 0x00 0x00
Followed by DATA (exactly LENGTH bytes).

Checksum rule (Stage 2, sensitive only):
- Packet is accepted if the 16-bit one’s-complement sum of all 16-bit words
  across the entire header + data equals 0xFFFF. (Equivalent to the sender
  computing the checksum over header+data with the checksum field treated as
  0xCC 0xCC.)

-----------------------------------------------------------------------

## Build

Ubuntu 24.04 LTS (and macOS) with g++.

Build Stage 2 (full) proxy:
$ g++ -std=c++17 -pthread -Wall -Wextra -O2 -o ctmp_proxy main.cpp

Optionally, build Stage 1–only proxy:
$ g++ -std=c++17 -pthread -Wall -Wextra -O2 -o ctmp_stage1 main_stage1.cpp

No third-party libraries. C++17 standard library only.

-----------------------------------------------------------------------

## Usage

Start the Stage 2 proxy (keep it running):
$ ./ctmp_proxy &

- Source connects to localhost:33333 and sends CTMP-framed messages.
- Any number of receivers connect to localhost:44444 and will receive every
  valid message in order.

Quick manual smoke test (optional):

Receiver (destination):
$ nc localhost 44444

In another terminal: Source (send raw CTMP bytes or your own generator).
For real validation, use the Python tests below.

Stop the proxy:
$ pkill ctmp_proxy

-----------------------------------------------------------------------

## Testing

Open a new terminal for each test step.

Stage 1 tests:
1) Run Stage 1 proxy:
$ ./ctmp_stage1 &

2) Execute tests:
$ cd ~/projects/wire-storm
$ python3 tests.py

3) Stop Stage 1 proxy:
$ pkill ctmp_stage1

Stage 2 tests:
1) Run Stage 2 proxy:
$ cd ~/projects/ctmp-proxy
$ ./ctmp_proxy &

2) Execute tests:
$ cd ~/projects/wire-storm/ws-second-stage/wire-storm-reloaded-1.0.0
$ python3 tests.py

3) Stop Stage 2 proxy:
$ pkill ctmp_proxy

Both suites should end with:
OK

-----------------------------------------------------------------------

## Notes

- Broadcast order matches the receive order from the source.
- Invalid or oversized messages are dropped (not forwarded).
- Destination sockets that fail on send are closed and removed.

-----------------------------------------------------------------------

## License

Submitted under CoreTech’s WIRE STORM challenge terms.











































































