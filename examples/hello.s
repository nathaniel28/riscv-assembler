.text
addi a7, zero, 64
addi a0, zero, 0
auipc a1, 0
addi a1, a1, 28
addi a2, zero, 13
ecall
addi a7, zero, 93
addi a0, zero, 0
ecall
.ascii "Hello, world\n"
