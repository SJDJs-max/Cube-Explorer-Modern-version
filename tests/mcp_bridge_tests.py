from __future__ import annotations

import asyncio
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "mcp"))
from engine_bridge import EngineBridge, EngineUnavailable
from schemas import SearchSpec, TemplateSearchSpec


class BridgeTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.bridge = EngineBridge([sys.executable, "-u", str(ROOT / "tests/mcp_mock_engine.py")], cwd=ROOT)

    async def asyncTearDown(self):
        await self.bridge.close()

    async def test_concurrent_responses_are_correlated(self):
        replies = await asyncio.gather(*(self.bridge.request("echo", value=n, delay=(40-n)*0.001) for n in range(40)))
        self.assertEqual([reply["result"]["echo"] for reply in replies], list(range(40)))
        self.assertEqual(len({reply["id"] for reply in replies}), 40)

    async def test_cancelled_caller_does_not_steal_late_response(self):
        first = asyncio.create_task(self.bridge.request("echo", value="cancelled", delay=0.1))
        await asyncio.sleep(0.02)
        first.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await first
        second = await self.bridge.request("echo", value="second", delay=0.15)
        self.assertEqual(second["result"]["echo"], "second")

    async def test_timeout_does_not_steal_late_response(self):
        self.bridge.request_timeout = 0.04
        with self.assertRaisesRegex(EngineUnavailable, "outcome may be unknown"):
            await self.bridge.request("echo", value="late", delay=0.1)
        self.bridge.request_timeout = 2
        second = await self.bridge.request("echo", value="second", delay=0.15)
        self.assertEqual(second["result"]["echo"], "second")

    async def test_process_exit_fails_pending_and_never_restarts(self):
        with self.assertRaisesRegex(EngineUnavailable, "exited"):
            await self.bridge.request("exit")
        process = self.bridge._process
        with self.assertRaises(EngineUnavailable):
            await self.bridge.request("echo")
        self.assertIs(process, self.bridge._process)

    async def test_malformed_protocol_fails_explicitly(self):
        with self.assertRaisesRegex(EngineUnavailable, "protocol failed"):
            await self.bridge.request("malformed")

    async def test_missing_executable_fails_explicitly(self):
        bridge = EngineBridge([str(ROOT / "mcp/not-present.exe")], cwd=ROOT)
        with self.assertRaisesRegex(EngineUnavailable, "Cannot start"):
            await bridge.request("capabilities")
        await bridge.close()


class SchemaTests(unittest.TestCase):
    def test_unspecified_formula_flags_are_not_inserted(self):
        self.assertEqual(SearchSpec(mode="two_phase", scramble="").payload(), {"mode": "two_phase", "scramble": ""})

    def test_full_explicit_options_preserved(self):
        payload = dict(mode="formula", direction="generate", scramble="R U", allowed_moves=["r", "x", "R2"],
                       min_turns=0, max_turns=7, max_results=0, time_limit_ms=0, node_limit=0,
                       modulo_u=False, remove_u_symmetric_prefix=False, reject_alternating_triples=False,
                       deduplicate_commuting=False, heuristic="strong", center_ori=[-1]*6,
                       memoize_dead_ends=False, quotient_free_rotations=False)
        self.assertEqual(SearchSpec(**payload).payload(), payload)

    def test_unknown_or_ambiguous_inputs_rejected(self):
        for payload in [dict(mode="formula"), dict(mode="formula", scramble="", facelets=""),
                        dict(mode="formula", scramble="", typo=True),
                        dict(mode="formula", scramble="", move_symbols="R", allowed_moves=["R"])]:
            with self.assertRaises(ValueError):
                SearchSpec(**payload)

    def test_optimal_options_preserve_requested_direction(self):
        payload = dict(mode="optimal", direction="generate", scramble="R U", max_turns=20,
                       time_limit_ms=0, node_limit=0, initial_solution="R U", excluded_solutions=["R U"])
        self.assertEqual(SearchSpec(**payload).payload(), payload)

    def test_unlimited_depth_and_real_triple_option(self):
        for depth in (-1, 0, 256, 2147483647):
            payload = dict(mode="formula", scramble="R", max_turns=depth, max_results=0, time_limit_ms=0, node_limit=0)
            self.assertEqual(SearchSpec(**payload).payload(), payload)
        payload = dict(mode="two_phase", scramble="R U", use_triple_search=True)
        self.assertEqual(SearchSpec(**payload).payload(), payload)
        with self.assertRaises(ValueError):
            SearchSpec(mode="formula", scramble="R", max_turns=-2)

    def test_template_spec_forwards_all_options(self):
        payload = dict(mode="template", id="pattern-1", templates=[[0, 1, 2, 3, 4, 5, 0, 1, 2]]*5,
                       allowed_templates=[[True, False, True, False, True]]*6, continuous=True,
                       max_results=0, time_limit_ms=0, node_limit=0, find_generators=True,
                       generator_options=dict(max_turns=30, target_turns=20, max_phase1_turns=20,
                                              time_limit_ms=5000, node_limit=0))
        self.assertEqual(TemplateSearchSpec(**payload).payload(), payload)
        self.assertEqual(TemplateSearchSpec(templates=[[5]*9]*5).payload(),
                         {"mode": "template", "templates": [[5]*9]*5})

    def test_template_schema_rejects_wrong_shapes_and_label_coercion(self):
        for invalid in [dict(templates=[[0]*9]*4), dict(templates=[[0]*8]*5),
                        dict(templates=[[6]*9]*5), dict(templates=[[False]*9]*5),
                        dict(templates=[[0]*9]*5, allowed_templates=[[1]*5]*6),
                        dict(templates=[[0]*9]*5, scramble=""),
                        dict(templates=[[0]*9]*5, generator_options={"modulo_u": True})]:
            with self.assertRaises(ValueError):
                TemplateSearchSpec(**invalid)


if __name__ == "__main__":
    unittest.main(verbosity=2)
