import cv2
import numpy as np
from skimage.metrics import structural_similarity as ssim

MAC_DIR = "mac_output"
PI_DIR = "pi_naive_output"
N_FRAMES = 3

print(f"{'Frame':<10}\t{'PSNR (dB)':<15}\t{'SSIM':<10}")
print("-" * 35)

def eval(fn):
    mac = cv2.imread(f"{MAC_DIR}/{fn}.png")
    pi = cv2.imread(f"{PI_DIR}/{fn}.png")

    psnr = cv2.PSNR(mac, pi)
    s = ssim(mac, pi, channel_axis=2)
    print(f"{fn}\t{psnr:<15.2f}\t{s:.4f}")

for i in range(N_FRAMES):
    eval(f"output_{i}")

eval("output_mertens")
