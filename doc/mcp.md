# Freeminer MCP integration

When `enable_mcp = true`, Freeminer exposes a standard MCP Streamable HTTP
endpoint at `http://127.0.0.1:3001/mcp`. It supports JSON responses to POST
requests, per-client sessions, notification acknowledgements, session deletion,
and the MCP protocol versions `2025-03-26` and `2025-06-18`. GET returns HTTP
405 because Freeminer currently has no unsolicited server-to-client messages
and does not need an SSE listening stream.

The listener is restricted to IPv4 loopback. Its port can be changed with
`mcp_port`.

### Cline configuration

Cline can connect directly to the Streamable HTTP endpoint. Configure this URL:

```text
http://127.0.0.1:3001/mcp
```

The Freeminer client must already be running with `enable_mcp = true`.

Each HTTP session must perform the MCP `initialize` exchange and send
`notifications/initialized` before listing or calling tools.

## Local llama.cpp player

`util/llama_mcp_player.py` starts a local `llama-server`, starts the Freeminer
client connected to a Luanti server, and runs an agent loop through the client's
loopback MCP endpoint. It needs a llama.cpp `llama-server` executable and a
GGUF model with a chat template that supports tool calls. Give it a local model
path or a Hugging Face repository and optional quantization:

```sh
util/llama_mcp_player.py SoAIHQ/Qwen3.5-4B-GGUF:Q4_K_M fm.setun.net:30013
# Or use a model file already downloaded:
util/llama_mcp_player.py /path/to/model.gguf fm.example.net:30000
```

For a repository identifier, llama.cpp downloads the selected GGUF the first
time and caches it for later runs. The cache location follows llama.cpp's
`LLAMA_CACHE` environment variable.

The port defaults to `30000` if the server address omits it. The client binary
defaults to `build_-20/freeminer`; use `--client-bin` if it is elsewhere. The
MCP listener defaults to port `31001`, and the llama.cpp API defaults to
`8080`. Both can be changed with `--mcp-port` and `--llama-port`. For a server
account, pass `--name PlayerName` and set `LUANTI_PASSWORD` in the environment.
Use `--goal` to set a different task.

The agent omits chat, teleport, direct position changes, and raw key toggles.
It stops on repeated identical tool failures or when observed health is critical.
Press Ctrl+C to stop the agent and both child processes.

When started from an interactive terminal, you can steer it while it is
running: type a new instruction and press Enter. The agent adds it to its
context after the current model response and its tool calls finish. Recent live
instructions remain in context as the conversation is shortened. Enter `:help`
for a reminder or `:stop` (also `:quit`) to stop the agent and child processes.
Use `--no-interactive` when terminal input should not be read.

`rotate_player` sets absolute camera angles in degrees. `look_at_position`
accepts a world position in node coordinates, and `look_at_object` accepts an
active object ID from `get_nearby_objects`; both aim the camera without moving
the player. These calls submit a rotation for the next client frame. Read
`get_player_state` and `get_pointed_thing` afterward to confirm the resulting
view. Aimed camera rays are useful for ordinary view-dependent interaction,
while `dig_node`, `place_node`, and object action tools can target their
explicit positions or IDs without changing the camera.

## Chat tools

`send_chat_message` sends public chat as the connected player. Its result has a
`delivery` field of `sent` or `queued`; queued messages are sent automatically
when the client chat rate limit permits. A message beginning with `/` is sent as
a server command.

`get_chat_messages` defaults to the structured MCP history. Each message has an
`id`, `type`, `sender`, `text`, `formatted`, and `timestamp`. For polling, pass
the previous response's `next_after_id` as `after_id`. Up to 1,000 messages are
retained and each call returns at most 200.
