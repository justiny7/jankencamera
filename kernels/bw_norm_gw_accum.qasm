.include <vc4.qinc>

.set stride_x, ra0
.set N_x, ra1
.set stride_y, ra2
.set N_y, ra3
.set qpu_num, ra4

.set img_in, rb0
.set accum_out, rb1

.set black_lvl, ra5
.set diff, ra6
.set mul, ra7

.set loop_x, rb2
.set loop_y, rb3
.set base_row, ra8

.set img_in_cpy, ra9

.macro mem_to_vpm_vec16, addr_reg, offset
    # read 1 row of length 16 at VPM row [offset]
    mov r0, vdr_setup_0(1, 16, 1, vdr_h32(1, offset, 0))

    # shift by 4 to switch to [base_row]'th row, add to vr_setup
    shl r1, base_row, 4
    add vr_setup, r0, r1

    # read [addr_reg] into ([base_row] + [offset])'th row of VPM
    mov vr_addr, addr_reg
    mov -, vr_wait
.endm

.macro vpm_to_mem_vec16, addr_reg, offset
    # write 1 row of length 16 at VPM row [offset]
    mov r0, vdw_setup_0(1, 16, dma_h32(offset, 0))

    # shift by 7 to switch to [base_row]'th row, add to vw_setup
    shl r1, base_row, 7
    add vw_setup, r0, r1

    # write ([base_row + [offset])'th row of VPM into [addr_reg]
    mov vw_addr, addr_reg
    mov -, vw_wait
.endm

.macro reg_to_vpm_vec16, src_reg, offset
    # set up vpm at row [offset]
    mov r0, vpm_setup(1, 1, h32(offset))

    # add [base_row] to [offset] and kick off VPM write
    add vw_setup, r0, base_row
    mov vpm, src_reg
.endm

.macro vpm_to_reg_vec16, dst_reg, offset
    # set up vpm at row [offset]
    mov r0, vpm_setup(1, 1, h32(offset))

    # add [base_row] to [offset] and kick off VPM read
    add vr_setup, r0, base_row
    mov dst_reg, vpm
.endm

mov stride_x, unif
mov N_x, unif
mov stride_y, unif
mov N_y, unif
mov qpu_num, unif

mov img_in, unif
mov accum_out, unif

mov black_lvl, unif
mov diff, unif
mov mul, unif

mov loop_y, qpu_num
shl base_row, qpu_num, 2

# let r3 be accum register
mov r3, 0

# skip num_qpus rows
:yloop
    mov loop_x, 0
    mov img_in_cpy, img_in

    # loop within a row
    :xloop
        mem_to_vpm_vec16 img_in, 0
        vpm_to_reg_vec16 r1, 0

        fsub r1, r1, black_lvl
        fmax r1, r1, 0.0
        fmin r1, r1, diff
        fmul r1, r1, mul

        fadd r3, r3, r1

        # write out black-level subbed img_in
        reg_to_vpm_vec16 r1, 0
        vpm_to_mem_vec16 img_in, 0

        shl r0, stride_x, 2
        add img_in, img_in, r0

        add r0, loop_x, stride_x
        sub.setf -, r0, N_x

        brr.anyc -, :xloop
        mov loop_x, r0
        nop
        nop

    shl r0, stride_y, 2
    mul24 r0, r0, N_x
    add img_in, img_in_cpy, r0

    add r0, loop_y, stride_y
    sub.setf -, r0, N_y
    brr.anyc -, :yloop
    mov loop_y, r0
    nop
    nop

reg_to_vpm_vec16 r3, 0
vpm_to_mem_vec16 accum_out, 0

nop; thrend
nop
nop

