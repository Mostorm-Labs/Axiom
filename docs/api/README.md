# Canvas Runtime API documents

This directory contains the product Runtime API baseline:

- [Runtime C API Contract](RUNTIME_C_API_CONTRACT.md): ownership, lifecycle, ports, hot/control
  paths, versioning and validation rules.
- [Normative ABI v1 header](canvas_runtime_api_v1.h): compileable symbol, type, field and numeric
  constant manifest used as the R1 implementation input.

The header is intentionally under `docs/api` until R1 creates the product module layout. Current
POCs continue to use their explicitly Experimental interfaces. Do not include this file from POC
production targets or imply that a binary implementation has already shipped.
