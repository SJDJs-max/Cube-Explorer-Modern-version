"""Published MCP schemas. Semantic validation belongs to the native engine."""
from __future__ import annotations

from typing import Annotated, Any, Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator

Corner = Annotated[int, Field(ge=-1, le=7, strict=True)]
Edge = Annotated[int, Field(ge=-1, le=11, strict=True)]
CornerOrientation = Annotated[int, Field(ge=-1, le=2, strict=True)]
EdgeOrientation = Annotated[int, Field(ge=-1, le=1, strict=True)]
CenterOrientation = Annotated[int, Field(ge=-1, le=3, strict=True)]
CenterArray = Annotated[list[CenterOrientation], Field(min_length=6, max_length=6)]
MoveToken = Annotated[str, Field(pattern=r"^[URFDLBurfdlbMESxyz](2|')?$")]
Nonnegative = Annotated[int, Field(ge=0, strict=True)]


class CubePattern(BaseModel):
    """Original cubie order; -1 leaves a position/orientation unconstrained.

    Corners: URF,UFL,ULB,UBR,DFR,DLF,DBL,DRB.
    Edges: UR,UF,UL,UB,DR,DF,DL,DB,FR,FL,BL,BR.
    Center orientation order: U,R,F,D,L,B. Frame is the native orientation ID.
    """
    model_config = ConfigDict(extra="forbid")
    cp: Annotated[list[Corner], Field(min_length=8, max_length=8)]
    co: Annotated[list[CornerOrientation], Field(min_length=8, max_length=8)]
    ep: Annotated[list[Edge], Field(min_length=12, max_length=12)]
    eo: Annotated[list[EdgeOrientation], Field(min_length=12, max_length=12)]
    frame: Annotated[int, Field(ge=0, le=23, strict=True)] | None = None
    center_ori: CenterArray | None = None


class SearchSpec(BaseModel):
    """One independent native solve/generate request.

    Exactly one of scramble, facelets or pattern is required. An empty scramble
    is a solved cube. Facelets use URFDLB face order (54 symbols); '?' expresses
    an unknown sticker in formula mode. Rotations cost zero turns and are counted
    separately. Explicit options are forwarded unchanged; unspecified defaults
    are selected and reported by the native engine.
    """
    model_config = ConfigDict(extra="forbid")
    id: str | None = Field(default=None, description="Caller-provided item identifier within the batch.")
    mode: Literal["two_phase", "optimal", "formula"]
    direction: Literal["solve", "generate"] = "solve"
    scramble: str | None = None
    facelets: str | None = None
    pattern: CubePattern | None = None
    center_ori: CenterArray | None = None
    move_symbols: str | None = Field(default=None, pattern=r"^[URFDLBurfdlbMESxyz]*$", description="Each symbol enables quarter, half and inverse turns. Formula mode only.")
    allowed_moves: list[MoveToken] | None = Field(default=None, description="Exact enabled move tokens. Formula mode only; alternative to move_symbols.")
    min_turns: Nonnegative | None = None
    max_turns: Annotated[int, Field(ge=-1, le=2147483647, strict=True)] | None = Field(
        default=None, description="Formula mode: -1 (default) keeps increasing depth until cancelled; 0 means zero turns. Ordinary modes retain their own nonnegative limits.")
    target_turns: Nonnegative | None = None
    max_phase1_turns: Nonnegative | None = None
    max_results: Nonnegative | None = Field(default=None, description="Formula result limit; default 0 means unlimited.")
    time_limit_ms: Nonnegative | None = Field(default=None, description="Native search time budget; formula mode includes PDB preparation, ordinary modes exclude shared-table loading. 0 means unlimited.")
    node_limit: Nonnegative | None = Field(default=None, description="Native node budget; 0 means unlimited.")
    modulo_u: bool | None = Field(default=None, description="Formula mode: accept goals differing only by U/U2/U'.")
    remove_u_symmetric_prefix: bool | None = None
    reject_alternating_triples: bool | None = None
    deduplicate_commuting: bool | None = None
    heuristic: Literal["none", "standard", "strong"] | None = None
    memoize_dead_ends: bool | None = Field(default=None, description="Formula mode: reuse only fully searched subtrees with no goal; preserves all successful formula prefixes.")
    quotient_free_rotations: bool | None = Field(default=None, description="Formula mode: collapse free-rotation contexts only when the native action-profile proof succeeds; false disables this optimization.")
    initial_solution: str | None = Field(default=None, description="Optimal mode: verified upper-bound candidate, expressed in this request's solve/generate direction. It may survive cancellation without an optimality proof.")
    excluded_solutions: Annotated[list[str], Field(max_length=10000)] | None = Field(default=None, description="Optimal mode: already returned formulas, in this request's solve/generate direction; search for another formula. Exclusion never proves optimality.")
    use_triple_search: bool | None = Field(default=None, description="Two-phase mode only: original optional UD/RL/FB search with shared total budgets and serialized verified results; default false.")

    @model_validator(mode="after")
    def check_input(self) -> SearchSpec:
        if sum(value is not None for value in (self.scramble, self.facelets, self.pattern)) != 1:
            raise ValueError("Specify exactly one of scramble, facelets or pattern.")
        if self.move_symbols is not None and self.allowed_moves is not None:
            raise ValueError("Specify move_symbols or allowed_moves, not both.")
        return self

    def payload(self) -> dict[str, Any]:
        return self.model_dump(mode="json", exclude_unset=True, exclude_none=True)


