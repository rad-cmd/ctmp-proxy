# CTMP Proxy for Operation WIRE STORM

A high-speed, multi-threaded TCP proxy in C++17 implementing the CoreTech Message Protocol (CTMP), extended for sensitive-message checksum validation.

---

## Mission Summary

The mission is to relay CTMP-framed messages from a single **source** to multiple **destination** clients, over a known-compromised server:

- **Source** connects on TCP port **33333**  
- **Destinations** connect on TCP port **44444**

Messages must be validated (magic, length, padding, and—when flagged—checksum) and broadcast in arrival order.

---

## Protocol Specification

Each CTMP packet is:

- **MAGIC** (1 byte): `0xCC`  
- **OPTIONS** (1 byte): bit 1 (0x40) = **sensitive** → checksum required  
- **LENGTH** (2 bytes, big-endian): number of **DATA** bytes  
- **CHECKSUM** (2 bytes, big-endian): 16-bit one’s-complement sum over header+data, with these two bytes zeroed when calculating  
- **PADDING** (2 bytes): must be `0x00 0x00`  

Followed by **DATA** (exactly `LENGTH` bytes).

---

## Features

- Validates **magic**, **length**, **padding**  
- Validates 16-bit checksum for sensitive messages  
- Drops malformed or oversized packets  
- Thread-per-connection (`std::thread` + `std::mutex`)  
- No external dependencies (pure C++17 standard library)  

---

## Build & Run

Clone & build (choose one):

1) HTTPS (no SSH key needed):  
`git clone https://github.com/rad-cmd/ctmp-proxy.git`

or

2)SSH (if you’ve added your SSH key to GitHub)**:  
`git clone git@github.com:rad-cmd/ctmp-proxy.git`

Then:
  
- `cd ctmp-proxy`  
- `g++ -std=c++17 -pthread -Wall -Wextra -o ctmp_proxy main.cpp``  
- `./ctmp_proxy` (This starts the proxy- keep this running)

## Testing

Open a new terminal window for testing  

**Stage 1 tests**
- `cd ~/projects/wire-storm`
- `python3 tests.py`

**Stage 2 tests**  
- `cd ~/projects/wire-storm/ws-second-stage/wire-storm-reloaded-1.0.0`  
- `python3 tests.py`  

Both sets of tests should print **OK**

## License
Submitted under CoreTech’s WIRE STORM challenge terms. 
