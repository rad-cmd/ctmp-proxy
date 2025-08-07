# CTMP Proxy for Operation WIRE STORM

A **high-speed**, **multi-threaded** TCP proxy in **C++17** implementing the  
CoreTech Message Protocol (CTMP), Stage 1 & Stage 2.  

- **Source** listens on port **33333**  
- **Destinations** listen on port **44444**  

---

## Table of Contents

1. [Overview](#overview)  
2. [Protocol Specification](#protocol-specification)  
3. [Features](#features)  
4. [Build Instructions](#build-instructions)  
5. [Usage](#usage)  
6. [Testing](#testing)  
7. [Design Notes](#design-notes)  
8. [License](#license)  

---

## Overview

Reads CTMP messages from one source, validates them, and broadcasts valid  
packets in arrival order to all connected destinations. Supports:

- **Stage 1**: 8-byte header (magic, padding, length, padding)  
- **Stage 2**: 8-byte header extended with **OPTIONS** and **CHECKSUM**  

---

## Protocol Specification

**Header (8 bytes)**

| Offset | Field     | Size | Description                             |
|:------:|:----------|:----:|:----------------------------------------|
| 0      | MAGIC     | 1    | `0xCC`                                  |
| 1      | OPTIONS   | 1    | bit 1 = 0x40 → sensitive (checksum)     |
| 2–3    | LENGTH    | 2    | Payload length (big-endian)             |
| 4–5    | CHECKSUM  | 2    | 16-bit one’s-complement (sensitive)     |
| 6–7    | PADDING   | 2    | Must be `0x00 0x00`                     |

Followed by **DATA** (`LENGTH` bytes).

- Non-sensitive (`OPTIONS & 0x40 == 0`): checksum ignored.  
- Sensitive (`OPTIONS & 0x40 != 0`): compute checksum with bytes 4–5 = `0xCC` and verify.

---

## Features

- Single **source** on port 33333  
- Multiple **destinations** on port 44444  
- Validates magic, length, padding and (for sensitive) checksum  
- Drops malformed or oversized packets  
- Uses `std::thread` + `std::mutex` for safe broadcasting  
- No external dependencies (pure C++17 standard library)  

**Build & Run**

\`\`\`bash
# Clone & build
git clone git@github.com:rad-cmd/ctmp-proxy.git
cd ctmp-proxy
g++ -std=c++17 -pthread -Wall -Wextra -o ctmp_proxy main.cpp

# Start the proxy in the background
./ctmp_proxy &
#   → source on port 33333
#   → destinations on port 44444
\`\`\`

---

**Usage**

\`\`\`bash
# 1) Send CTMP messages as your source:
nc localhost 33333 < example.ctmp

# 2) Connect a destination to receive messages:
nc localhost 44444
\`\`\`

---

**Testing**

\`\`\`bash
# Stage 1 tests
cd ~/projects/wire-storm
python3 tests.py

# Stage 2 tests
cd ~/projects/wire-storm/ws-second-stage/wire-storm-reloaded-1.0.0
python3 tests.py
\`\`\`

---


## License:
Submitted under CoreTech’s WIRE STORM challenge terms.

