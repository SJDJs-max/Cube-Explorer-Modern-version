"""Exercise older MCP host protocol handshakes against the real stdio EXE."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent


async def probe(version: str, server: Path, engine: Path):
    environment = os.environ.copy()
    environment["PATH"] = str(ROOT / "tools/Qt/bin") + os.pathsep + str(ROOT / "tools/bin") + os.pathsep + environment.get("PATH", "")
    process = await asyncio.create_subprocess_exec(
        str(server.resolve()), "--engine", str(engine.resolve()),
        "--tables", str(ROOT / "tables/original"), "--workspace", str(ROOT / "cache/mcp-legacy-test"),
        stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
        cwd=ROOT, env=environment, limit=1024*1024,
    )
    async def send(payload):
        process.stdin.write((json.dumps(payload)+"\n").encode())
        await process.stdin.drain()
    async def response(identifier):
        while True:
            line = await asyncio.wait_for(process.stdout.readline(), 10)
            if not line:
                raise AssertionError((await process.stderr.read()).decode())
            message = json.loads(line)
            if message.get("id") == identifier:
                assert "error" not in message, message
                return message["result"]
    try:
        await send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
            "protocolVersion": version, "capabilities": {}, "clientInfo": {"name": "cube-compatibility-test", "version": "1"}}})
        initialized = await response(1)
        assert initialized["protocolVersion"] == version, initialized
        assert initialized["serverInfo"]["version"] == "0.3.1", initialized
        await send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        await send({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
        tools = await response(2)
        assert len(tools["tools"]) == 9, tools
        await send({"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "get_capabilities", "arguments": {}}})
        result = await response(3)
        assert not result.get("isError"), result
        text = json.loads(result["content"][0]["text"])
        assert text["ok"] and text["result"]["formula_rotation_cost"] == 0, text
        if "structuredContent" in result:
            assert result["structuredContent"] == text
        print(json.dumps({"status": "PASS", "protocolVersion": version}))
    finally:
        process.stdin.close()
        try:
            await asyncio.wait_for(process.wait(), 5)
        except TimeoutError:
            process.terminate()
            await process.wait()


async def main(server, engine):
    for version in ["2024-11-05", "2025-03-26", "2025-06-18"]:
        await probe(version, server, engine)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--server-exe", type=Path, default=ROOT / "mcp/dist/CubeExplorerMcp/CubeExplorerMcp.exe")
    parser.add_argument("--engine", type=Path, default=ROOT / "build/release/src/engine/CubeExplorerEngine.exe")
    options = parser.parse_args()
    asyncio.run(main(options.server_exe, options.engine))
