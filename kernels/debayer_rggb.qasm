.include <vc4.qinc>

.set stride_x,  ra0
.set N_x,       ra1
.set stride_y,  ra2
.set N_y,       ra3
.set qpu_num,   ra4

.set img_in,    rb0
.set fb,        rb1

.set loop_x,    rb2
.set loop_y,    rb3
.set base_row,  ra6

.set img_in_cpy, ra7
.set fb_cpy,    ra8
.set row_mask,  ra9

# top row (bottom row)
# ylxl        yl      ylxr
# xl (ylxl)   x0 (yl) xr (ylxr)
# yrxl (xl)   yr (x0) yrxr (xr)
# (yrxl)      (yr)    (yrxr)

# G2  B
# R   G1

# these are relative to the top row, reused in the bottom
.set xl,        ra10
.set x0,        ra11
.set xr,        ra12
.set yr,        ra13
.set yrxl,      ra14
.set yrxr,      ra15

.set dif_x,     ra16
.set dif_y,     ra17
.set idxs,      ra18

.set row_store, ra19

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
mov fb, unif

mov row_mask, [0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1]

# start on row qpu_num * 2
shl loop_y, qpu_num, 1
shl base_row, qpu_num, 2

mov dif_x, 4
shl r0, N_x, 2
mov dif_y, r0
shl r0, elem_num, 2
mov idxs, r0

# use r3 as general accumulator?

:yloop
    mov loop_x, 0
    mov img_in_cpy, img_in
    mov fb_cpy, fb

    :xloop
        sub.setf -, row_mask, 0

        # odd rows: GB (since we start on row 1)
        mem_to_vpm_vec16 img_in, 0
        vpm_to_reg_vec16 r1, 0

        mov x0, r1

        add r2, idxs, img_in
        sub t0s, r2, dif_x  # xl
        add t0s, r2, dif_x  # xr
        sub t0s, r2, dif_y  # yl
        add t0s, r2, dif_y  # yr

        mov r3, 0

        ## G2
        # g
        mov.pack8bsf r3, r1

        # b
        ldtmu0
        mov xl, r4
        mov r0, r4
        ldtmu0
        mov xr, r4
        fadd r0, r0, r4
        fmul r0, r0, 0.5
        mov.pack8asf r3, r0

        # r
        ldtmu0
        mov r2, r4
        ldtmu0
        mov yr, r4
        fadd r2, r2, r4
        fmul r2, r2, 0.5
        mov.pack8csf r3, r2

        mov row_store, r3
        mov r3, 0

        ## B
        # g
        fadd r0, r0, r2
        fmul r0, r0, 0.5
        mov.pack8bsf r3, r0

        # b
        mov.pack8asf r3, r1

        # r
        add r2, idxs, img_in
        sub r0, 0, dif_y
        sub r1, r0, dif_x
        add t0s, r2, r1     # ylxl
        add r1, r0, dif_x
        add t0s, r2, r1     # ylxr

        mov r0, dif_y
        sub r1, r0, dif_x
        add t0s, r2, r1     # yrxl
        add r1, r0, dif_x
        add t0s, r2, r1     # yrxr

        ldtmu0
        mov r0, r4
        ldtmu0
        fadd r0, r0, r4
        ldtmu0
        mov yrxl, r4
        fadd r0, r0, r4
        ldtmu0
        mov yrxr, r4
        fadd r0, r0, r4
        fmul r0, r0, 0.25
        mov.pack8csf r3, r0

        mov.ifz r3, row_store

        reg_to_vpm_vec16 r3, 0
        vpm_to_mem_vec16 fb, 0

        #### next row... (RG)
        shl r0, N_x, 2
        add img_in, img_in, r0
        add fb, fb, r0

        # current x0 is previous yr

        add r2, idxs, img_in
        # don't need xl (previous yrxl)
        # don't need xr (previous yrxr)
        # don't need yl (previous x0)
        add t0s, r2, dif_y  # yr

        mov r3, 0

        ## G1
        # g
        mov.pack8bsf r3, yr

        # r
        mov r0, yrxl
        fadd r0, r0, yrxr
        fmul r0, r0, 0.5
        mov.pack8csf r3, r0

        # b
        ldtmu0
        fadd r2, x0, r4
        fmul r2, r2, 0.5
        mov.pack8asf r3, r2

        mov row_store, r3
        mov r3, 0

        ## R
        # g
        fadd r0, r0, r2
        fmul r0, r0, 0.5
        mov.pack8bsf r3, r0

        # r
        mov.pack8csf r3, yr

        # b
        add r2, idxs, img_in
        # already have ylxx (previous xl, xr)
        mov r0, dif_y
        sub r1, r0, dif_x   # yrxl
        add t0s, r2, r1
        add r1, r0, dif_x   # yrxr
        add t0s, r2, r1

        ldtmu0
        fadd r0, xl, r4
        fadd r0, r0, xr
        ldtmu0
        fadd r0, r0, r4
        fmul r0, r0, 0.25
        mov.pack8asf r3, r0

        mov.ifnz r3, row_store

        reg_to_vpm_vec16 r3, 0
        vpm_to_mem_vec16 fb, 0

        # img -1y +16x
        shl r0, stride_x, 2
        shl r1, N_x, 2
        sub r0, r0, r1
        add img_in, img_in, r0
        add fb, fb, r0

        add r0, loop_x, stride_x
        sub.setf -, r0, N_x

        brr.anyc -, :xloop
        mov loop_x, r0
        nop
        nop

    # loop back
    shl r0, stride_y, 2
    mul24 r0, r0, N_x
    add img_in, img_in_cpy, r0
    add fb, fb_cpy, r0

    add r0, loop_y, stride_y
    sub.setf -, r0, N_y
    brr.anyc -, :yloop
    mov loop_y, r0
    nop
    nop

nop; thrend
nop
nop

