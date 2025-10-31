# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SSW is a high-performance, non-blocking TCP server implementation in C with integrated RESP2 (Redis Serialization Protocol) parser. The project demonstrates zero-copy parsing techniques and epoll-based event handling for scalable network I/O.

## Build Commands

```bash
# Create build directory and compile
mkdir -p cmake-build-debug
cd cmake-build-debug
cmake ..
make

# Run the executable
./ssw
```

The project uses CMake with C11 standard. The main executable is `ssw`.

## Architecture Overview

### Two-Layer Design

1. **Network Layer** (`server_/noblock_sserver.c/h`)
   - Epoll-based event loop with edge-triggered mode (EPOLLET)
   - Connection pool indexed by file descriptor for O(1) lookup
   - Dynamic buffer management with automatic expansion (up to 1GB limit)
   - Callback-driven architecture: `on_read`, `on_writer`, `on_error`

2. **Protocol Layer** (`protocol/resp2parser.c/h`, `protocol/resp2segments.c/h`)
   - Zero-copy RESP2 parser that references buffer segments directly
   - State machine handles packet fragmentation (COMPLETE/WAITING states)
   - Supports 5 RESP2 types: Simple Strings (+), Errors (-), Integers (:), Bulk Strings ($), Arrays (*)

### Key Design Patterns

**Zero-Copy Parsing**
- `parser_content_segment` stores pointers into the read buffer (`rbp`) rather than copying data
- Parser advances `rb_offset` only when a complete frame is parsed
- Segments reference original buffer location via `rbp` + `slen`

**Connection-Parser Binding**
- Each `connection_t` has `use_data` pointer for parser context
- Parser context (`parser_context`) maintains state across multiple `zerocopy_proceed()` calls
- State transitions: COMPLETE → process new frame, WAITING → accumulate more data

**Buffer Management Strategy**
- Read buffer: Grows when `space < rb_size/2`, doubles each expansion
- Write buffer: Uses `wb_offset` (send cursor), `wb_limit` (total to send), `wb_size` (capacity)
- On partial send (EAGAIN), re-registers EPOLLOUT and continues from `wb_offset`

**Segments Pool**
- Fixed global array `segments_pool[4096]` for zero-allocation parsing
- Single-threaded only (not thread-safe)
- Caller must use segments starting from index 0 sequentially

### Critical Flow: Handling Fragmented Packets

When a RESP2 frame arrives incomplete (e.g., bulk string `$5\r\nhe` without `llo\r\n`):

1. Parser enters WAITING state, saves `scan_pos` (current parse position)
2. `rb_offset` remains unchanged (no forward progress)
3. Next `zerocopy_proceed()` resumes from `scan_pos` when more data arrives
4. On completion, `consumed` bytes are calculated and `rb_offset` advances

For bulk strings specifically:
- First `\r\n` parse extracts length (`bulk_str_data_len`)
- Parser validates enough bytes exist: `cursor + 1 + dlen + 2 <= buffer_len`
- If insufficient, sets WAITING and stores `bulk_str_data_len` for next call
- Second `\r\n` completes frame, segment points to data body

### Error Handling Philosophy

- Protocol errors (malformed frames): Skip corrupted bytes, advance `rb_offset`, return to COMPLETE state
- Overflow detection: `BUFFER_SIZE_MAX` (1GB) limit, `-EMSGSIZE` requires external handling
- Integer parsing: Bounds-checked to prevent `long long` overflow, returns -1 on overflow
- Connection errors: Destroy connection, remove from epoll, close fd immediately

## Important Implementation Notes

**Parser State Machine**
- `state` represents result of *previous* call (initial state: COMPLETE)
- `consumed` tracks bytes processed in current call
- Only COMPLETE state advances `rb_offset`; WAITING state preserves position

**Epoll Write Handling**
- EPOLLOUT only registered when send() returns EAGAIN
- After complete send, EPOLLOUT is removed via EPOLL_CTL_MOD
- Write callback (`on_writer`) prepares data in write buffer before writere() sends

**Integer Parsing Optimization**
- `try_parser_positive_num_str_64()` uses fast path for len < 18 (guaranteed no overflow)
- For len >= 18, checks overflow before multiply: `acc > (LLONG_MAX - digit) / 10`

**Connection Lifecycle**
- `create_connection()`: Allocates fd-indexed slot, expands pool if needed
- `get_connection()`: Non-destructive lookup
- `take_connection()`: Removes from pool, decrements `active_count`
- `destroy_connection()`: Frees buffers, calls `use_data_free` callback, frees struct

## Testing Approach

The main.c:141-303 contains commented-out parser tests covering:
- Single frame parsing for all types
- Bulk string fragmentation across two recv() calls
- Partial CRLF at buffer boundary
- Oversized bulk string rejection
- Multi-frame sequential parsing

To enable tests: Uncomment test cases and ensure `segments_pool` is initialized.

## Critical Invariants

1. `rb_offset <= rb_size` always (checked in `zerocopy_proceed()`)
2. Parser only advances offset on COMPLETE, never on WAITING
3. Write buffer: `wb_offset <= wb_limit <= wb_size`
4. Connection pool: `connections[fd]` valid only if `fd < size`
5. Segments pool: Single-threaded access only, no concurrent modifications
