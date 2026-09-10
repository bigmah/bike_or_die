@ The ROM data image (game, system resources, level packs), at a fixed address
@ so the packer can bake absolute pointers into it.
    .section .prc, "a"
    .align 2
    .global rom_image
rom_image:
    .incbin "../build/gba/data.bin"