TemplateLabel = Annotated[int, Field(ge=0, le=5, strict=True)]
FaceTemplate = Annotated[list[TemplateLabel], Field(min_length=9, max_length=9)]
TemplateAllowed = Annotated[bool, Field(strict=True)]
TemplateFaceChoices = Annotated[list[TemplateAllowed], Field(min_length=5, max_length=5)]


class GeneratorOptions(BaseModel):
    """Independent ordinary two-phase budget for each matched complete state."""
    model_config = ConfigDict(extra="forbid")
    max_turns: Annotated[int, Field(ge=0, le=60, strict=True)] | None = None
    target_turns: Annotated[int, Field(ge=0, le=60, strict=True)] | None = None
    max_phase1_turns: Annotated[int, Field(ge=0, le=30, strict=True)] | None = None
    time_limit_ms: Annotated[int, Field(ge=0, le=86400000, strict=True)] | None = None
    node_limit: Annotated[int, Field(ge=0, le=9007199254740991, strict=True)] | None = None
    use_triple_search: bool | None = None


class TemplateSearchSpec(BaseModel):
    """Enumerate complete legal cubes matching five abstract face templates.

    This produces cube states, not zero-turn solutions. Labels 0..5 express
    equality classes, not literal cube colors. All-gray (all 5) templates are
    unused; gray in a mixed template is the sixth class. Each face matches one
    allowed template under any planar rotation/reflection. The six face rows
    in allowed_templates use URFDLB order. Global isomorphism is not filtered.
    """
    model_config = ConfigDict(extra="forbid")
    id: str | None = None
    mode: Literal["template"] = "template"
    templates: Annotated[list[FaceTemplate], Field(min_length=5, max_length=5)]
    allowed_templates: Annotated[list[TemplateFaceChoices], Field(min_length=6, max_length=6)] | None = Field(
        default=None, description="URFDLB by five templates boolean availability matrix; omitted means all enabled.")
    continuous: bool | None = Field(default=None, description="Apply the original IsContinuous relation across adjacent faces.")
    max_results: Annotated[int, Field(ge=0, le=1000000, strict=True)] | None = Field(
        default=None, description="Matched state limit, default 100; 0 unlimited.")
    time_limit_ms: Annotated[int, Field(ge=0, le=86400000, strict=True)] | None = Field(
        default=None, description="Enumeration plus generator search budget, default 30000; shared table loading excluded. 0 unlimited.")
    node_limit: Annotated[int, Field(ge=0, le=9007199254740991, strict=True)] | None = Field(
        default=None, description="Enumeration nodes only, default 20000000; 0 unlimited.")
    find_generators: bool | None = Field(default=None, description="Also run actual exact two-phase generation for each matched state; default false.")
    generator_options: GeneratorOptions | None = Field(
        default=None, description="Requires find_generators=true. Separate per-state generator budget; overall remaining time also caps it.")

    def payload(self) -> dict[str, Any]:
        return {**self.model_dump(mode="json", exclude_unset=True, exclude_none=True), "mode": "template"}


class EngineError(BaseModel):
    model_config = ConfigDict(extra="allow")
    code: str
    message: str


class EngineResponse(BaseModel):
    """Native response envelope, including original algorithm result fields."""
    model_config = ConfigDict(extra="allow")
    id: str | None = None
    ok: bool
    result: dict[str, Any] | None = None
    error: EngineError | None = None
