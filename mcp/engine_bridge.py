"""One persistent native engine, one reader, correlated concurrent requests.

This module only transports JSON. Cube moves, search costs and verification are
owned by the C++ engine and are never recalculated here.
"""
from __future__ import annotations

import asyncio
import json
import logging
import subprocess
from pathlib import Path
from typing import Any


class EngineUnavailable(RuntimeError):
    """The native process cannot answer; existing jobs must not be recreated."""


class EngineBridge:
    def __init__(self, command: list[str], *, cwd: Path, request_timeout: float = 30.0):
        self.command = command
        self.cwd = cwd
        self.request_timeout = request_timeout
        self._process: asyncio.subprocess.Process | None = None
        self._start_lock = asyncio.Lock()
        self._write_lock = asyncio.Lock()
        self._pending: dict[str, asyncio.Future[dict[str, Any]]] = {}
        self._sequence = 0
        self._reader: asyncio.Task[None] | None = None
        self._stderr: asyncio.Task[None] | None = None
        self._failure: str | None = None
        self._closed = False

    async def start(self) -> None:
        async with self._start_lock:
            if self._failure:
                raise EngineUnavailable(self._failure)
            if self._closed:
                raise EngineUnavailable("Native engine connection is closed.")
            if self._process is not None:
                if self._process.returncode is not None:
                    raise EngineUnavailable(f"Native engine exited with code {self._process.returncode}.")
                return
            try:
                self._process = await asyncio.create_subprocess_exec(
                    *self.command, cwd=str(self.cwd),
                    stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.PIPE, limit=16 * 1024 * 1024,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                )
            except OSError as exc:
                self._failure = f"Cannot start native engine: {exc}"
                raise EngineUnavailable(self._failure) from exc
            self._reader = asyncio.create_task(self._read_responses(), name="cube-engine-responses")
            self._stderr = asyncio.create_task(self._read_stderr(), name="cube-engine-stderr")

    def _fail_pending(self, message: str) -> None:
        self._failure = message
        for future in self._pending.values():
            if not future.done():
                future.set_exception(EngineUnavailable(message))

    async def _read_responses(self) -> None:
        assert self._process is not None and self._process.stdout is not None
        try:
            while line := await self._process.stdout.readline():
                response = json.loads(line)
                if (not isinstance(response, dict) or not isinstance(response.get("id"), str)
                        or not isinstance(response.get("ok"), bool)):
                    raise ValueError("Malformed native engine response envelope.")
                future = self._pending.get(response["id"])
                # Cancellation/timeout may have removed the caller. Its late reply
                # must not be attached to a later request.
                if future is not None and not future.done():
                    future.set_result(response)
            code = await self._process.wait()
            self._fail_pending(f"Native engine exited with code {code}; active jobs are unavailable.")
        except asyncio.CancelledError:
            raise
        except (ValueError, OSError, asyncio.LimitOverrunError) as exc:
            self._fail_pending(f"Native engine protocol failed: {exc}")
            if self._process.returncode is None:
                self._process.terminate()

    async def _read_stderr(self) -> None:
        assert self._process is not None and self._process.stderr is not None
        while line := await self._process.stderr.readline():
            logging.getLogger("cube.engine").warning("%s", line.decode("utf-8", "replace").rstrip())

    async def request(self, op: str, **payload: Any) -> dict[str, Any]:
        await self.start()
        assert self._process is not None and self._process.stdin is not None
        self._sequence += 1
        request_id = f"mcp-{self._sequence}"
        request = {"id": request_id, "op": op, **payload}
        future: asyncio.Future[dict[str, Any]] = asyncio.get_running_loop().create_future()
        self._pending[request_id] = future
        try:
            async with self._write_lock:
                if self._failure:
                    raise EngineUnavailable(self._failure)
                self._process.stdin.write((json.dumps(request, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8"))
                await self._process.stdin.drain()
            return await asyncio.wait_for(future, self.request_timeout)
        except TimeoutError as exc:
            raise EngineUnavailable(
                f"Native engine did not answer {op!r} within {self.request_timeout:g}s; "
                "submission outcome may be unknown. Do not blindly resubmit."
            ) from exc
        except (BrokenPipeError, ConnectionResetError) as exc:
            self._fail_pending("Native engine closed its input; active jobs are unavailable.")
            raise EngineUnavailable(self._failure) from exc
        finally:
            self._pending.pop(request_id, None)
            if not future.done():
                future.cancel()
            elif not future.cancelled():
                # Retrieve an exception if writing failed before await future.
                future.exception()

    async def close(self) -> None:
        if self._closed:
            return
        process = self._process
        if process is not None and process.returncode is None:
            if not self._failure:
                try:
                    await asyncio.wait_for(self.request("shutdown"), timeout=3.0)
                except (EngineUnavailable, TimeoutError):
                    pass
            if process.stdin is not None:
                process.stdin.close()
            try:
                await asyncio.wait_for(process.wait(), timeout=3.0)
            except TimeoutError:
                process.terminate()
                await process.wait()
        self._closed = True
        self._fail_pending("Native engine connection is closed.")
        for task in (self._reader, self._stderr):
            if task is not None and not task.done():
                task.cancel()
        await asyncio.gather(*(task for task in (self._reader, self._stderr) if task is not None), return_exceptions=True)
