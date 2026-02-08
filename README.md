# pico2-swd-riscv

SWD debug probe for RP2350 RISC-V (Hazard3) cores. One Pico2 debugs another via two GPIO wires.

## 0. VIBE CODE WARNING (WRITTEN BY HUMAN)

About 80% of the code is vibe coded; The readme is almost completely generated (except the whole vibe-code-warning section). I spent many nights with the oscilloscope and the docs and made a working prototype that was able ti do sba/read/write regs and do abstract commands and progbuf, the rest was done with claude code. The tests are quite [comprehensive test suite](examples/test_suite) and I use the core of the library in my own projects, but, as they say, "hic sunt dracones". I also read the readme and the code didn't notice anything wrong (and removed the wrong/unclear parts).

This project was my casestudy of vibecoding a more complicated project that I dont understand 100% and there is no obvious existing code that can be "used". It started as ~1000 loc that I have written and knew very well, reading the rp2350, arm swd and riscv debug docs, capturing data with oscilloscope and openocd then decoding it and analyzing the wakeup sequence and then read/write commands. After I got it working I gave it to claude to make it into a library that I can use in other projects, and then I slowly built it up.

After about 3-4k lines of code I completely lost track of what is going on, and I woudn't consider this code that I have written, but adding more and more tests felt "nice", or at least reassuring.

There was a some gaslighting, particularly when it misunderstood dap_read_mem32 thinking it is reading from ram and not MEM-AP TAR/DRW/RDBUFF protocol, which lead to incredible amount of nonsense.

Overall I would say it was a horrible experience, even though it took 10 hours to write close to 10000 lines of code, I don't consider this my project, and I have no sense of acomplishment or growth.

In contrast, using AI to read all the docs (which are thousands of pages) and write helpful scripts to decode the oscilloscope data, create packed C structs from docs and etc, was very nice, and I did feel good after. The moment I read the first register and then when I was able to read memory via SBA I felt amazing.

The main issue is `taste`, when I write code I feel if its good or bad, as I am writing it, I know if its wrong, but using claude code I get desensitized very quickly and I just can't tell, it "reads" OK, but I don't know how it feels. In this case it happened when the code grew about 4x, from 1k to 4k lines. And worse of all, my mental model of the code is completely gone, and with it my ownership.

__The tokens have no reason or purpose__, which makes reading code ridiculously difficult, as and each token can be complete nonsense. When reading human code the symbols have a purpose, someone thought "I will put this in a variable, later I will check its status.", so I pretend I am them, and think `why` would they have written this? Shortly after I understand, as they are human and I am human. But the AI symbols have no reason, and worse of all, they all look deceptively correct, so I have to think 10 times harder if it is wrong. With any human code (including your own) it is quite easy to gauge how much you can trust it, and it is quite consistent, with the AI code, one function can be much better than what you woudld've written, and the code 2 lines below can be cargo culted gunk that looks incredibly good, but is structurally wrong.

In the end I would say I have gained good understanding of the wires, timings, and the lower level ap/dp mechanics, sba and progbuf, but I regret not writing the whole thing myself, even if it would've taken 10x the time.

I fucking hate this.

And I can not help, but feel dusgust and shame. Is this what programming is now? I really hope this is some intermediate stage and it changes for the better, the problem is I dont know what "better" is, it seems for some people is not writing the code, for others is not modeling the problem and for third is not having to think. For me, I am not sure, I do want to make things, and many times I dont want to know something, but I want to use it, e.g. the rp2350 usb host controller the way you have to re-arm interrupts and the way the epx register is shared is super annoying, for good reasons probably, but I just want to use it to make my CBI driver.

I guess the question is what is the thing I want to make, because you can go way up the stack, from the USB chip registers to CBI to UFI to FAT16 to the OS of the old school computer I am making, but why stop? make the schematics, the pcbs, the cad files, maybe automatically send it to the factory? and then just ship it to me? but why stop? make my webshop, start selling, make a community, ads, marketing, generate some unboxing videos, maybe some viral memes? process the orders directly to the factory, on demand, if there is an issue, it is ready with customer support.

What do I do in the meanwhile? Sit on the beach? I hate the beach.

Where does it stop?

PS: as the poet said: the price of getting what you want, is getting what you once wanted.

## Architecture

```
Application
    |
rp2350.c    RISC-V Debug Module (halt/resume/step, registers, memory, trace)
    |
dap.c       Debug Access Port (DP/AP registers, bank caching, MEM-AP)
    |
swd_protocol.c  SWD wire protocol (PIO bit-banging, packet encoding, retry)
    |
swd.pio     PIO state machine (4-cycle SWCLK, bidirectional SWDIO)
```

Each layer maintains its own state and speaks only to its neighbor.

## Usage

```c
swd_config_t config = swd_config_default();
config.pin_swclk = 2;
config.pin_swdio = 3;

swd_target_t *target = swd_target_create(&config);
swd_connect(target);
rp2350_init(target);

rp2350_halt(target, 0);
swd_result_t pc = rp2350_read_pc(target, 0);
rp2350_resume(target, 0);

swd_target_destroy(target);
```

