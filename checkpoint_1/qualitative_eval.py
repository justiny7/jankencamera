import cv2
import numpy as np
from skimage.metrics import structural_similarity as ssim

FRAMES_DIR  = "opencv_demosaic_wb"
MERTENS_DIR = "opencv_mertens"
N_FRAMES    = 5

ref = cv2.imread(f"{MERTENS_DIR}/hdr_mertens.png")

print(f"{'Frame':<10} {'PSNR (dB)':<15} {'SSIM':<10}")
print("-" * 35)

for i in range(N_FRAMES):
    frame = cv2.imread(f"{FRAMES_DIR}/frame_{i:02d}.png")
    psnr  = cv2.PSNR(ref, frame)
    s     = ssim(ref, frame, channel_axis=2)
    print(f"frame_{i:02d}   {psnr:<15.2f} {s:.4f}")
