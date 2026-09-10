/* Linker script for the Bike or Die 2 GBA port.
 * ROM holds code, read-only data and the game's databases; .data and .bss
 * live in EWRAM; .iwram/.iwram_bss are the hot code and data in IWRAM, below
 * the stack, which grows down from 0x03007F00. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY {
  rom   (rx)  : ORIGIN = 0x08000000, LENGTH = 4M
  prc   (r)   : ORIGIN = 0x08400000, LENGTH = 28M
  ewram (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
  iwram (rwx) : ORIGIN = 0x03000000, LENGTH = 32K
}

SECTIONS {
  .crt0 : { KEEP(*(.crt0)) . = ALIGN(4); } > rom
  .text : { *(.text .text.* .gnu.linkonce.t.*) *(.glue_7) *(.glue_7t) . = ALIGN(4); } > rom
  .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) . = ALIGN(4); } > rom

  .iwram : { __iwram_start = .; *(.iwram .iwram.*) . = ALIGN(4); __iwram_end = .; } > iwram AT> rom
  __iwram_lma = LOADADDR(.iwram);
  .data : { __data_start = .; *(.data .data.* .gnu.linkonce.d.*) . = ALIGN(4); __data_end = .; } > ewram AT> rom
  __data_lma = LOADADDR(.data);
  .iwram_bss (NOLOAD) : { __iwram_bss_start = .; *(.iwram_bss .iwram_bss.*) . = ALIGN(4); __iwram_bss_end = .; } > iwram
  /* IWRAM after the handler: 12 texture slots x 768 bytes of lookup tables at 0x03000400,
   * the engine's hot code (copied in by gba/src/engine.c) at 0x03002800, then the stack
   * growing down from 0x03007F00. */
  .bodlut 0x03000400 (NOLOAD) : { . = . + 0x2400; } > iwram
  .bodcode 0x03002800 (NOLOAD) : { __bodcode_start = .; . = . + 0x2C00; __bodcode_end = .; } > iwram
  .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON) . = ALIGN(4); __bss_end = .; } > ewram

  __eheap_start = __bss_end;
  __eheap_end = ORIGIN(ewram) + LENGTH(ewram);
  __iheap_start = __iwram_bss_end;
  __iheap_end = 0x03007000;
  __rom_end = LOADADDR(.data) + SIZEOF(.data);
  .prc : { KEEP(*(.prc)) . = ALIGN(4); } > prc

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.iplt) *(.igot*) *(.rel.*) }
}
