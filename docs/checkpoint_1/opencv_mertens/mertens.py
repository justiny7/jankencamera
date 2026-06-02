import cv2
import numpy as np
import sys

# file of RAW bayer data from Pi output (each line contains 640x480 uint16, one for each pixel)
INPUT_FILE   = "input.txt"
WIDTH        = 640
HEIGHT       = 480
BAYER        = cv2.COLOR_BayerBG2BGR
BLACK_LEVEL  = 64
WHITE_LEVEL  = 1023

def load_frame(line, idx):
    values = np.fromstring(line.strip(), dtype=np.uint16, sep=' ')
    bayer  = values.reshape((HEIGHT, WIDTH))
    bayer  = np.clip(bayer.astype(np.int32) - BLACK_LEVEL, 0, WHITE_LEVEL - BLACK_LEVEL)
    bayer  = (bayer * 255 // (WHITE_LEVEL - BLACK_LEVEL)).astype(np.uint8)
    bgr    = cv2.cvtColor(bayer, BAYER)
    bgr    = cv2.xphoto.createGrayworldWB().balanceWhite(bgr)
    print(f"  Frame {idx} loaded")
    return bgr

with open(INPUT_FILE) as f:
    frames = [load_frame(l, i) for i, l in enumerate(f) if l.strip()]

for i, f in enumerate(frames):
    cv2.imwrite(f"frame_{i:02d}.png", f)

alignMTB = cv2.createAlignMTB()
alignMTB.process(frames, frames)

fused = cv2.createMergeMertens(1.0, 1.0, 1.0).process(frames)
cv2.imwrite("hdr_mertens.png",       np.clip(fused * 255.0,   0, 255  ).astype(np.uint8))
cv2.imwrite("hdr_mertens_16bit.png", np.clip(fused * 65535.0, 0, 65535).astype(np.uint16))
