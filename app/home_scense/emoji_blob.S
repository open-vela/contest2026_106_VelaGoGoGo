    .section .rodata.emoji,"a",%progbits
    .global emoji_blob_data
.global emoji_blob_end
.balign 4
emoji_blob_data:
    .incbin "/home/mi/velago/contest2026_106_VelaGoGoGo/app/home_scense/emoji_blob.bin"
emoji_blob_end:
