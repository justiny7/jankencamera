.include <vc4.qinc>

.set stride, ra0
.set N, ra1
.set qpu_num, ra2

.set dif_x, ra5
.set dif_y, ra6

.set img_in, rb1
.set img_out, rb3

.set loop_counter, rb0

.set base_row, ra7

.set idxs, ra19

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

mov stride, unif
mov N, unif
mov qpu_num, unif

mov dif_x, unif
mov dif_y, unif

mov img_in, unif
mov img_out, unif

# offsets
shl dif_x, dif_x, 2
shl dif_y, dif_y, 2
shl r0, elem_num, 2
mov idxs, r0

shl base_row, qpu_num, 2

shl loop_counter, qpu_num, 4
:loop1
    mem_to_vpm_vec16 img_out, 1
    mem_to_vpm_vec16 img_in, 0
    vpm_to_reg_vec16 r1, 0

    fmul r1, r1, 4.0
    fsub r1, 0, r1

    add r2, idxs, img_in
    add t0s, r2, dif_x  # offset x +1
    add t0s, r2, dif_y  # offset y +1

    sub r3, 0, dif_x
    add t0s, r2, r3     # offset x -1
    sub r3, 0, dif_y
    add t0s, r2, r3     # offset y -1
    
    ldtmu0
    fadd r1, r1, r4
    ldtmu0
    fadd r1, r1, r4
    ldtmu0
    fadd r1, r1, r4
    ldtmu0
    fadd.setf r1, r1, r4

    # abs val
    fsub.ifn r1, 0, r1

    # read output, mul onto it
    vpm_to_reg_vec16 r2, 1
    fmul r1, r1, r2

    reg_to_vpm_vec16 r1, 0
    vpm_to_mem_vec16 img_out, 0

    shl r0, stride, 2
    add img_in, img_in, r0
    add img_out, img_out, r0

    add r0, loop_counter, stride
    sub.setf -, r0, N

    brr.anyc -, :loop1
    mov loop_counter, r0
    nop
    nop

nop; thrend
nop
nop

