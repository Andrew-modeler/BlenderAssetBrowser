# Bundled ML Models

## SigLIP2 (vision encoder, FP16)

- File: `siglip2_vision_fp16.onnx`
- Model: `siglip2-base-patch16-224`
- Source: https://huggingface.co/onnx-community/siglip2-base-patch16-224-ONNX
- License: Apache 2.0 (Google LLC; see THIRD_PARTY_NOTICES.txt at plugin root)
- Approx size: ~186 MB

## Why this model

- **Sigmoid loss** = each tag is scored independently, perfect for multi-label
  asset tagging where an asset can be "rock" AND "stone" AND "weathered"
  simultaneously without softmax forcing one winner.
- **Apache 2.0** = no redistribution restrictions for a free plugin.
- **FP16 + vision-only** = small enough to ship (~186 MB) without the text
  encoder, since the plugin pre-computes tag embeddings at build time and
  ships those as a tiny binary blob.

## Integrity

Before the first inference call, `FSigLIPInference::EnsureModel` will compare
the file's SHA256 against an expected hash (filled in once the binary is
finalized). A mismatch refuses to load — this guards against an attacker
swapping the model file on disk.
