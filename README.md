# JankenCamera: Bare-metal exposure fusion pipeline on Raspberry Pi

---

## Summary

This is a bare-metal exposure fusion pipeline on a Raspberry Pi Zero W and IMX219 camera module. I extract RAW Bayer frames through a FrankenCamera-style API, run them through a GPU-optimized image processing pipeline to get linear RGB images, and perform Mertens exposure fusion algorithm to get the final fused image. This is a project for CS348K at Stanford, so my original proposal, checkpoints, and final writeup are all in `docs/`.

---

## CS348K Docs
- [Proposal](docs/proposal.md)
- [Checkpoint 1](docs/checkpoint_1)
- [Checkpoint 2](docs/checkpoint_2)
- [Final writeup](docs/final_writeup.md)
