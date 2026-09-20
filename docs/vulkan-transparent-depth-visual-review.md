# Transparent depth composition visual review

Both run42 synthetic indexed composition samples pass exactly against an independent ordered-remap oracle: 2047×1439 and 2048×1440, each with96 clipped quad groups and three transparent peels. The `screenshot_runner` agent manually opened all four oracle/Vulkan images. The coloured rectangles and repeating indexed background match, without visible diagonal splits, holes or leaked edges. Raw indexed and PNG bytes were independently confirmed equal.

This is **not a frozen software scene comparison**. The qualified old shaders also pass both new samples, so this regression does not reproduce or establish correction of the giant screenshot's41-pixel water defect. The actual giant corpus and fresh repeats remain the decisive correctness evidence. No pixel exception is accepted. FinalRGBA qualification is outside this synthetic indexed check.

The companion JSON pins run42, build47, the old-shader control and every sample artifact.
