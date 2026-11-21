"""Cube Explorer MCP stdio entry point; no cube algorithms live in Python."""
from __future__ import annotations

import argparse
from contextlib import asynccontextmanager
import json
import logging
from pathlib import Path
import sys
from typing import Annotated, Any

from mcp.server import MCPServer
from mcp.types import CallToolResult, TextContent, ToolAnnotations
from pydantic import Field

from engine_bridge import EngineBridge, EngineUnavailable
from schemas import EngineResponse, SearchSpec, TemplateSearchSpec

ToolResult = Annotated[CallToolResult, EngineResponse]


def create_server(bridge: EngineBridge) -> MCPServer:
    @asynccontextmanager
    async def lifespan(_: MCPServer):
        try:
            yield {}
        finally:
            await bridge.close()

    server = MCPServer(
        "Cube Explorer", version="0.3.1", lifespan=lifespan, log_level="WARNING",
        instructions=(
            "All formulas and turn counts are produced and independently verified by the local C++ engine. "
            "First call get_capabilities. Submit batches with submit_solve; it returns a job immediately. "
            "Poll get_job and get_results without waiting inside a tool. Use cancel_job to stop work. "
            "two_phase exactly solves complete ordinary cubes; optimal runs true three-axis IDA* and "
            "reports optimality_proven separately from solved, including retained unproven candidates. "
            "formula supports incomplete patterns, "
            "wide/slice moves, zero-cost rotations and modulo-U goals. Rotations are reported separately. "
            "Formula requests default to unlimited depth/results/time/nodes; cancel explicitly when enough results arrive. "
            "Set max_turns=-1 for unlimited depth and 0 for zero-turn only. Two-phase optionally supports use_triple_search. "
            "submit_templates enumerates legal matching cube states from five abstract face templates; "
            "these are kind=cube_state, not zero-turn formulas. Optional generators are real two-phase results. "
            "Native response envelopes and result payloads are preserved. Never count turns from display text."
        ),
    )
    readonly = ToolAnnotations(read_only_hint=True, destructive_hint=False, idempotent_hint=True, open_world_hint=False)
    mutate = ToolAnnotations(read_only_hint=False, destructive_hint=False, idempotent_hint=False, open_world_hint=False)

    async def call(op: str, **payload: Any) -> CallToolResult:
        try:
            response = await bridge.request(op, **payload)
        except EngineUnavailable as exc:
            response = {"ok": False, "error": {"code": "engine_unavailable", "message": str(exc)}}
        return CallToolResult(
            content=[TextContent(type="text", text=json.dumps(response, ensure_ascii=False, separators=(",", ":")))],
            structured_content=response, is_error=not response["ok"],
        )

    @server.tool(annotations=readonly)
    async def get_capabilities() -> ToolResult:
        """Get native limits, defaults, supported modes, move symbols and verification capabilities."""
        return await call("capabilities")

    @server.tool(annotations=readonly)
    async def validate_cube(request: SearchSpec) -> ToolResult:
        """Validate cube legality, incomplete constraints and mode options without starting search."""
        return await call("validate", request=request.payload())

    @server.tool(annotations=readonly)
    async def apply_moves(request: SearchSpec, moves: str) -> ToolResult:
        """Apply a formula using native cube transformations and return the resulting cube/pattern.

        Lowercase letters are wide turns; M/E/S are middle slices; x/y/z rotate
        the entire cube. This is simulation, not search or formula rewriting.
        """
        return await call("apply", request=request.payload(), moves=moves)

    @server.tool(annotations=mutate)
    async def submit_solve(
        items: Annotated[list[SearchSpec], Field(min_length=1, max_length=10000)],
        parallelism: Annotated[int, Field(ge=1, le=16, strict=True)] = 1,
    ) -> ToolResult:
        """Start independent solve/generate items; return a job ID immediately.

        Native engine enforces its concurrency and batch limits. Limits or
        cancellation may leave already verified results, which remain retrievable.
        This tool never waits for the searches to finish.
        """
        return await call("submit", items=[item.payload() for item in items], parallelism=parallelism)

    @server.tool(annotations=readonly)
    async def validate_templates(request: TemplateSearchSpec) -> ToolResult:
        """Validate template shapes, labels, face availability and budgets without enumerating.

        Valid constraints may have no matching cube. Empty/all-gray templates
        and a face with no available template are valid zero-result searches.
        """
        return await call("validate", request=request.payload())

    @server.tool(annotations=mutate)
    async def submit_templates(
        items: Annotated[list[TemplateSearchSpec], Field(min_length=1, max_length=10000)],
        parallelism: Annotated[int, Field(ge=1, le=16, strict=True)] = 1,
    ) -> ToolResult:
        """Start template enumeration batches and return a job ID immediately.

        Use get_job/get_results/cancel_job as for solve batches. Each result's
        solution has kind=cube_state, verified pattern and facelets. Optional
        find_generators adds a nested generator only when a real exact formula
        is found; failed or cancelled generation does not discard the cube.
        """
        return await call("submit", items=[item.payload() for item in items], parallelism=parallelism)

    @server.tool(annotations=readonly)
    async def get_job(job_id: str) -> ToolResult:
        """Read current batch/item states and native statistics immediately; never wait for completion."""
        return await call("status", job_id=job_id)

    @server.tool(annotations=readonly)
    async def get_results(
        job_id: str,
        cursor: Annotated[int, Field(ge=0, strict=True)] = 0,
        limit: Annotated[int, Field(ge=1, le=1000, strict=True)] = 100,
    ) -> ToolResult:
        """Fetch an available result page immediately, preserving native formulas/counts/verification.

        Continue with the returned next_cursor. An empty page while a job is
        running is not completion; inspect the returned state or get_job.
        """
        return await call("results", job_id=job_id, cursor=cursor, limit=limit)

    @server.tool(annotations=ToolAnnotations(read_only_hint=False, destructive_hint=False, idempotent_hint=True, open_world_hint=False))
    async def cancel_job(job_id: str, item_id: str | None = None) -> ToolResult:
        """Request native cancellation of one item or the whole batch; retain completed results."""
        payload: dict[str, Any] = {"job_id": job_id}
        if item_id is not None:
            payload["item_id"] = item_id
        return await call("cancel", **payload)

    return server


