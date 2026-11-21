/* The code image (runtime/interp/image.h), as build/imagegen wrote it:
   read-only data that each interpreter copies at boot. */
	.section .rodata.lu_image,"a",@progbits
	.balign 4096
	.globl lu_image_data
	.hidden lu_image_data
	.type lu_image_data, @object
lu_image_data:
	.incbin "build/image.bin"
	.size lu_image_data, . - lu_image_data
	.section .note.GNU-stack,"",@progbits
