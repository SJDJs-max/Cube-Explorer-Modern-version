# Implementation ledger · 0.3.1

Original source remains read-only in `D:/Cube/cube explorer/raw/CubeExplorer-master`. All new code, tools, caches, logs and deliverables are under `new version`. `docs/raw-sha256.json` is the original 136-file baseline.

## Algorithms

- Ordinary Kociemba two-phase search retains 18 outer moves and exact restoration. Its tables are shared per process/path. Optional original-style UD/RL/FB triple search shares total budgets and verifies remapped solutions.
- Phase-two U/D power pruning follows the original source. Root distance recovery stops when an admissible lower bound already exceeds the remaining depth; successful candidate order and single-axis formulas remain unchanged.
- Formula mode accepts complete or partial constraints, real wide/slice turns and zero-cost whole-cube rotations. Both solving and generation support modulo-U with independently defined targets.
- U symmetry is proved at the search root; reaching a modulo-U goal ends the path immediately. Rendering only brackets an essential initial U. It never removes moves or reconstructs cost from text.
- Alternating same-letter single/wide triples are excluded during search. Commutation duplicates use a canonical reachable ordering with no growing historical-result set. Different paths to one state are not globally discarded.
- Every published formula is independently replayed in a geometric facelet model. Counts and optimality proofs come from the search. The UI no longer displays rotation statistics; internal/API metadata remains compatible.
- Formula pruning restores the original overlapping edge groups and stops unnecessary lower-bound work. Its independent geometric verifier precomputes permutations and decoding without borrowing the search move tables or skipping output verification.
- Formula defaults are unlimited: max_turns=-1, all other budgets zero. Streaming mode avoids a second full result buffer. Engine JSONL plus offsets supports pagination and cancellation without retaining full JSON records in memory.
- Exact optimal search is distinct from two-phase search; only a genuine proof grants the star.

## Interface and API

The main window preserves original sticker/control geometry, mouse modifiers, AutoFix and result-row interaction. The left pane starts collapsed. Method one expands and centers it; method two opens a smaller idle separate formula window with minimize/maximize controls. Explicit Add copies all buffered results, including repeated Add behavior. The editor ends at formula input without the extra blank strip; compact result rows align the solution and name labels. The default view is white up/green front. The 3D view uses a perspective camera and beveled cubies. The unchanged original icon is embedded in the executable and Qt resources.

The Pattern Editor GUI was removed by request. Existing template core/MCP interfaces remain for compatibility. Color customization is in Options, with a Chinese RGB/HSV picker. Defaults use the requested exact RGB values; special marks use original black-backed colored hatching. Calibri and Microsoft YaHei are used, with proportional resizing and a sun/moon theme shortcut.

Core requests, cancellation, true step counts and immutable moves are shared by GUI and MCP. MCP supports single/batch jobs, streamed progress, cancellation and paged verified results; it is not an embedded chat model.

Menu behavior, workspace transactions, hidden center-direction persistence, real statistics and original text import/export are documented in `UI-MIGRATION.md`. Source correspondence is in `ORIGINAL-UI-CONTRACT.md`.

## Validation and distribution

`tests/user_regression_tests.cpp` fixes the user's three concrete regression cases, plus streaming, unlimited cancellation and proof tests. Other tests compare geometric/cubie models and shallow pruned/unpruned formula equivalence classes. `CORE-VALIDATION.md` holds final build, portable EXE, UI, Engine and MCP evidence.

Performance measurements compare the pre-change modern implementation with this version on identical fixed inputs. No original-versus-new EXE speedup is claimed. See `twophase-migration.md` and `pattern-migration.md` for reproducible measurements and proof boundaries.

Deliverable: `dist/CubeExplorerModern-0.3.1/CubeExplorerModern.exe`, archive `dist/CubeExplorerModern-0.3.1-win64.zip`. Keep the whole portable directory. Offline help, MCP configuration, corresponding application and Qt source, licenses and hash inventories accompany it.

Camera and deep symmetry GUI are excluded as requested. Old HTTP, ordinary slice-mode and dedicated huge center tables are not presented as implemented features. Finite test coverage is not a universal correctness/performance proof; every actual output is still independently verified.