def main() -> None:
    parser = argparse.ArgumentParser(description="Cube Explorer MCP server (stdio JSON-RPC).")
    parser.add_argument("--engine", type=Path, help="Native CubeExplorerEngine.exe path; defaults to executable sibling.")
    parser.add_argument("--tables", type=Path, help="Native two-phase table directory, forwarded to the engine.")
    parser.add_argument("--workspace", type=Path, help="Native job/result data directory; defaults inside the application folder.")
    parser.add_argument("--request-timeout", type=float, default=30.0, help="Transport response deadline in seconds, not a search time limit.")
    args = parser.parse_args()
    if args.request_timeout <= 0:
        parser.error("--request-timeout must be positive")
    frozen = getattr(sys, "frozen", False)
    base = Path(sys.executable).resolve().parent if frozen else Path(__file__).resolve().parent.parent
    if frozen:
        sibling = base / "CubeExplorerEngine.exe"
        default_engine = sibling if sibling.is_file() else base.parent / "CubeExplorerEngine.exe"
    else:
        default_engine = base / "build" / "release" / "src" / "engine" / "CubeExplorerEngine.exe"
    engine = (args.engine or default_engine).resolve()
    workspace = (args.workspace or (engine.parent / "data" if frozen else base / "cache/mcp-engine")).resolve()
    command = [str(engine), "--workspace", str(workspace)]
    if args.tables is not None:
        command += ["--tables", str(args.tables.resolve())]
    logging.basicConfig(level=logging.WARNING, stream=sys.stderr)
    bridge = EngineBridge(command, cwd=engine.parent, request_timeout=args.request_timeout)
    create_server(bridge).run(transport="stdio")


if __name__ == "__main__":
    main()
