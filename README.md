# Hardware-Software Interface (IHS)

Low-level systems programming and hardware-software interface projects developed in C and Assembly during the Computer Engineering course at **UFS**.

## Highlights

### Dynamic JIT Binary Translator (`3° Unidade/coias/magianegra.c`)
- **Architecture:** Custom bytecode/ISA-to-**x86-64** dynamic binary translator in C.
- **Mechanism:** Uses `mmap` (`PROT_READ | PROT_WRITE | PROT_EXEC`), `SIGTRAP` (`0xCC` software breakpoints), `SIGSEGV` exit trapping, and `ucontext_t` register inspection to lazily compile and patch machine code at runtime.
- **Features:** Register mapping (`RAX`–`R15`), SSE/XMM instruction counters, arithmetic/bitwise/branch emission (`MOV`, `ADD`, `SUB`, `AND`, `OR`, `XOR`, `SAL`, `SAR`, `CMP`, `JMP`, `JG`, `JL`, `JE`), and execution trace logging.

### Earlier Units
- **`1° Unidade/`:** Low-level C utilities (`image2ascii`, `pwcraker`) and architecture fundamentals.
- **`2° Unidade/`:** JIT experiments and hardware-software interface labs (`JIT`, `VP1`, `VP2`).
