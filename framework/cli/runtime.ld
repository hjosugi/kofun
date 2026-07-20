ENTRY(_start)

PHDRS
{
    text PT_LOAD FILEHDR PHDRS FLAGS(5);
    config PT_LOAD FLAGS(4);
}

SECTIONS
{
    . = 0x400000 + SIZEOF_HEADERS;
    .text : {
        KEEP(*(.text.start))
        *(.text .text.*)
        *(.rodata .rodata.*)
    } :text

    . = ALIGN(0x1000);
    .cli_config : {
        KEEP(*(.cli_config))
    } :config

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
        *(.data)
        *(.bss)
    }
}
