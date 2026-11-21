# Project constraints

All new files, downloads, tools, caches, logs, temporary files, build outputs and deliverables MUST stay inside this directory. Set process-local TEMP/TMP and package caches here when invoking build/download tools. Never edit `../raw` or read/reuse `../失败产物`.

Original reference: `../raw/CubeExplorer-master` (Cube Explorer 5.15, GPL-3.0). `docs/raw-sha256.json` records the original bytes. Preserve attribution and original license in the distribution.

Implement and verify the native core before integrating the GUI or MCP. The ordinary two-phase solver retains its existing semantics. New wide-turn, modulo-U, symmetry and formula-enumeration rules apply only to formula solving.

Formula mode: face/slice/wide tokens cost one (including half turns); rotations cost zero and have a separate token count. Modulo U is anchored to the input logical U face. All results carry immutable move arrays, search-derived costs, terminal state and validation. Display formatting may bracket an initial U but MUST NOT remove/rewrite moves or recalculate costs from text.

Use a geometric facelet implementation independent of the cubie search tables to replay results. Do not use global visited-state pruning to eliminate different enumerated formula paths. Caches must identify move set, metric, target policy and data format.

Public C++ model API is owned by the model agent; solver agents coordinate changes. Build/test scripts and integration belong to root. Do not overwrite another agent's files.
