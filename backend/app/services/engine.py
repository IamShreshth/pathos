"""Adapter around the C++ engine process.

The engine runs as a long-lived subprocess (`pathos_engine <graph> serve`) so
the road graph is loaded into memory once. Commands are written one per line
to its stdin and each answer is one JSON line on its stdout. A lock keeps
request/response pairs in order; a crashed or hung engine is restarted.
"""

from __future__ import annotations

import json
import logging
import queue
import subprocess
import threading
from pathlib import Path

log = logging.getLogger("pathos.engine")


class EngineError(RuntimeError):
    """The engine process failed (crash, timeout, unreadable output)."""


class EngineRequestError(ValueError):
    """The engine rejected the command (bad node ID, bad arguments...)."""


class EngineClient:
    def __init__(self, engine_path: Path, graph_path: Path, timeout_s: float = 30.0):
        self.engine_path = engine_path
        self.graph_path = graph_path
        self.timeout_s = timeout_s
        self.info: dict = {}
        self._proc: subprocess.Popen[str] | None = None
        self._lines: queue.Queue[str | None] = queue.Queue()
        self._lock = threading.Lock()

    # ------------------------------------------------------------ lifecycle
    def start(self) -> dict:
        if not self.engine_path.exists():
            raise EngineError(f"engine binary not found at {self.engine_path} — run engine/build.sh")
        if not self.graph_path.exists():
            raise EngineError(f"graph file not found at {self.graph_path} — run scripts/prepare_graph.py")
        self._lines = queue.Queue()
        self._proc = subprocess.Popen(
            [str(self.engine_path), str(self.graph_path), "serve"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        threading.Thread(target=self._pump, args=(self._proc, self._lines), daemon=True).start()
        threading.Thread(target=self._drain_stderr, args=(self._proc,), daemon=True).start()
        ready = self._read_line(timeout=max(self.timeout_s, 60.0))
        if not ready.get("ok") or not ready.get("ready"):
            self.stop()
            raise EngineError(f"engine failed to start: {ready.get('error', ready)}")
        self.info = ready
        log.info("engine ready: %s nodes, %s edges (%s)", ready["nodes"], ready["edges"], ready["graph_version"])
        return ready

    def stop(self) -> None:
        proc, self._proc = self._proc, None
        if proc and proc.poll() is None:
            try:
                proc.stdin.write("quit\n")  # type: ignore[union-attr]
                proc.stdin.flush()  # type: ignore[union-attr]
                proc.wait(timeout=2)
            except Exception:
                proc.kill()

    @property
    def alive(self) -> bool:
        return self._proc is not None and self._proc.poll() is None

    # ------------------------------------------------------------- requests
    def route(self, algorithm: str, weight: str, source: int, target: int, trace: bool) -> dict:
        return self._call(f"route {algorithm} {weight} {source} {target} {1 if trace else 0}")

    def tour(self, weight: str, stops: list[int]) -> dict:
        return self._call(f"tour {weight} " + " ".join(str(s) for s in stops))

    def _call(self, command: str) -> dict:
        with self._lock:
            for attempt in (1, 2):
                if not self.alive:
                    log.warning("engine not running; (re)starting")
                    self.start()
                try:
                    self._proc.stdin.write(command + "\n")  # type: ignore[union-attr]
                    self._proc.stdin.flush()  # type: ignore[union-attr]
                    reply = self._read_line(self.timeout_s)
                    break
                except (BrokenPipeError, EngineError) as exc:
                    log.error("engine call failed (attempt %d): %s", attempt, exc)
                    if self._proc:
                        self._proc.kill()
                    self._proc = None
                    if attempt == 2:
                        raise EngineError(str(exc)) from exc
        if not reply.get("ok"):
            raise EngineRequestError(reply.get("error", "engine rejected the request"))
        return reply

    # -------------------------------------------------------------- helpers
    def _read_line(self, timeout: float) -> dict:
        try:
            line = self._lines.get(timeout=timeout)
        except queue.Empty:
            raise EngineError(f"engine did not answer within {timeout:.0f}s") from None
        if line is None:
            raise EngineError("engine process exited")
        try:
            return json.loads(line)
        except json.JSONDecodeError as exc:
            raise EngineError(f"unreadable engine output: {line[:200]!r}") from exc

    @staticmethod
    def _pump(proc: subprocess.Popen[str], lines: queue.Queue[str | None]) -> None:
        assert proc.stdout is not None
        for line in proc.stdout:
            lines.put(line)
        lines.put(None)

    @staticmethod
    def _drain_stderr(proc: subprocess.Popen[str]) -> None:
        assert proc.stderr is not None
        for line in proc.stderr:
            log.warning("engine stderr: %s", line.rstrip())