## Hart Control

```c
rp2350_halt(target, 0);
rp2350_step(target, 0);
rp2350_resume(target, 0);
rp2350_reset(target, 0, true);

swd_result_t pc = rp2350_read_pc(target, 0);
rp2350_write_pc(target, 0, 0x20000000);

swd_result_t val = rp2350_read_reg(target, 0, 5);
rp2350_write_reg(target, 0, 5, 0xDEADBEEF);

uint32_t regs[32];
rp2350_read_all_regs(target, 0, regs);

swd_result_t csr = rp2350_read_csr(target, 0, 0x300);
rp2350_write_csr(target, 0, 0x300, value);
```

Both harts (0 and 1) are independently controllable.

## Memory Access

Non-intrusive via System Bus Access. Works while hart runs.

```c
swd_result_t val = rp2350_read_mem32(target, 0x20000000);
rp2350_write_mem32(target, 0x20000000, 0xDEADBEEF);

rp2350_read_mem16(target, addr);
rp2350_write_mem8(target, addr, byte);

uint32_t buf[256];
rp2350_read_mem_block(target, 0x20000000, buf, 256);
rp2350_write_mem_block(target, 0x20000000, buf, 256);
```

Block transfers use SBA auto-increment for performance.

## Code Execution

```c
const uint32_t program[] = {
    0x200415b7,  // lui  a1, 0x20040
    0xabcd0537,  // lui  a0, 0xabcd0
    0x00a5a223,  // sw   a0, 4(a1)
    0x0000006f,  // j    . (loop)
};

rp2350_execute_code(target, 0, 0x20000000, program, 4);
```

Uploads to target SRAM, verifies, sets PC, resumes.

## Instruction Tracing

```c
bool on_instruction(const trace_record_t *rec, void *ctx) {
    printf("0x%08x: 0x%08x\n", rec->pc, rec->instruction);
    return true;
}

int traced = rp2350_trace(target, 0, 100, on_instruction, NULL, false);
```

Single-steps through instructions via DCSR.step. ~5ms per instruction without register capture, ~80ms with full register capture.

## Program Buffer

Direct RISC-V instruction execution in debug context:

```c
uint32_t progbuf[] = {
    0x34202473,  // csrr s0, mcause
    0x00100073   // ebreak
};
rp2350_execute_progbuf(target, 0, progbuf, 2);
swd_result_t mcause = rp2350_read_reg(target, 0, 8);
```

CSR access uses this internally since Hazard3 does not support abstract CSR commands.

## Building

```cmake
add_subdirectory(lib/pico2-swd-riscv)
target_link_libraries(your_app pico2_swd_riscv)
```

Debug verbosity (compile-time):

```cmake
set(PICO2_SWD_DEBUG_LEVEL 3)  # 0=none, 1=warn, 2=info, 3=debug
```

## Internals

### Wire Protocol

SWD uses 8-bit request packets (start, APnDP, RnW, addr[3:2], parity, stop, park), 3-bit ACK (OK=1, WAIT=2, FAULT=4), and 33-bit data phases with parity. Turnaround cycles handle SWDIO direction changes. WAIT responses retry automatically (default: 5 retries, 100us backoff).

### RP2350 DP_SELECT Encoding

Non-standard: `[15:12]=APSEL, [11:8]=0xD, [7:4]=bank, [0]=ctrlsel`. The 0xD in bits[11:8] is required but undocumented.

### DM Activation

Three-phase handshake through Bank 1 CSW: deactivate (0x00000000), activate (0x00000001), full config (0x07FFFFC1). Expected status response: 0x04010001.

### Register Access

GPRs via abstract commands (regno 0x1000+n, 32-bit transfer). CSRs via program buffer: save s0, execute `csrr s0, <csr>` or `csrw <csr>, s0`, read/restore s0.

### System Bus Access

SBCS configured with sbaccess=32bit and sbreadonaddr. Writing SBADDRESS0 triggers the bus read; data available immediately in SBDATA0. Block transfers enable sbautoincrement for streaming reads/writes without per-word address setup.

### Single-Step

Read DCSR via progbuf, set step bit (bit 2), resume hart. Hart executes one instruction and re-enters debug mode. Clear step bit after.

## Limitations

- No hardware breakpoints (trigger module removed, planned for reimplementation)
- No multi-drop SWD
- No compressed instruction decoding (reads correctly, doesn't decode mnemonics)
- No flash programming (achievable via rp2350_execute_code with a stub)
- No cycle-accurate profiling

## References

- RISC-V External Debug Support v0.13
- ARM Debug Interface Architecture Specification ADIv5/v6
- RP2350 Datasheet, Chapter 3.5
- ARM CoreSight SWD-DP Technical Reference Manual

## License

MIT. See [LICENSE](LICENSE).
