.include <vc4.qinc>

.set stride, ra0
.set N, ra1         # number of elements in img_out (GRAY)
.set qpu_num, ra2

.set img_in, rb1    # size N*3
.set img_gray, rb3  # size N
.set img_out, rb4   # size N
.set idxs, ra5

.set loop_counter, rb0

.set base_row, ra7

.set r, ra10
.set g, ra11
.set b, ra12
.set avg, rb21

.set div3, rb20
.set LUMA_R, rb10
.set LUMA_G, rb11
.set LUMA_B, rb12
.set exp_mul, rb13
.set neg_half_M_LOG2E, rb14

.set LUMA_R_V, 0.299
.set LUMA_G_V, 0.587
.set LUMA_B_V, 0.114

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

mov img_in, unif
mov img_gray, unif
mov img_out, unif

mov div3, 0.33333334
mov exp_mul, -12.5 * M_LOG2E    # -1.f / (2.f * sigma * sigma), sigma = 0.2f

mov LUMA_R, LUMA_R_V
mov LUMA_G, LUMA_G_V
mov LUMA_B, LUMA_B_V

# get TMU address offsets
shl r0, qpu_num, 4
add r0, r0, elem_num
shl idxs, r0, 2

shl base_row, qpu_num, 2

shl loop_counter, qpu_num, 4
:loop1
    # get idx*3, idx*3+1, idx*3+2 (except in bytes so offset=0, 4, 8)
    mul24 r0, idxs, 3
    add r0, r0, img_in
    mov t0s, r0
    add t0s, r0, 4
    add t0s, r0, 8

    ldtmu0
    mov r, r4
    ldtmu0
    mov g, r4
    ldtmu0
    mov b, r4

    # start exposedness
    fsub r0, r, 0.5
    fmul r0, r0, r0
    fmul sfu_exp, r0, exp_mul

    fsub r0, g, 0.5
    fmul r0, r0, r0
    mov r3, r4
    fmul sfu_exp, r0, exp_mul

    fsub r0, b, 0.5
    fmul r0, r0, r0
    fmul r3, r3, r4
    fmul sfu_exp, r0, exp_mul

    # start saturation
    mov r0, r
    fadd r0, r0, g
    fadd r0, r0, b
    fmul avg, r0, div3

    # finish exposedness
    fmul r3, r3, r4

    fsub r0, r, avg
    fsub r1, g, avg; fmul r0, r0, r0
    fsub r2, b, avg; fmul r1, r1, r1
    fadd r0, r0, r1; fmul r2, r2, r2
    fadd r0, r0, r2
    fmul sfu_recipsqrt, r0, div3

    # start luma
    fmul r0, r, LUMA_R
    fmul r1, g, LUMA_G

    # move to recip to get sqrt
    mov sfu_recip, r4

    # finish luma
    fmul r2, b, LUMA_B; fadd r1, r0, r1
    fadd r1, r1, r2
    reg_to_vpm_vec16 r1, 0
    vpm_to_mem_vec16 img_gray, 0

    # finish saturation
    fmul r3, r3, r4
    reg_to_vpm_vec16 r3, 0
    vpm_to_mem_vec16 img_out, 0

    shl r0, stride, 2
    add idxs, idxs, r0
    add img_gray, img_gray, r0
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

