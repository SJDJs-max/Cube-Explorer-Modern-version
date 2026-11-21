"""Real official SDK stdio client -> MCP process -> native C++ engine.

Run with the project's MCP venv Python. --server-exe exercises the PyInstaller
delivery instead of the Python source. No fake solver is used in this file.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import os
from pathlib import Path
import sys
import time

from mcp import Client, StdioServerParameters

ROOT = Path(__file__).resolve().parent.parent
SOLVED = "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB"


async def run(server_exe: Path | None, engine: Path, use_default_engine: bool = False, clean_path: bool = False) -> None:
    environment = {key: value for key, value in os.environ.items() if key in {
        "PATH", "SystemRoot", "WINDIR", "TEMP", "TMP", "PYTHONPYCACHEPREFIX", "PYTHONNOUSERSITE",
    }}
    if clean_path:
        windows = Path(environment.get("SystemRoot", r"C:\Windows"))
        environment["PATH"] = str(windows / "System32") + os.pathsep + str(windows)
    else:
        environment["PATH"] = str(ROOT / "tools/Qt/bin") + os.pathsep + str(ROOT / "tools/bin") + os.pathsep + environment.get("PATH", "")
    common = ["--workspace", str(ROOT / ("cache/mcp-release-test" if use_default_engine else "cache/mcp-exe-test" if server_exe else "cache/mcp-source-test"))]
    if not use_default_engine:
        common += ["--engine", str(engine), "--tables", str(ROOT / "tables/original")]
    else:
        assert server_exe is not None
        assert all((engine.parent / "tables/original" / name).is_file() for name in ["RawFlipSlice", "flipUDslice.move", "phase1PF.prun", "phase2PF.prun"]), "delivery must include the original lookup tables"
    parameters = StdioServerParameters(command=str(server_exe or sys.executable),
                                      args=common if server_exe else [str(ROOT / "mcp/server.py"), *common],
                                      cwd=ROOT, env=environment)
    checks = 0

    def check(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(message)

    async with Client(parameters) as client:
        tools = (await client.list_tools()).tools
        check({tool.name for tool in tools} == {"get_capabilities", "validate_cube", "apply_moves", "submit_solve", "get_job", "get_results", "cancel_job", "validate_templates", "submit_templates"}, "nine published tools")
        check(all(tool.output_schema for tool in tools), "all tools publish output schemas")
        submit_schema = next(tool for tool in tools if tool.name == "submit_solve").input_schema
        check(all(name in json.dumps(submit_schema) for name in ["modulo_u", "center_ori", "memoize_dead_ends", "quotient_free_rotations", "use_triple_search"]), "full search schema")

        async def call(name, arguments=None, *, error=False):
            reply = await client.call_tool(name, arguments or {})
            check(bool(reply.is_error) == error, f"{name}: unexpected error flag: {reply}")
            data = reply.structured_content
            check(isinstance(data, dict), f"{name}: structured output")
            check(json.loads(reply.content[0].text) == data, f"{name}: text/structured identity")
            return data if error else data["result"]

        capabilities = await call("get_capabilities")
        check(capabilities["version"] == "0.3.1", "native release version published")
        check(capabilities["formula_rotation_cost"] == 0, "native rotation cost")
        check("optimal" in capabilities["modes"], "native true optimal mode published")
        check(capabilities["formula_defaults"] == {"min_turns": 0, "max_turns": -1, "max_results": 0, "time_limit_ms": 0, "node_limit": 0},
              "formula defaults have no artificial stopping budget")
        for depth in (-1, 0, 256, 2147483647):
            unlimited_check = await call("validate_cube", {"request": {"mode": "formula", "scramble": "R", "max_turns": depth}})
            check(unlimited_check["valid"], "unlimited and large finite depths survive MCP schema")
        negative_depth = await client.call_tool("validate_cube", {"request": {"mode": "formula", "scramble": "R", "max_turns": -2}})
        check(negative_depth.is_error, "depth below unlimited sentinel rejected")
        normal = await call("validate_cube", {"request": {"mode": "two_phase", "scramble": "R U"}})
        check(normal["valid"] and normal["complete"], "normal input validation")
        invalid = await call("validate_cube", {"request": {"mode": "two_phase", "scramble": "R", "modulo_u": True}}, error=True)
        check(bool(invalid["error"]["message"]), "ordinary mode rejects formula switch")
        bad_schema = await client.call_tool("validate_cube", {"request": {"mode": "formula", "scramble": "", "unknown_option": 1}})
        check(bad_schema.is_error, "unknown option rejected, not silently discarded")

        applied = await call("apply_moves", {"request": {"mode": "formula", "scramble": ""}, "moves": "r x U2 x' r'"})
        check(applied["turns"] == 3 and applied["rotations"] == 2, "native apply separates rotations")
        replay = await call("apply_moves", {"request": {"mode": "formula", "pattern": applied["pattern"]}, "moves": "r x U2 x' r'"})
        check(replay["facelets"] == SOLVED, "native inverse/conjugate replay")

        items = [
            {"id": "ordinary", "mode": "two_phase", "scramble": "R U F2", "time_limit_ms": 3000},
            {"id": "mod-u", "mode": "formula", "scramble": "U", "max_turns": 0},
            {"id": "wide", "mode": "formula", "scramble": "r", "allowed_moves": ["r'"], "max_turns": 1, "max_results": 1,
             "memoize_dead_ends": False, "quotient_free_rotations": False},
            {"id": "generator", "mode": "formula", "direction": "generate", "scramble": "r", "allowed_moves": ["r"], "max_turns": 1, "max_results": 1},
            {"id": "rotation", "mode": "formula", "scramble": "R", "allowed_moves": ["z", "U'"], "max_turns": 1, "max_results": 1},
            {"id": "partial", "mode": "formula", "pattern": {"cp": [-1]*8, "co": [0]*8, "ep": [-1]*12, "eo": [0]*12}, "max_turns": 0},
            {"id": "initial-u", "mode": "formula", "scramble": "R U", "allowed_moves": ["U'", "R'"], "max_turns": 2, "max_results": 1},
            {"id": "optimal", "mode": "optimal", "scramble": "R U F2", "max_turns": 3},
            {"id": "optimal-generator", "mode": "optimal", "direction": "generate", "scramble": "R U", "initial_solution": "R U", "excluded_solutions": [], "max_turns": 2},
            {"id": "optimal-unproven", "mode": "optimal", "scramble": "R U", "initial_solution": "U' R' F F'", "node_limit": 1},
            {"id": "triple", "mode": "two_phase", "scramble": "R U F2 L D B'", "use_triple_search": True},
            {"id": "triple-generator", "mode": "two_phase", "direction": "generate", "scramble": "R U F2", "use_triple_search": True},
            {"id": "invalid", "mode": "two_phase", "facelets": "U"*54},
        ]
        started = time.perf_counter()
        job = await call("submit_solve", {"items": items, "parallelism": 3})
        submit_ms = (time.perf_counter() - started)*1000
        check(job["accepted"] and len(job["item_ids"]) == len(items), "batch accepted immediately")
        job_id = job["job_id"]

        async def wait_job(identifier, timeout=15):
            deadline = time.monotonic() + timeout
            while True:
                status = await call("get_job", {"job_id": identifier})
                if status["done"]:
                    return status
                if time.monotonic() > deadline:
                    raise AssertionError(f"job timeout: {status}")
                await asyncio.sleep(0.03)

        status = await wait_job(job_id)
        states = {item["item_id"]: item for item in status["items"]}
        check(states["invalid"]["state"] == "invalid", "invalid item isolated")
        for item in items[:-1]:
            check(states[item["id"]]["state"] == "completed", f"completed {item['id']}: {states}")
        cursor, rows = 0, []
        while True:
            page = await call("get_results", {"job_id": job_id, "cursor": cursor, "limit": 2})
            rows.extend(page["results"])
            check(page["next_cursor"] == cursor+len(page["results"]), "stable cursor")
            cursor = page["next_cursor"]
            if not page["has_more"]:
                break
        by_item = {row["item_id"]: row["solution"] for row in rows}
        check(set(by_item) == {item["id"] for item in items[:-1]}, "all valid item results")
        check(by_item["mod-u"]["turns"] == 0 and by_item["mod-u"]["moves"] == [], "modU zero native formula")
        check(by_item["wide"]["moves"] == ["r'"] and by_item["wide"]["turns"] == 1, "wide result not rewritten")
        check(by_item["generator"]["moves"] == ["r"], "generate direction")
        check(by_item["rotation"]["turns"] == 1 and by_item["rotation"]["rotations"] >= 1, "free rotation separate count")
        check(by_item["partial"]["turns"] == 0, "partial orientation-only pattern")
        check(by_item["initial-u"]["formula"].startswith("[U']") and by_item["initial-u"]["moves"] == ["U'", "R'"] and by_item["initial-u"]["turns"] == 2, "bracketed initial U preserves algorithm turn count")
        check(by_item["optimal"]["optimality_proven"] and by_item["optimal"]["turns"] == 3, "real optimal minimum")
        check(by_item["optimal-generator"]["optimality_proven"] and by_item["optimal-generator"]["moves"] == ["R", "U"], "optimal generation and initial upper bound")
        check(not by_item["optimal-unproven"]["optimality_proven"] and by_item["optimal-unproven"]["turns"] == 4, "limited optimal retains unproven candidate")
        check(states["optimal-unproven"]["stop_reason"] == "node_limit", "unproven candidate reports stopping reason")
        for name in ("triple", "triple-generator"):
            check(states[name]["summary"]["search_axes"] == 3 and 0 <= by_item[name]["search_axis"] < 3,
                  "actual three-axis search and winning axis reported")
        for result in by_item.values():
            check(result["verified"] is True, "native independent verification retained")
            check(result["turns"] == sum(action["turn_cost"] for action in result["actions"]), "algorithm structured costs")
        ordinary_replay = await call("apply_moves", {"request": items[0], "moves": " ".join(by_item["ordinary"]["moves"])})
        check(ordinary_replay["facelets"] == SOLVED, "ordinary exact goal (no mod U)")
        triple_replay = await call("apply_moves", {"request": {"mode": "two_phase", "scramble": ""}, "moves": by_item["triple-generator"]["formula"]})
        triple_target = await call("validate_cube", {"request": {"mode": "two_phase", "scramble": "R U F2"}})
        check(triple_replay["facelets"] == triple_target["facelets"], "three-axis generation preserves exact direction")

        concurrent = await asyncio.gather(*(call("get_results" if n%2 else "get_job", {"job_id": job_id}) for n in range(24)))
        for n, response in enumerate(concurrent):
            check(("results" if n%2 else "items") in response, "concurrent response correlation")
        await call("get_job", {"job_id": "does-not-exist"}, error=True)

        long_items = [{"id": str(n), "mode": "formula", "scramble": "R U R' U R U2 R'", "move_symbols": "URFDLBurfdlbMESxyz",
                       "min_turns": 12, "max_turns": 12, "max_results": 0, "time_limit_ms": 30000, "node_limit": 0, "heuristic": "none"} for n in range(3)]
        long_job = await call("submit_solve", {"items": long_items, "parallelism": 1})
        await call("cancel_job", {"job_id": long_job["job_id"], "item_id": "1"})
        await call("cancel_job", {"job_id": long_job["job_id"]})
        cancelled = await wait_job(long_job["job_id"])
        check(all(item["state"] == "cancelled" for item in cancelled["items"]), "running and queued cancellation")
        preserved = await call("get_results", {"job_id": job_id, "cursor": 0, "limit": 1000})
        check(preserved["results"] == rows, "other job cancellation preserves prior results")

        unlimited = await call("submit_solve", {"items": [{"mode": "formula", "scramble": "R", "move_symbols": "UR",
            "modulo_u": False, "heuristic": "none"}]})
        deadline = time.monotonic() + 5
        while True:
            active = await call("get_job", {"job_id": unlimited["job_id"]})
            if active["result_count"] and active["items"][0]["progress"].get("depth_start_count", 0) >= 2:
                break
            check(not active["done"] and time.monotonic() < deadline, "unlimited enumeration stays active and reports layers")
            await asyncio.sleep(0.01)
        first_page = await call("get_results", {"job_id": unlimited["job_id"], "limit": 1})
        check(not first_page["done"] and first_page["results"][0]["solution"]["optimality_proven"],
              "verified shortest first layer streams before enumeration completes")
        await call("cancel_job", {"job_id": unlimited["job_id"]})
        stopped = await wait_job(unlimited["job_id"])
        check(stopped["items"][0]["stop_reason"] == "cancelled", "unlimited job responds to cancellation")
        retained = await call("get_results", {"job_id": unlimited["job_id"], "limit": 1000})
        check(retained["results"][:1] == first_page["results"], "cancelled unlimited job preserves published rows")
        check(stopped["result_count"] == stopped["items"][0]["summary"]["results"], "stream-only core reports complete result count")
        check([json.loads(line) for line in Path(retained["result_file"]).read_text(encoding="utf-8").splitlines()] == retained["results"],
              "MCP pagination reads persisted JSONL results without modification")

        template_schema = next(tool for tool in tools if tool.name == "submit_templates").input_schema
        check(all(name in json.dumps(template_schema) for name in
                  ["templates", "allowed_templates", "continuous", "find_generators", "generator_options", "node_limit"]),
              "template tool exposes all original editor constraints and budgets")
        mono = [[0]*9] + [[5]*9 for _ in range(4)]
        mixed = [[0]*9, [5, 5, 5, 0, 0, 0, 0, 0, 0]] + [[5]*9 for _ in range(3)]
        template_check = await call("validate_templates", {"request": {"templates": mono}})
        check(template_check["valid"] and template_check["result_kind"] == "cube_state" and not template_check["search_started"],
              "template validation is independent from cube solving")
        await call("validate_templates", {"request": {"templates": mono, "generator_options": {}}}, error=True)
        bad_template = await client.call_tool("submit_templates", {"items": [{"templates": [[6]*9]*5}]})
        check(bad_template.is_error, "six abstract labels strictly validated")
        templates_job = await call("submit_templates", {"items": [
            {"id": "plain", "templates": mixed, "allowed_templates": [[True]*5]*6, "max_results": 3,
             "continuous": False, "node_limit": 0, "time_limit_ms": 0},
            {"id": "continuous", "templates": mono, "continuous": True, "max_results": 0},
            {"id": "empty", "templates": [[5]*9]*5},
            {"id": "generator", "templates": mixed, "max_results": 3, "find_generators": True,
             "generator_options": {"max_turns": 30, "target_turns": 20, "max_phase1_turns": 20, "time_limit_ms": 1000, "node_limit": 0, "use_triple_search": True}},
            {"id": "limited-generator", "templates": mixed, "max_results": 3, "find_generators": True,
             "generator_options": {"max_turns": 0}},
        ], "parallelism": 2})
        templates_status = await wait_job(templates_job["job_id"])
        check(all(item["state"] == "completed" for item in templates_status["items"]), "template batch completed")
        cursor, template_rows = 0, []
        while True:
            page = await call("get_results", {"job_id": templates_job["job_id"], "cursor": cursor, "limit": 2})
            template_rows.extend(page["results"])
            check(page["next_cursor"] == cursor+len(page["results"]), "template stable result pagination")
            cursor = page["next_cursor"]
            if not page["has_more"]:
                break
        matches = {}
        for row in template_rows:
            state = row["solution"]
            matches.setdefault(row["item_id"], []).append(state)
            check(state["kind"] == "cube_state" and state["verified"] and "turns" not in state and "formula" not in state,
                  "matched state is never disguised as zero-step formula")
            if "generator" in state:
                generator = state["generator"]
                replay = await call("apply_moves", {"request": {"mode": "two_phase", "scramble": ""}, "moves": generator["formula"]})
                check(replay["facelets"] == state["facelets"] == generator["terminal_facelets"], "template exact generator independently replays")
                check(generator["turns"] == sum(action["turn_cost"] for action in generator["actions"]), "template generator count comes from algorithm")
        check(len(matches["plain"]) == 3 and all("generator" not in state for state in matches["plain"]), "enumeration alone returns states")
        check(len(matches["continuous"]) == 1 and "empty" not in matches, "continuous/empty template semantics retained")
        check(any(state["generator"]["turns"] > 0 for state in matches["generator"]), "nontrivial template formula generated")
        check(all(state["generator_search"]["search_axes"] == 3 for state in matches["generator"]), "template generator options select real triple search")
        check(any("generator" not in state and not state["generator_search"]["solved"] for state in matches["limited-generator"]),
              "limited generator retains state without fake formula")
        pending = await call("submit_templates", {"items": [{"id": str(n), "templates": mixed, "max_results": 0,
            "time_limit_ms": 0, "node_limit": 0} for n in range(50)], "parallelism": 1})
        await call("cancel_job", {"job_id": pending["job_id"]})
        template_cancelled = await wait_job(pending["job_id"])
        check(any(item["state"] == "cancelled" for item in template_cancelled["items"]), "template queued cancellation uses shared async jobs")
    print(json.dumps({"status": "PASS", "checks": checks, "server": str(server_exe or ROOT / "mcp/server.py"),
                      "engine": str(engine), "default_engine_discovery": use_default_engine, "clean_path": clean_path,
                      "batch_submit_ms": round(submit_ms, 3)}, ensure_ascii=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--server-exe", type=Path)
    parser.add_argument("--engine", type=Path, default=ROOT / "build/release/src/engine/CubeExplorerEngine.exe")
    parser.add_argument("--use-default-engine", action="store_true")
    parser.add_argument("--clean-path", action="store_true")
    options = parser.parse_args()
    asyncio.run(run(options.server_exe, options.engine.resolve(), options.use_default_engine, options.clean_path))
