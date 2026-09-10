/* The recompiled ARM engine runs natively on the GBA; the emulator is not built. */
typedef struct arm_emu_t arm_emu_t;
typedef uint32_t (*call68KFunc_f)(uint32_t emulStateP, uint32_t trapOrFunction, uint32_t argsOnStackP, uint32_t argsSizeAndwantA0);
