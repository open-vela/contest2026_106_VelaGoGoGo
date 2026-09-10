    .section .rodata.emoji,"a",%progbits
    .global emoji_blob_data
    .global emoji_blob_end
    .balign 4
emoji_blob_data:
    .incbin "emoji_blob.bin"
emoji_blob_end:
