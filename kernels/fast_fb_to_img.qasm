.include <vc4.qinc>

.set stride,        ra0
.set N,             ra1     # number of elements in img_out (RGB)
.set qpu_num,       ra2

.set fb,            rb0     # framebuffer, size N/3
.set img_out,       rb1     # RGB, size N
.set shifts,        rb2
.set conv_mul,      ra3

.set idxs,          rb3
.set loop_counter,  rb4
.set base_row,      ra4

.set div3,          ra5
.set EPS,           ra6
.set byte,          ra7

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

mov fb, unif
mov img_out, unif
mov shifts, unif
mov conv_mul, unif

# get TMU address offsets
shl r0, qpu_num, 4
add idxs, r0, elem_num

# constants
mov div3, 0.33333334
mov EPS, 0.1
mov byte, 0xFF

shl base_row, qpu_num, 2

# r3 is shifts
mem_to_vpm_vec16 shifts, 0
vpm_to_reg_vec16 r3, 0

shl loop_counter, qpu_num, 4
:loop1
    # calculate idxs / 3
    itof r0, idxs
    fmul r0, r0, div3
    fadd r0, r0, EPS
    ftoi r0, r0
    shl r0, r0, 2
    add t0s, r0, fb

    ldtmu0
    shr r1, r4, r3
    and r1, r1, byte
    itof r1, r1
    fmul r1, r1, conv_mul
    
    reg_to_vpm_vec16 r1, 1
    vpm_to_mem_vec16 img_out, 1

    shl r0, stride, 2
    add img_out, img_out, r0

    add idxs, idxs, stride
    add r0, loop_counter, stride
    sub.setf -, r0, N

    brr.anyc -, :loop1
    mov loop_counter, r0
    nop
    nop

nop; thrend
nop
nop

