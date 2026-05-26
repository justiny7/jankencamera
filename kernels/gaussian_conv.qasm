.include <vc4.qinc>

.set stride, ra0
.set N, ra1
.set qpu_num, ra2

# probably need:
# total pixels (size * fmt)
# width, height
# stride (width * fmt)

# how to sort... each QPU gets its own pixel, interleave by R/G/B?
# then each QPU can get hmax / hmin (depending on which RGB)
# similar for vmax / vmin?
# ^ this is assuming border cases are just clamped (no reflection probably)
# or special case borders? <- then each QPU can get a row

# two passes, one for vert and one for hor? since we need entire grid

# NEW PLAN: just do 2-pixel border on CPU, do rows [2, N-3] on QPU
# allow invalid reads between rows (for x-positions 0-2, N-2, N-1) bc we'll overwrite anyways
# ^ this is safe bc the under/overflows are still in valid memory
# also then we don't have to care about x/y position anymore, every position is the same

.set dif, ra5
.set mul, ra20

.set img_in, rb1
.set img_out, rb3

.set loop_counter, rb0

.set base_row, ra7

.set GK0_v, 0.0625
.set GK1_v, 0.25
.set GK2_v, 0.375
.set GK3_v, 0.25
.set GK4_v, 0.0625

.set GK0, ra14
.set GK1, ra15
.set GK2, ra16
.set GK3, ra17
.set GK4, ra18

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

mov dif, unif
mov mul, unif

mov img_in, unif
mov img_out, unif

mov GK0, GK0_v
mov GK1, GK1_v
mov GK2, GK2_v
mov GK3, GK3_v
mov GK4, GK4_v

# offsets
shl dif, dif, 2
shl r0, elem_num, 2
mov idxs, r0

shl base_row, qpu_num, 2

.macro convolve, in, out, s
    mem_to_vpm_vec16 in, 0
    vpm_to_reg_vec16 r1, 0
    fmul r0, r1, GK2

    add r2, idxs, in

    mov r3, s
    add t0s, r2, r3 # offset +1

    add r3, r3, s
    add t0s, r2, r3 # offset +2

    sub r3, 0, s
    add t0s, r2, r3 # offset -1

    sub r3, r3, s
    add t0s, r2, r3 # offset -2

    ldtmu0
    fmul r1, r4, GK3
    ldtmu0
    fadd r0, r0, r1; fmul r2, r4, GK4
    ldtmu0
    fadd r0, r0, r2; fmul r1, r4, GK1
    ldtmu0
    fadd r0, r0, r1; fmul r2, r4, GK0
    fadd r1, r0, r2
    fmul r1, r1, mul

    reg_to_vpm_vec16 r1, 0
    vpm_to_mem_vec16 out, 0
.endm


shl loop_counter, qpu_num, 4
:loop1
    convolve img_in, img_out, dif

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

