import cv2
import argparse
from skimage.metrics import structural_similarity as ssim

def evaluate(reference_path, test_path):
    ref = cv2.imread(reference_path)
    test = cv2.imread(test_path)

    if ref is None:
        raise ValueError(f"Failed to load reference image: {reference_path}")

    if test is None:
        raise ValueError(f"Failed to load test image: {test_path}")

    if ref.shape != test.shape:
        raise ValueError(
            f"Image dimensions do not match:\n"
            f"Reference: {ref.shape}\n"
            f"Test:      {test.shape}"
        )

    psnr_value = cv2.PSNR(ref, test)
    ssim_value = ssim(ref, test, channel_axis=2)

    print(f"Reference Image : {reference_path}")
    print(f"Test Image      : {test_path}")
    print(f"PSNR             : {psnr_value:.2f} dB")
    print(f"SSIM             : {ssim_value:.6f}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Compute PSNR and SSIM between two images"
    )

    parser.add_argument("reference", help="Path to reference image")
    parser.add_argument("test", help="Path to test image")

    args = parser.parse_args()

    evaluate(args.reference, args.test)
