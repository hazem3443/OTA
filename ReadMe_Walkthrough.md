# WalkThrough

## Bootloader

### ESP Memory Layout

- Each of the two Harvard Architecture Xtensa LX6 CPUs has 4 GB (32-bit) address space

- The data bus and instruction bus are both little-endian

- The CPU can access data bus addresses via aligned or non-aligned
byte, half-word and word read-and-write operations. The CPU can read and write data through the instruction
bus, but only in a word aligned manner; non-word-aligned access will cause a CPU exception

- Each CPU can directly access embedded memory through both the data bus and the instruction bus, external
memory which is mapped into the address space (via transparent caching & MMU), and peripherals.

there are two memory categories:

- Embedded Memory  
  - 1.1 internal ROM (448 KB)  
        1.1.1 Internal ROM 0 (384 KB)  
        1.1.2 Internal ROM 1 (64 KB)  
  - 1.2 internal SRAM (520 KB)  
        1.2.1 Internal SRAM 0 (192 KB)  
        1.2.2 Internal SRAM 1 (128 KB)  
        1.2.3 Internal SRAM 2 (200 KB)  
  - 1.3 RTC FAST memory (8 KB)(SRAM)  
  - 1.4 RTC SLOW memory (8 KB)(SRAM)  
- External Memory
  - 2.1 External Flash ( 4MB )
  - 2.1 External SRAM ( 4MB )

| Memory Section | Address Range | Size | Description |
| -------------- | ------------- | ------ | ----------- |
| Internal ROM 0 | 0x4000 0000 - 0x4000 7FFF + 0x4000 8000- 0x4005 FFFF | 32+352 KB | 384 KB of read-only memory that contains the boot code and some basic functions. |
| Internal ROM 1 | 0x3FF9 0000 - 0x3FF9 FFFF  | 64 KB | 64 KB of read-only memory that contains some cryptographic functions. |
| Internal SRAM 0 | 0x4007 0000 - 0x4007 FFFF + 0x4008 0000 - 0x4009 FFFF | 64+128 KB | 192 KB of data memory that is accessible by both CPUs and DMA. |
| Internal SRAM 1 | 0x400A 0000 - 0x400A FFFF + 0x400B 0000 - 0x400B 7FFF + 0x400B 8000 - 0x400B FFFF (or remap 0x3FFE 0000 - 0x3FFF FFFF - DMA ) | 64+32+32 KB |  128 KB of data memory that is accessible by both CPUs and DMA. |
| Internal SRAM 2 | 0x3FFA E000 - 0x3FFD FFFF | 200 KB | 200 KB of instruction memory that is accessible by CPU1 only. |
| RTC FAST Memory | 0x400C0000 - 0x400C 1FFF (remap to data BUS 0x3FF8 0000 - 0x3FF8 1FFF) | 8 KB | 8 KB of data memory that is accessible by PRO_CPU only and ULP coprocessor. It can retain data in deep-sleep mode. |
| RTC SLOW Memory | 0x5000 0000 - 0x5000 1FFF | 8 KB | 8 KB of data memory that is accessible by both CPUs and ULP coprocessor. It can retain data in deep-sleep mode. |
| External Flash | Configurable (0x400C 2000 - 0x40BF FFFF on the instruction Bus ) or ( 0x3F40 0000 - 0x3F7F FFFF Data bus remap) | 4 MB | Up to 16 MB of external SPI flash that can be mapped to the CPU address space. |
| External SRAM    | Configurable (0x3F80 0000 - 0x3FBF FFFF)    | 4 MB | Up to 8 MB of external SPI PSRAM that can be mapped to the CPU address space. |
