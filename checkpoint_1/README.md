# Checkpoint 1: Evals

## Goals
My goal for this project is to evaluate how accurately and efficiently I can perform a basic exposure bracketing HDR pipeline consisting of at least the following components:
1. Get RAW Bayers from camera
2. HDR merge
3. Demosaic
4. White balance
5. Tone map

There are two aspects to this evaluation:
- Efficiency
    - I'll measure processing times for each component and peak memory usage
    - There isn't really a fair comparison against real phones/digital cameras since I'm on a resource-constrained device, but I plan to perform ablation studies on optimizations I make to see the speed progression
- Accuracy
    - For quantitative accuracy, I'll implement the exact same algorithms on my Mac with full precision and run them on the same RAW Bayer inputs
        - I'll use PSNR and SSIM metrics to compare the images produced by the Raspberry Pi to Mac's ground truth (since I'll probably be cutting corners on the Pi to make it run fast enough)
    - For qualitative analysis, I'll use an existing exposure bracketing implementation (e.g. OpenCV) and run the same RAW Bayer inputs. Then, I'll conduct a survey for how preferable the Pi output is vs. the OpenCV output
        - I can randomly assign image A/B to Pi/OpenCV and give options of strongly prefer A, slightly prefer A, they're about the same, slightly prefer B, strongly prefer B
        - It's a win if the average answer is "they're about the same"

## Progress

### API
I spent the last two weeks setting up the camera drivers and FrankenCamera-like API. I'm able to configure a sequence of shots, each with a certain exposure, gain, and relative timestamp I want to take the photo at.

These are the current config/shot/frame attributes:

**Config**
- Width (in pixels)
- Height (in pixels)
- Stride (bytes per row)
- Format (10-bit or 8-bit per pixel)

**Shot**:
- Exposure (in microseconds)
- Gain (analog × digital gain in multiplier amount)
- Analog gain (in multiplier amount)
- Digital gain (in multiplier amount)
- Timestamp (from system clock, in microseconds)
- Error (difference between actual and intended capture time, in microseconds)
- Delay (time after last frame if part of a series of shots, in microseconds)

**Frame**:
- Buffer (pointer to RAW Bayer output from camera stream)
- Size (buffer size in bytes)
- Sequence number (global counter of images taken)
- Config (camera config object associated with this frame)
- Shot (shot object associated with this frame)

I tested the API by taking a sequence of five photos at different exposures in my dorm (`bayer_frames`), each spaced one second apart. These were the results from the shots of each frame:

| Frame    | Exposure (us) | Gain (×)  | Timestamp (us) | Delay (us)   | Error (us) |
| -------- | -------- | ----- | --------- | ------- | ----- |
| frame_00 | 2495     | 2.000 | 31081959  | 0       | 0     |
| frame_01 | 5009     | 4.000 | 32115098  | 1033139 | 33139 |
| frame_02 | 10000    | 8.000 | 33114910  | 999812  | 32951 |
| frame_03 | 20000    | 8.000 | 34114722  | 999812  | 32763 |
| frame_04 | 40000    | 8.000 | 35109864  | 995142  | 27905 |

I followed the FrankenCamera philosphy of best-effort, since it's basically impossible to get the timings exactly right (for the IMX219, frames stream in at a certain framerate so I can't sample at arbitrary timestamps).

### Evaluation
I haven't decided the exact scenes I want to use yet, but I performed an example evalution using the Bayer outputs from my dorm. I set up the OpenCV script to demosaic and white balance (`opencv_demosaic_wb`), and perform Mertens exposure bracketing (`opencv_mertens`).

To perform image quality evaluations, I implemented PSNR and SSIM calculation functions. I haven't started the actual HDR pipeline yet on the Pi yet, but just to test it, I created a table comparing the Mertens output to each of the frames processed frames:

| Frame    | PSNR (dB) | SSIM   |
| -------- | --------- | ------ |
| frame_00 | 8.18      | 0.0124 |
| frame_01 | 8.65      | 0.0858 |
| frame_02 | 10.79     | 0.3990 |
| frame_03 | 14.75     | 0.7258 |
| frame_04 | 28.44     | 0.9831 |


Eventually, I'll use this to compare the Mac implementations with the Pi implementations once I code the algorithms myself and not use OpenCV.

TL;DR
- I implemented the quantitative eval pipeline (PSNR/SSIM calculations) and qualitative analysis pipeline (RAW Bayer w/ OpenCV)
- Once I implement my own HDR algorithms, I'll run the qualitative eval pipeline on them
