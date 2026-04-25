# JankenCamera

**Justin Wu** (justinyw)

---

## Summary

I plan on building a bare-metal HDR pipeline on a Raspberry Pi Zero W with a FrankenCamera-style API, interfacing directly with camera modules (e.g. OV7670 or OV5647) to extract RAW frames and precisely control sensor parameters like exposure and gain. To evaluate success, I plan on implementing foundational HDR image pipelines like Mertens exposure fusion or Debevec’s method (and maybe some more complex ones, if time allows). My primary contribution is adapting these methods to a resource-constrained system, documenting optimizations and design choices and evaluating against a theoretical ceiling to show what level of HDR quality and performance I can achieve.

---

## Problem Definition

### Inputs
- camera module
- FrankenCamera-style frame specification
- image processing algorithm

### Outputs
- processed image according to the given image processing algorithm

---

## Task List

- Implement driver for Raspberry Pi camera module
  - First, be able to read raw Bayer inputs
  - Then, support precise control of camera parameters (exposure, gain, etc.)
- Implement FrankenCamera-like API over drivers
  - Basic support for Frames/Shots
  - API documented here: https://graphics.stanford.edu/papers/fcam/html/
- Implement simple HDR pipelines with API
  - Mertens exposure fusion, Debevec’s method
  - The actual image processing algorithms will probably have to run on the Raspberry Pi GPU, which will require writing VC4 assembly
- Implement more complex pipelines (nice to have)
  - Will have to do some more research on feasibile pipelines, given no flash or IMU in current setup

---

## Deliverables/Evaluation
- Deliverable
    - Real-time demonstration of camera taking picture + HDR algorithms applied
- Metrics
    - Fidelity: compare outputs against reference implementations on the same RAW outputs, e.g. OpenCV, using PSNR/SSIM
    - Performance: measure runtime, memory usage, and throughput
- Design documentation
    - Document key implementation decisions + evaluate alternatives based off of previous metrics

---

## Risks & Mitigation

- Compute constraints: limited compute/memory bandwidth may make full HDR pipelines infeasible without simplifications
    - Identify bottlenecks and evaluate which parts of the pipeline can be optimized, documenting tradeoffs
- Camera constraints: the camera isn’t the best, so I’m unsure how precise the timing control is and how good the image quality will be
    - Not really much I can do about this except try to find the range where I'm reasonably confident about control and perform experiments in that range
- Driver complexity: some drivers don’t have officially released datasheets (but do have open-source Linux implementations)
    - Start with simpler camera that we know definitely works (OV7670), only try stripping down the Linux implementation for OV5647 if time allows

---

## What do I need help with?
- Advice on selecting appropriate HDR algorithms