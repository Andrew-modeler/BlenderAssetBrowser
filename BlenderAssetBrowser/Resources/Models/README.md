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

Before the first inference call, `FSigLIPInference::EnsureRuntime` compares
the file's **SHA-1** against a hash baked into the source at
`SigLIPInference.cpp` (`kExpectedHash`). A mismatch refuses to load — this
guards against on-disk corruption or accidental model swap.

SHA-1 (not SHA-256) is intentional: the threat model here is integrity /
tamper *detection*, not cryptographic non-repudiation. A 160-bit hash gives
~80 bits of practical collision resistance against accidental corruption,
which is plenty for a sanity check on a 186 MB file. For audit / supply-chain
verification you should additionally check the upstream HuggingFace
`siglip2_vision_fp16.onnx` digest yourself.
