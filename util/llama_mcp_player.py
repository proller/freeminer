#!/usr/bin/env python3
"""Run a local llama.cpp model as a cautious Luanti MCP player.

Usage:
    util/llama_mcp_player.py MODEL.gguf SERVER[:PORT]

The script starts llama-server and the local Freeminer client. The client joins
the requested server and exposes its MCP tools on loopback.
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import random
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CLIENT = ROOT / "build_-20" / "freeminer"


SYSTEM_PROMPT = """You control one Luanti player through MCP tools. Play cautiously and
use the tools to inspect the world before acting. Work toward the user's goal,
check each action's result, and adapt when it fails. For interactions that say
state=approaching, wait briefly and retry the same action. Use ordinary movement
and crafting. Avoid lava, deep drops, hostile creatures, other players, and
other players' builds. Do not send chat, run slash commands, teleport, or use
creative/fly/noclip controls. Stop if repeated actions fail or the player is in
immediate danger. Keep observations concise and do not repeat large map scans."""

MAX_TOOL_RESULT_CHARS = 2000
MAX_RECENT_MESSAGES = 12


class MCPClient:
    def __init__(self, url: str):
        self.url = url
        self.session_id: str | None = None
        self.request_id = 0

    def request(self, method: str, params: dict[str, Any] | None = None,
                notification: bool = False) -> dict[str, Any] | None:
        payload: dict[str, Any] = {"jsonrpc": "2.0", "method": method}
        if not notification:
            self.request_id += 1
            payload["id"] = self.request_id
        if params is not None:
            payload["params"] = params
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
            "MCP-Protocol-Version": "2025-06-18",
        }
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        request = urllib.request.Request(
            self.url, data=json.dumps(payload).encode(), headers=headers, method="POST"
        )
        with urllib.request.urlopen(request, timeout=30) as response:
            self.session_id = response.headers.get("Mcp-Session-Id", self.session_id)
            raw = response.read().decode()
        if notification or not raw:
            return None
        try:
            return json.loads(raw)
        except json.JSONDecodeError:
            data = "\n".join(
                line[6:] for line in raw.splitlines() if line.startswith("data: ")
            )
            if not data:
                raise RuntimeError(f"MCP returned an unreadable response: {raw[:300]}")
            return json.loads(data)

    def connect(self) -> list[dict[str, Any]]:
        result = self.request("initialize", {
            "protocolVersion": "2025-06-18",
            "capabilities": {},
            "clientInfo": {"name": "llama-cpp-luanti-player", "version": "1.0"},
        })
        if not result or "error" in result:
            raise RuntimeError(f"MCP initialize failed: {result}")
        self.request("notifications/initialized", notification=True)
        listed = self.request("tools/list")
        if not listed or "error" in listed:
            raise RuntimeError(f"MCP tools/list failed: {listed}")
        tools = listed.get("result", {}).get("tools", [])
        # Keep the actions player-like and leave public chat, slash commands,
        # teleport, and debug movement unavailable to the model.
        denied = {
            "send_chat_message", "teleport_player", "move_player_to", "press_keys"
        }
        tools = [tool for tool in tools if tool.get("name") not in denied]
        if not tools:
            raise RuntimeError("The game MCP endpoint exposed no usable tools")
        return tools

    def call_tool(self, name: str, arguments: dict[str, Any]) -> str:
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        if not result:
            return "MCP returned no result"
        if "error" in result:
            return json.dumps(result["error"], ensure_ascii=False)
        content = result.get("result", {}).get("content", [])
        text = "\n".join(item.get("text", "") for item in content if item.get("type") == "text")
        if not text:
            text = json.dumps(result.get("result", {}), ensure_ascii=False)
        return text[:MAX_TOOL_RESULT_CHARS]


def http_json(url: str, payload: dict[str, Any], timeout: int = 180) -> dict[str, Any]:
    request = urllib.request.Request(
        url,
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read())


def compact_messages(messages: list[dict[str, Any]], recent_limit: int) -> list[dict[str, Any]]:
    """Keep the system prompt, initial goal, and complete recent exchanges."""
    if len(messages) <= recent_limit + 2:
        return messages
    recent = messages[-recent_limit:]
    # A tool reply without its matching assistant tool-call message is not
    # valid chat history. Drop leading tool replies and their assistant call.
    while recent and recent[0].get("role") == "tool":
        recent.pop(0)
    if recent and recent[0].get("role") == "assistant" and recent[0].get("tool_calls"):
        recent.pop(0)
    return [messages[0], messages[1], *recent]


def is_context_overflow(error: urllib.error.HTTPError) -> bool:
    try:
        detail = error.read().decode("utf-8", errors="replace").lower()
    except OSError:
        detail = str(error).lower()
    return "context" in detail and (
        "exceeds" in detail or "available" in detail or "token" in detail
    )


def wait_ready(url: str, process: subprocess.Popen[bytes], label: str,
               timeout: int = 180) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"{label} exited with status {process.returncode}")
        try:
            with urllib.request.urlopen(url, timeout=2):
                return
        except (urllib.error.URLError, TimeoutError):
            time.sleep(0.5)
    raise TimeoutError(f"Timed out waiting for {label} at {url}")


def split_server_address(value: str) -> tuple[str, int]:
    if value.startswith("["):
        closing = value.find("]")
        if closing < 0:
            raise ValueError("IPv6 addresses with ports must use [address]:port")
        host = value[1:closing]
        suffix = value[closing + 1:]
        return host, int(suffix[1:]) if suffix.startswith(":") else 30000
    if value.count(":") == 1:
        host, port = value.rsplit(":", 1)
        if port.isdigit():
            return host, int(port)
    return value, 30000


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "model",
        help="Local GGUF path or Hugging Face repo[:quant], for example "
             "SoAIHQ/Qwen3.5-4B-GGUF:Q4_K_M",
    )
    parser.add_argument("server", help="Luanti host[:port]; port defaults to 30000")
    parser.add_argument("--goal", default=(
        "Explore the area safely, learn the controls, and gather a few common "
        "resources without damaging other players' builds."
    ))
    parser.add_argument("--client-bin", default=os.environ.get(
        "FREEMINER_BIN", str(DEFAULT_CLIENT)), help="Freeminer executable")
    parser.add_argument("--llama-server", default=os.environ.get(
        "LLAMA_SERVER", "llama-server"), help="llama.cpp server executable")
    parser.add_argument("--mcp-port", type=int, default=31001)
    parser.add_argument("--llama-port", type=int, default=8080)
    parser.add_argument("--name", default=f"LlamaPlayer{random.randint(1000, 9999)}")
    parser.add_argument("--max-turns", type=int, default=80)
    parser.add_argument("--ctx-size", type=int, default=8192)
    parser.add_argument(
        "--no-interactive", action="store_true",
        help="Do not read live instructions from the terminal",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    model_path = Path(args.model).expanduser()
    client_bin = Path(args.client_bin).expanduser().resolve()
    if model_path.is_file():
        model_args = ["-m", str(model_path.resolve())]
        model_display = str(model_path.resolve())
    else:
        hf_model = args.model.removeprefix("hf://")
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+(?::[A-Za-z0-9_.-]+)?", hf_model):
            print(
                f"Model file does not exist and this is not a Hugging Face repo: {args.model}",
                file=sys.stderr,
            )
            return 2
        model_args = ["-hf", hf_model]
        model_display = f"Hugging Face {hf_model} (downloaded and cached by llama.cpp)"
    if not client_bin.is_file():
        print(f"Freeminer executable does not exist: {client_bin}", file=sys.stderr)
        print("Build it first or pass --client-bin /path/to/freeminer.", file=sys.stderr)
        return 2
    host, port = split_server_address(args.server)
    if not host or not 1 <= port <= 65535:
        print("Invalid Luanti server address or port", file=sys.stderr)
        return 2
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,20}", args.name):
        print("Player name must be 1–20 letters, digits, underscores, or hyphens", file=sys.stderr)
        return 2
    password = os.environ.get("LUANTI_PASSWORD", "")
    if "\n" in password or "\r" in password:
        print("LUANTI_PASSWORD must be a single line", file=sys.stderr)
        return 2

    processes: list[subprocess.Popen[bytes]] = []
    temp_config: str | None = None
    try:
        llama = subprocess.Popen([
            args.llama_server, *model_args, "--alias", "luanti-player",
            "--host", "127.0.0.1", "--port", str(args.llama_port),
            "--ctx-size", str(args.ctx_size), "--jinja",
        ])
        processes.append(llama)
        wait_ready(f"http://127.0.0.1:{args.llama_port}/health", llama, "llama-server")

        config = tempfile.NamedTemporaryFile("w", prefix="luanti-ai-", suffix=".conf", delete=False)
        temp_config = config.name
        config.write(f"name = {args.name}\nrespawn_auto = false\n")
        if password:
            config.write(f"password = {password}\n")
        config.close()
        os.chmod(temp_config, 0o600)
        client = subprocess.Popen([
            str(client_bin), "--go", "--address", host, "--port", str(port),
            "--name", args.name, "--config", temp_config,
            "-enable_mcp=1", f"-mcp_port={args.mcp_port}",
        ])
        processes.append(client)

        mcp = MCPClient(f"http://127.0.0.1:{args.mcp_port}/mcp")
        deadline = time.monotonic() + 180
        tools: list[dict[str, Any]] | None = None
        while time.monotonic() < deadline:
            if client.poll() is not None:
                raise RuntimeError(f"Freeminer exited with status {client.returncode}")
            try:
                tools = mcp.connect()
                break
            except (urllib.error.URLError, TimeoutError, RuntimeError, ValueError):
                time.sleep(1)
        if tools is None:
            raise TimeoutError("Timed out waiting for the Freeminer MCP endpoint")

        print(f"Model: {model_display}", flush=True)
        print(f"Connected to {host}:{port} as {args.name}; MCP exposes {len(tools)} tools.", flush=True)
        command_queue: queue.Queue[str | None] = queue.Queue()
        operator_instructions: list[str] = []
        if not args.no_interactive and sys.stdin.isatty():
            print(
                "Live control: type an instruction and press Enter. "
                "Use :help for console commands.",
                flush=True,
            )

            def read_operator_input() -> None:
                for line in sys.stdin:
                    command = line.strip()
                    if not command:
                        continue
                    if command == ":help":
                        print(
                            "Enter a new task or steering instruction. "
                            ":stop or :quit stops the agent and its child processes.",
                            flush=True,
                        )
                    elif command in {":stop", ":quit"}:
                        command_queue.put(None)
                        return
                    else:
                        command_queue.put(command)
                        print(f"[queued] {command}", flush=True)

            threading.Thread(target=read_operator_input, daemon=True).start()

        messages: list[dict[str, Any]] = [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": args.goal},
        ]

        def apply_operator_input() -> bool:
            """Add queued operator instructions; return true when asked to stop."""
            stop_requested = False
            while True:
                try:
                    command = command_queue.get_nowait()
                except queue.Empty:
                    break
                if command is None:
                    stop_requested = True
                    break
                operator_instructions.append(command)
                del operator_instructions[:-8]
                messages[0]["content"] = SYSTEM_PROMPT + (
                    "\n\nCurrent operator instructions (follow the newest):\n"
                    + "\n".join(f"- {item}" for item in operator_instructions)
                )
                messages.append({
                    "role": "user",
                    "content": f"New live operator instruction: {command}",
                })
                print("[agent] Live instruction added to context.", flush=True)
            return stop_requested

        last_failure: tuple[str, str, str] | None = None
        failure_streak = 0
        player_health: int | None = None
        turn = 0
        while turn < args.max_turns:
            if apply_operator_input():
                print("Stopping player agent by operator request.", flush=True)
                break
            turn += 1
            messages = compact_messages(messages, MAX_RECENT_MESSAGES)
            request_payload = {
                "model": "luanti-player",
                "messages": messages,
                "tools": [{"type": "function", "function": {
                    "name": tool["name"],
                    "description": tool.get("description", ""),
                    "parameters": tool.get("inputSchema", {"type": "object", "properties": {}}),
                }} for tool in tools],
                "tool_choice": "auto",
                "temperature": 0.2,
                "max_tokens": 1200,
            }
            try:
                response = http_json(
                    f"http://127.0.0.1:{args.llama_port}/v1/chat/completions",
                    request_payload,
                )
            except urllib.error.HTTPError as error:
                if not is_context_overflow(error):
                    raise
                messages = compact_messages(messages, 4)
                request_payload["messages"] = messages
                print(
                    "[agent] Context limit reached; retrying with a shorter history.",
                    flush=True,
                )
                response = http_json(
                    f"http://127.0.0.1:{args.llama_port}/v1/chat/completions",
                    request_payload,
                )
            choice = response["choices"][0]["message"]
            calls = choice.get("tool_calls", [])
            if choice.get("content"):
                print(f"[agent] {choice['content']}", flush=True)
            if not calls:
                # Keep the session alive so a later terminal instruction can
                # start another task even if the model has no action right now.
                time.sleep(0.25)
                continue
            messages.append({
                "role": "assistant",
                "content": choice.get("content"),
                "tool_calls": calls,
            })
            for call_index, call in enumerate(calls):
                function = call.get("function", {})
                name = function.get("name", "")
                try:
                    arguments = json.loads(function.get("arguments") or "{}")
                except json.JSONDecodeError as error:
                    result = f"Invalid JSON arguments: {error}"
                else:
                    if call_index >= 4:
                        result = "Skipped: at most four tool calls are executed per model turn."
                    elif player_health is not None and player_health <= 5 and name not in {
                        "get_player_state", "get_inventory", "get_chat_messages"
                    }:
                        result = "Skipped: player health is critical. Inspect state and stop." 
                    else:
                        print(f"[tool] {name} {json.dumps(arguments, ensure_ascii=False)}", flush=True)
                        try:
                            result = mcp.call_tool(name, arguments)
                        except Exception as error:  # keep a transient tool failure inside the agent loop
                            result = f"Tool request failed: {error}"
                        print(f"[result] {result[:1000]}", flush=True)
                        try:
                            result_data = json.loads(result)
                        except (json.JSONDecodeError, TypeError):
                            result_data = {}
                        if name == "get_player_state" and isinstance(result_data.get("health"), int):
                            player_health = result_data["health"]
                        if result_data.get("state") == "approaching":
                            time.sleep(0.85)
                        if result_data.get("success") is False:
                            failure = (name, json.dumps(arguments, sort_keys=True),
                                       str(result_data.get("error", "unknown error")))
                            if failure == last_failure:
                                failure_streak += 1
                            else:
                                last_failure = failure
                                failure_streak = 1
                            if failure_streak >= 3:
                                print("Stopping after three identical tool failures.", flush=True)
                                messages.append({
                                    "role": "tool",
                                    "tool_call_id": call.get("id", ""),
                                    "content": "Stopped: the same tool action failed three times.",
                                })
                                return 0
                        else:
                            last_failure = None
                            failure_streak = 0
                messages.append({
                    "role": "tool",
                    "tool_call_id": call.get("id", ""),
                    "content": result,
                })
            # Keep enough recent context for action feedback without letting
            # map scans grow the prompt for the entire session.
            messages = compact_messages(messages, MAX_RECENT_MESSAGES)
        else:
            print(f"Reached the {args.max_turns}-turn limit.", flush=True)
        return 0
    except KeyboardInterrupt:
        print("Stopping player agent.", flush=True)
        return 130
    except Exception as error:
        print(f"Player agent failed: {error}", file=sys.stderr)
        return 1
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.terminate()
        if temp_config:
            try:
                os.unlink(temp_config)
            except FileNotFoundError:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
