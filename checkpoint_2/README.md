# Checkpoint 2: Progress

## Updates

### Pipeline design
I restructured my pipeline to fit Mertens exposure fusion more accurately:
1. Get RAW Bayers from camera
2. Black/white level subtraction
3. White balance (gray world)
4. Demosaic
5. Mertens exposure fusion
6. Tone map (not implemented yet)

### Implementations
I made two versions of the pipeline:
- Mac baseline (`../baseline/`): C++ version of the pipeline on my Mac, not optimized for speed but uses full precision floats
- Naive Pi (`../src/image.*`, `../src/mertens.*`): ported the Mac baseline to C on the Raspberry Pi

### Evaluations
I stuck with the same Bayer frames as checkpoint 1 to eval, but only used the middle three exposures (`bayer_frames/`).

#### Qualitative results
I saved the images output from both platforms:
- `output_[0-2].png` - post-demosaic images
- `output_mertens.png` - fused images
- Mac outputs in `mac_outputs/`, Pi outputs in `naive_pi_outputs/`

#### Quantitative Results
I ran PSNR and SSIM evals between the Mac and Pi outputs:
| Frame   | PSNR (dB) | SSIM  |
|---------|------------|--------|
| Frame 0 | 60.27      | 0.9991 |
| Frame 1 | 56.86      | 0.9994 |
| Frame 2 | 57.89      | 0.9995 |
| Mertens | 53.65      | 0.9983 |

I also profiled runtime for each section:
| Platform | Black-level subtraction (ms) | White balance (ms) | Demosaic (ms) | Mertens Exposure Fusion (ms) |
|----------|-------------------------------|--------------------|----------------|-------------------------------|
| Mac      | 0.059                         | 1.689              | 1.455          | 338.09                        |
| Naive    | 48.573                        | 211.21             | 120.379        | 18423.113                     |

As you can see, the naive implementation is far slower, but matches the Mac baseline nearly perfectly in terms of quality. When I decomposed the Mertens pipeline, the far majority of the time was computing Laplacian and Gaussian weight pyramids, which took approximately 13 seconds:

| Step                         | Time (ms) |
|------------------------------|-----------|
| Normalize image to [0, 1]    | 176.345   |
| Compute weight maps          | 1969.745  |
| Normalize weight maps        | 93.595    |
| Build pyramids               | 13074.063 |
| Blend pyramids               | 1084.126  |
| Collapse pyramids            | 1905.747  |
| Scale back to original depth | 100.407   |

The bottleneck is probably due to the large number of Gaussian convolutions (sliding a 5x5 kernel along the entire image - I optimized it by composing two 5x1 convolutions but it's still pretty slow, might have to optimize with a GPU kernel).

## Next steps
I plan on applying some of these optimizations:
- Fusing steps in the Mertens pipeline (e.g. multiply-add)
- Rewriting some of the core algorithms as VideoCore GPU kernels (e.g. Gaussian convolution)
- Faster memory allocator (arena allocator on top of pre-allocated kernel memory?)
- Overclocking the Pi (ARM clock currently at 700MHz, can go up to 1GHz)

I'll also take more example photos to evaluate on, such as indoors photos with a window to the daylight outside, to capture a larger set of example images in different lightings. When I'm happy with my test suite, I'll run them on the OpenCV baseline and my own implementation, and conduct the preference survey.
