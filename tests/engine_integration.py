"""End-to-end native engine checks; all subprocesses and files stay in the workspace."""
from __future__ import annotations
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/release/src/engine/CubeExplorerEngine.exe"
WORK = ROOT / "cache/engine-integration"
WORK.mkdir(parents=True, exist_ok=True)
checks = 0

def require(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)

class Client:
    def __init__(self):
        self.next_id = 0
        self.queue = queue.Queue()
        self.err = (ROOT / "logs/engine-integration-stderr.txt").open("w", encoding="utf-8")
        self.process = subprocess.Popen(
            [str(EXE), "--workspace", str(WORK), "--tables", str(EXE.parent / "tables/original" if (EXE.parent / "tables/original").exists() else ROOT / "tables/original")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.err, text=True, encoding="utf-8",
            cwd=ROOT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        try:
            for line in self.process.stdout:
                self.queue.put(json.loads(line))
        except Exception as error:
            self.queue.put(error)
        finally:
            self.queue.put(RuntimeError("engine stdout closed"))

    def call(self, op, expect_error=False, **arguments):
        self.next_id += 1
        request = {"id": self.next_id, "op": op, **arguments}
        self.process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
        self.process.stdin.flush()
        response = self.queue.get(timeout=30)
        if isinstance(response, Exception):
            raise response
        require(response["id"] == self.next_id, "response correlation failed")
        require(response["ok"] != expect_error, f"unexpected response: {response}")
        return response.get("error") if expect_error else response["result"]

    def wait(self, job_id, seconds=30):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            status = self.call("status", job_id=job_id)
            if status["done"]:
                return status
            time.sleep(0.02)
        raise AssertionError(f"job did not finish: {status}")

    def close(self):
        try:
            if self.process.poll() is None:
                self.call("shutdown")
                self.process.wait(timeout=10)
        finally:
            if self.process.poll() is None:
                self.process.kill()
            self.err.close()

client = Client()
try:
    capabilities = client.call("capabilities")
    require(capabilities["version"] == "0.3.1", "engine release version mismatch")
    require(capabilities["formula_rotation_cost"] == 0, "wrong rotation cost")
    require(set(capabilities["modes"]) == {"two_phase", "optimal", "formula", "template"}, "mode capability mismatch")
    require(capabilities["formula_defaults"] == {"min_turns": 0, "max_turns": -1, "max_results": 0, "time_limit_ms": 0, "node_limit": 0},
            "formula defaults must not impose hidden limits")
    require(capabilities["formula_unlimited_depth"] == -1 and capabilities["formula_max_turns"] > 40, "formula depth capability retained old limit")
    client.call("validate", request={"mode": "two_phase", "scramble": "R U"})
    client.call("validate", expect_error=True, request={"mode": "two_phase", "scramble": "R", "modulo_u": True})
    client.call("validate", expect_error=True, request={"mode": "formula", "scramble": "R", "time_limit_ms": -1})
    client.call("validate", expect_error=True, request={"mode": "formula", "scramble": "", "typo_parameter": True})
    applied = client.call("apply", request={"scramble": ""}, moves="r U x")
    require(applied["turns"] == 2 and applied["rotations"] == 1, "apply counters wrong")
    checked = client.call("validate", request={"mode": "formula", "pattern": applied["pattern"]})
    require(checked["facelets"] == applied["facelets"], "structured pattern round trip failed")
    client.call("validate", expect_error=True, request={"mode": "formula", "pattern": {**applied["pattern"], "center_orii": [0] * 6}})
    client.call("validate", expect_error=True, request={"mode": "two_phase", "scramble": "", "quotient_free_rotations": False})
    client.call("validate", expect_error=True, request={"mode": "two_phase", "scramble": "", "initial_solution": ""})
    client.call("validate", expect_error=True, request={"mode": "optimal", "scramble": "R", "modulo_u": True})
    client.call("validate", expect_error=True, request={"mode": "optimal", "scramble": "R", "target_turns": 20})
    client.call("validate", expect_error=True, request={"mode": "optimal", "scramble": "R", "initial_solution": "R"})
    client.call("validate", expect_error=True, request={"mode": "optimal", "scramble": "R", "excluded_solutions": ["r'"]})
    client.call("validate", request={"mode": "formula", "scramble": "R", "min_turns": 41, "max_turns": -1})
    client.call("validate", request={"mode": "formula", "scramble": "R", "max_turns": 256})
    client.call("validate", expect_error=True, request={"mode": "formula", "scramble": "R", "max_turns": -2})
    client.call("validate", expect_error=True, request={"mode": "formula", "scramble": "R", "use_triple_search": True})
    client.call("validate", expect_error=True, request={"mode": "optimal", "scramble": "R", "use_triple_search": True})

    batch = [
        {"id": "ordinary-u", "mode": "two_phase", "scramble": "U"},
        {"id": "formula-u", "mode": "formula", "scramble": "U", "max_turns": 2},
        {"id": "wide", "mode": "formula", "scramble": "r U", "move_symbols": "URr", "max_turns": 3, "max_results": 20},
        {"id": "generator", "mode": "formula", "direction": "generate", "scramble": "U R", "move_symbols": "UR", "max_turns": 2},
        {"id": "exact-generator", "mode": "two_phase", "direction": "generate", "scramble": "R U F2"},
        {"id": "invalid", "mode": "two_phase", "scramble": "R rubbish"},
        {"id": "invalid-centers", "mode": "two_phase", "scramble": "R", "center_ori": [0] * 6},
        {"id": "node-limit", "mode": "formula", "scramble": "R F U L", "max_turns": 10, "node_limit": 1, "heuristic": "none"},
        {"id": "optimal", "mode": "optimal", "scramble": "R U F2", "max_turns": 3},
        {"id": "optimal-generator", "mode": "optimal", "direction": "generate", "scramble": "R U R' U'", "max_turns": 4},
        {"id": "optimal-candidate", "mode": "optimal", "scramble": "R U", "initial_solution": "U' R' F F'", "node_limit": 1},
        {"id": "optimal-gen-candidate", "mode": "optimal", "direction": "generate", "scramble": "R U", "initial_solution": "F F' R U", "node_limit": 1},
        {"id": "triple", "mode": "two_phase", "scramble": "R U F2 L D B'", "use_triple_search": True},
        {"id": "triple-generator", "mode": "two_phase", "direction": "generate", "scramble": "R U F2", "use_triple_search": True},
        {"id": "triple-node-limit", "mode": "two_phase", "scramble": "R U F2 L D B'", "use_triple_search": True, "node_limit": 1},
    ]
    submitted = client.call("submit", items=batch, parallelism=3)
    job = submitted["job_id"]
    status = client.wait(job)
    states = {item["item_id"]: item for item in status["items"]}
    require(states["invalid"]["state"] == "invalid", "invalid batch item accepted")
    require(states["invalid-centers"]["state"] == "invalid", "center constraints silently ignored")
    require(states["node-limit"]["stop_reason"] == "node_limit", "node limit misreported")
    rows = []
    cursor = 0
    while True:
        page = client.call("results", job_id=job, cursor=cursor, limit=2)
        rows.extend(page["results"])
        require(page["next_cursor"] == cursor + len(page["results"]), "unstable pagination")
        cursor = page["next_cursor"]
        if not page["has_more"]:
            break
    require([r["sequence"] for r in rows] == list(range(len(rows))), "pagination duplicated or skipped a row")
    require(len(rows) == status["result_count"], "result count disagrees with pagination")
    by_item = {}
    for row in rows:
        solution = row["solution"]
        by_item.setdefault(row["item_id"], []).append(solution)
        require(solution["verified"], "unverified formula published")
        require(solution["turns"] + solution["rotations"] == len(solution["actions"]), "structured counter mismatch")
        require(len(solution["moves"]) == len(solution["actions"]), "action/token mismatch")
        for action in solution["actions"]:
            require(action["turn_cost"] == (0 if action["symbol"] in "xyz" else 1), "wrong per-action cost")
    require(by_item["ordinary-u"][0]["turns"] == 1, "ordinary U became zero")
    require(by_item["formula-u"][0]["turns"] == 0, "formula U is not zero")
    require(all(s["turns"] == 2 for s in by_item["generator"]), "generation target direction is wrong")
    require(bool(by_item["wide"]), "wide request returned no results")
    require(states["triple"]["summary"]["search_axes"] == states["triple-generator"]["summary"]["search_axes"] == 3,
            "two-phase triple flag did not select three actual search axes")
    require(states["triple-node-limit"]["stop_reason"] == "node_limit" and states["triple-node-limit"]["summary"]["nodes"] == 1,
            "three-axis API multiplied the shared node budget")
    for result in by_item["triple"] + by_item["triple-generator"]:
        require(0 <= result["search_axis"] < 3, "winning axis diagnostic missing")
    generated = client.call("apply", request={"scramble": ""}, moves=by_item["triple-generator"][-1]["formula"])
    generated_target = client.call("validate", request={"mode": "two_phase", "scramble": "R U F2"})
    require(generated["facelets"] == generated_target["facelets"], "triple generator did not produce exact target")
    require(by_item["optimal"][0]["optimality_proven"] and by_item["optimal"][0]["turns"] == 3, "true optimal proof missing")
    require(by_item["optimal-generator"][0]["optimality_proven"] and by_item["optimal-generator"][0]["turns"] == 4, "optimal generator failed")
    for name in ("optimal-candidate", "optimal-gen-candidate"):
        require(not by_item[name][0]["optimality_proven"] and by_item[name][0]["turns"] == 4, "budget candidate falsely marked optimal")
        require(states[name]["stop_reason"] == "node_limit" and not states[name]["summary"]["optimality_proven"], "candidate stop/proof summary mismatch")
    persisted = [json.loads(line) for line in Path(page["result_file"]).read_text(encoding="utf-8").splitlines()]
    require(persisted == rows, "persisted results disagree with returned rows")
    client.call("results", expect_error=True, job_id=job, cursor=len(rows) + 1)
    client.call("cancel", job_id=job)
    require(client.call("status", job_id=job)["done"], "cancelling finished job changed completion")
    previous = by_item["optimal-generator"][0]["formula"]
    next_job = client.call("submit", items=[{"mode": "optimal", "direction": "generate", "scramble": "R U R' U'",
                                           "max_turns": 10, "excluded_solutions": [previous]}])["job_id"]
    require(client.wait(next_job)["items"][0]["state"] == "completed", "next optimal generator did not complete")
    next_solution = client.call("results", job_id=next_job)["results"][0]["solution"]
    require(next_solution["formula"] != previous, "generate-direction excluded formula was repeated")
    require(next_solution["optimality_proven"] == (next_solution["turns"] == 4), "next generator false optimal marker")
    next_replay = client.call("apply", request={"scramble": ""}, moves=next_solution["formula"])
    target = client.call("validate", request={"mode": "optimal", "scramble": "R U R' U'"})
    require(next_replay["facelets"] == target["facelets"], "next optimal generator did not generate its exact target")
    hard_word = "R U2 F L2 D B' R2 F' D2 L U B2 R' F2 U' L2 B D R2 F"
    inverse_word = " ".join(token if token.endswith("2") else token[:-1] if token.endswith("'") else token + "'"
                            for token in reversed(hard_word.split()))
    opt_cancel = client.call("submit", items=[{"mode": "optimal", "scramble": hard_word,
                                              "initial_solution": inverse_word, "time_limit_ms": 0}])["job_id"]
    deadline = time.monotonic() + 5
    while True:
        active = client.call("status", job_id=opt_cancel)
        if "depth" in active["items"][0]["progress"]:
            break
        require(not active["done"] and time.monotonic() < deadline, "optimal search did not report active depth")
        time.sleep(0.005)
    client.call("cancel", job_id=opt_cancel)
    ended = client.wait(opt_cancel)
    require(ended["items"][0]["stop_reason"] == "cancelled", "running optimal search did not cancel")
    retained = client.call("results", job_id=opt_cancel)["results"]
    require(len(retained) == 1 and not retained[0]["solution"]["optimality_proven"], "cancelled optimal lost candidate or falsely proved optimality")

    hard = {"mode": "formula", "scramble": "R U R' U R U2 R' F R U R' U' F'",
            "move_symbols": "URFDLBurfdlbMESxyz", "max_turns": 30, "max_results": 0,
            "node_limit": 0, "time_limit_ms": 0}
    cancelled = client.call("submit", items=[{"id": "active", **hard}, {"id": "queued", **hard}], parallelism=1)["job_id"]
    client.call("cancel", job_id=cancelled)
    stopped = client.wait(cancelled, 10)
    require(all(i["state"] == "cancelled" for i in stopped["items"]), "cancel did not stop queued and active items")
    second = client.call("submit", items=[{"mode": "two_phase", "scramble": "F2"}])["job_id"]
    require(client.wait(second)["items"][0]["state"] == "completed", "engine could not restart after cancel")

    # Omit all four limits: the search must stream a verified first result and
    # keep enumerating deeper layers until explicitly cancelled.
    unlimited = client.call("submit", items=[{"mode": "formula", "scramble": "R", "move_symbols": "UR",
        "modulo_u": False, "heuristic": "none"}], parallelism=1)["job_id"]
    deadline = time.monotonic() + 5
    while True:
        active = client.call("status", job_id=unlimited)
        if active["result_count"] > 0 and active["items"][0]["progress"].get("depth_start_count", 0) >= 2:
            break
        require(not active["done"] and time.monotonic() < deadline, "unlimited formula search stopped or did not stream")
        time.sleep(0.005)
    first_page = client.call("results", job_id=unlimited, limit=1)
    require(not first_page["done"] and first_page["results"][0]["solution"]["optimality_proven"],
            "first proven layer was not streamed while deeper search continued")
    client.call("cancel", job_id=unlimited)
    unlimited_status = client.wait(unlimited)
    require(unlimited_status["items"][0]["stop_reason"] == "cancelled", "unlimited formula search ignored cancellation")
    unlimited_rows = client.call("results", job_id=unlimited, limit=1000)
    require(unlimited_rows["results"][:1] == first_page["results"], "cancellation changed already persisted formula")
    require(unlimited_status["items"][0]["summary"]["results"] == unlimited_status["result_count"],
            "stream-only core lost result count")
    require([json.loads(line) for line in Path(unlimited_rows["result_file"]).read_text(encoding="utf-8").splitlines()] == unlimited_rows["results"],
            "JSONL-indexed paging differs from stored formulas")

    mono = [[0]*9] + [[5]*9 for _ in range(4)]
    mixed = [[0]*9, [5, 5, 5, 0, 0, 0, 0, 0, 0]] + [[5]*9 for _ in range(3)]
    enabled = [[True]*5 for _ in range(6)]
    disabled_u = [[False]*5, *enabled[1:]]
    validation = client.call("validate", request={"mode": "template", "templates": mono})
    require(validation["result_kind"] == "cube_state" and not validation["search_started"], "template validation searched or claimed a formula")
    require(validation["allowed_templates"] == enabled, "URFDLB template default matrix mismatch")
    for wrong in [
        {"templates": [[0]*9]*4}, {"templates": [[6]*9]*5}, {"allowed_templates": [[1]*5]*6},
        {"direction": "solve"}, {"scramble": ""}, {"generator_options": {}}, {"continuous": 1},
        {"find_generators": True, "generator_options": {"modulo_u": True}},
    ]:
        client.call("validate", expect_error=True, request={"mode": "template", "templates": mono, **wrong})
    template_items = [
        {"id": "states", "mode": "template", "templates": mixed, "allowed_templates": enabled, "max_results": 3},
        {"id": "continuous", "mode": "template", "templates": mono, "continuous": True, "max_results": 0},
        {"id": "empty", "mode": "template", "templates": [[5]*9]*5},
        {"id": "no-u", "mode": "template", "templates": mono, "allowed_templates": disabled_u},
        {"id": "generators", "mode": "template", "templates": mixed, "max_results": 3, "find_generators": True,
         "generator_options": {"max_turns": 30, "target_turns": 20, "max_phase1_turns": 20, "time_limit_ms": 1000, "node_limit": 0}},
        {"id": "generator-limited", "mode": "template", "templates": mixed, "max_results": 3,
         "find_generators": True, "generator_options": {"max_turns": 0}},
        {"id": "node-limited", "mode": "template", "templates": mixed, "max_results": 0, "node_limit": 1},
        {"id": "time-limited", "mode": "template", "templates": mixed, "max_results": 0, "node_limit": 0, "time_limit_ms": 1},
        {"id": "template-invalid", "mode": "template", "templates": [[False]*9]*5},
    ]
    template_job = client.call("submit", items=template_items, parallelism=3)["job_id"]
    template_status = client.wait(template_job)
    template_states = {item["item_id"]: item for item in template_status["items"]}
    require(template_states["template-invalid"]["state"] == "invalid", "template invalid item not isolated")
    require(template_states["states"]["stop_reason"] == "result_limit", "template state limit missing")
    require(template_states["node-limited"]["stop_reason"] == "node_limit", "template node limit missing")
    require(template_states["time-limited"]["stop_reason"] == "time_limit", "template time limit missing")
    for key in ("continuous", "empty", "no-u"):
        require(template_states[key]["summary"]["search_complete"], "small template constraints not exhausted")
    require(template_states["empty"]["results"] == template_states["no-u"]["results"] == 0, "unused/unavailable templates must be valid empty searches")
    cursor, template_rows = 0, []
    while True:
        page = client.call("results", job_id=template_job, cursor=cursor, limit=2)
        template_rows.extend(page["results"])
        require(page["next_cursor"] == cursor+len(page["results"]), "template pagination gap")
        cursor = page["next_cursor"]
        if not page["has_more"]:
            break
    template_by_item = {}
    for row in template_rows:
        state = row["solution"]
        template_by_item.setdefault(row["item_id"], []).append(state)
        require(state["kind"] == "cube_state" and state["verified"], "template result kind/verification missing")
        require(not any(key in state for key in ("formula", "moves", "turns", "actions")), "cube state disguised as a zero-step formula")
        legal = client.call("validate", request={"mode": "two_phase", "pattern": state["pattern"]})
        require(legal["facelets"] == state["facelets"], "template state is illegal or serializes inconsistently")
        if "generator" in state:
            generator = state["generator"]
            replay = client.call("apply", request={"scramble": ""}, moves=generator["formula"])
            require(replay["facelets"] == state["facelets"] == generator["terminal_facelets"], "template generator does not generate exact matched state")
            require(generator["turns"] == sum(action["turn_cost"] for action in generator["actions"]), "template generator counter mismatch")
    require(len(template_by_item["states"]) == 3 and all("generator" not in state for state in template_by_item["states"]), "disabled find_generators performed hidden search")
    require(any(state["generator"]["turns"] > 0 for state in template_by_item["generators"]), "only trivial template generator exercised")
    require(any("generator" not in state and not state["generator_search"]["solved"] for state in template_by_item["generator-limited"]), "generator budget lost matching states")
    require([json.loads(line) for line in Path(page["result_file"]).read_text(encoding="utf-8").splitlines()] == template_rows,
            "persisted template results differ from paged results")
    queued_templates = client.call("submit", items=[{"id": str(n), "mode": "template", "templates": mixed,
        "max_results": 0, "time_limit_ms": 0, "node_limit": 0} for n in range(50)], parallelism=1)["job_id"]
    client.call("cancel", job_id=queued_templates)
    cancelled_templates = client.wait(queued_templates)
    require(any(item["state"] == "cancelled" for item in cancelled_templates["items"]), "queued template jobs ignored cancellation")
    print(json.dumps({"passed": True, "checks": checks, "batch_results": len(rows)}, indent=2))
finally:
    client.close()
