# OdysseusBridge — the Unreal Engine 5 plugin that lets Odysseus drive the live editor

> **Open‑source (MIT) · drop‑in · no fork, no service.** The Unreal Engine plugin for **Odysseus** —
> it hands Odysseus's agent the **live** UE5 editor as a toolset. (It speaks standard MCP, so any MCP
> client *can* connect — but it's built for Odysseus.) Clone → build → connect.

A tiny **MCP server that runs inside the UE 5.7 editor**. Install it in your UE project, point
**Odysseus** at it, and Odysseus's agent can read the open project and run editor Python against the
live `unreal` API.

```
Odysseus ──MCP / HTTP──▶ OdysseusBridge (in-editor) ──▶ UE editor · Python API
```

## Tools
The server exposes **eight** native MCP tools (they show up in `tools/list`):

- **`project_info`** — name, absolute directory, and engine version of the open project. *(no args)*
- **`run_python`** — runs Python inside the live editor (the `unreal` module is in scope) and returns
  the captured log + result/traceback. *(arg: `script`)*
- **`list_assets`** — asset paths under a content folder. *(arg: `folder`, default `/Game`)*
- **`spawn_actor`** — spawn N `StaticMeshActor`s with a mesh, in a row. *(args: `mesh_path`, `count`)*
- **`datatable_rows`** — list a DataTable's row names. *(arg: `asset_path`)*
- **`create_blueprint`** — create a Blueprint asset. *(args: `name`, `path`, `parent_class`)*
- **`create_material`** — create a Material asset. *(args: `name`, `path`)*
- **`import_asset`** — import a file (FBX/image/…) into the content browser. *(args: `source`, `destination`)*

`run_python` is the workhorse: the **entire `unreal` editor Python API is reachable through it** — but
*you* (the agent) write the Python. There is no separate "create blueprint" or "import asset" tool;
those are things you **script**.

~450 lines of C++ (a 415‑line `.cpp` + a 39‑line header), MIT — small and self‑contained (public Epic APIs + the open MCP spec; see
`CREDITS.md`), **build‑verify in your editor**.

## What it can really do — the inventory, by domain
To make `run_python` reliable instead of guesswork, **`unreal_helpers.py`** ships **13 ready functions**,
each guarded (a bad call returns `ERR: …`, never a crash). Drop it into your project's `Content/Python`
(UE auto‑imports it) and call them through `run_python`:

```python
import unreal_helpers as u
print(u.project()); print(u.spawn_static('/Engine/BasicShapes/Cube.Cube', 8))
```

This is the honest, tested surface:

**Recon — read the editor**
- `project()` — project file, current level, live actor count
- `actors(limit=40)` — list level actors as `label [Class]`
- `find_assets(folder="/Game", recursive=True, limit=60)` — list asset paths under a content folder

**Scene — build / edit actors**
- `spawn_static(mesh_path, n=1, …)` — spawn N `StaticMeshActor`s in a grid
- `spawn_skeletal(mesh_path, n=1, …)` — spawn N `SkeletalMeshActor`s in a grid
- `scale_actor(label, s)` — uniform scale, reads the new value back
- `move_actor(label, x, y, z)` — set world location, reads it back
- `clear(prefix)` — destroy every actor whose label starts with `prefix`

**Look**
- `screenshot(name, width=1600, height=900)` — high‑res viewport shot → `Saved/Screenshots`

**Data**
- `datatable_rows(asset_path, limit=30)` — list a DataTable's row names *(read‑only)*

**Persist / level**
- `save_all()` — save every dirty package
- `open_level(path)` — open a level / map

**Util**
- `toast(msg)` — on‑screen + log message in the editor

Anything beyond these 13 — creating Blueprints, editing materials, importing FBX, running commandlets —
is **reachable but not pre‑wired**: you write the `unreal` Python for it through `run_python`. The helpers
are the proven shortcuts, not the ceiling.

## Footprint
**~454 lines of C++** — no external runtime, no vendored libraries; it uses only engine modules already in your editor.

| File | Lines | What it holds |
|---|---:|---|
| `OdysseusBridge.cpp` | 415 | the server — loopback + token gate, JSON-RPC dispatch, the shared `RunEditorPython` exec, and all 8 tool handlers |
| `OdysseusBridge.h` | 39 | the module class + members |
| `OdysseusBridge.Build.cs` | 24 | the 7 engine module dependencies |

Inside the `.cpp`, roughly a third is reusable helpers (port pick, RFC‑8259 escaping, auth, JSON), a third is
the HTTP lifecycle + request dispatch, and a third is the 8 tools — each a thin wrapper that builds `unreal`
Python and runs it through the one shared exec path.

## Install (any UE 5.7 project)
1. Copy `OdysseusBridge/` into `<YourProject>/Plugins/` → `<YourProject>/Plugins/OdysseusBridge/`.
2. Enable it in `<YourProject>.uproject`:
   ```json
   "Plugins": [ { "Name": "OdysseusBridge", "Enabled": true } ]
   ```
3. Build the editor target (or just open the project — it offers to compile):
   ```powershell
   & "<UE>\Engine\Build\BatchFiles\Build.bat" <Project>Editor Win64 Development `
     -Project="<...>\<YourProject>.uproject" -WaitMutex
   ```
4. *(optional)* copy `unreal_helpers.py` into `<YourProject>/Content/Python/` for the 13 helpers above.

## Run
Open the editor; the bridge starts and logs
`OdysseusBridge MCP listening at http://127.0.0.1:8762/mcp`.
Set `ODYSSEUS_BRIDGE_PORT` to pin a different port. It serves only while the editor is open.

```
MCP    : http://127.0.0.1:<port>/mcp   (JSON-RPC 2.0, streamable HTTP, multi-client)
health : http://127.0.0.1:<port>/odysseus/health
```

## Connect an agent
- **Odysseus:** `python scripts/connect_unreal.py --token-file "<YourProject>/Saved/OdysseusBridge/auth_token"`
  (or Settings → MCP → Add → HTTP). Odysseus's agent then drives the **live** UE editor via `mcp__odybridge__*` — that one step is what turns Odysseus into a UE‑driving agent.
- **Claude Code:** `.mcp.json` at your workspace root:
  ```json
  { "mcpServers": { "ody-bridge": {
    "type": "http",
    "url": "http://127.0.0.1:8762/mcp",
    "headers": { "Authorization": "Bearer <token>" }
  } } }
  ```
- **Any MCP HTTP client:** point it at the URL + the bearer token.

## Using it — a typical agent loop
The reliable pattern (and what an agent *skill* should encode): **recon → one small action → read it back → verify.**
```python
project_info()                                   # confirm you're on the real editor
run_python("import unreal_helpers as u; print(u.spawn_static('/Engine/BasicShapes/Cube.Cube', 8))")
run_python("import unreal_helpers as u; print(u.actors())")   # read it back — never assume
```
- **One action at a time, printed and verified** — not ten things in one script you can't debug.
- The guarded helper returns `ERR: …` on a bad call, so the agent self‑corrects instead of crashing.
- **Make it a skill.** Encode the rule — *call `project_info` first, act in small steps, read back each change, report one PASS/FAIL* — and even a small local model becomes a reliable editor operator.

## Recommended: the UE Engineer skill
The repo ships **[`UE_ENGINEER.md`](UE_ENGINEER.md)** — a small, curated **agent skill** you load as your
agent's system prompt *before* it touches the editor. It's the difference between a reliable operator and a
guesser, and it's the fastest way to get value out of this bridge.

**Why it matters.** `run_python` is unlimited power — which is exactly how an agent makes a mess: ten
half-checked actions in one script, no idea what actually changed. The skill imposes the discipline a
senior UE engineer actually works by: **recon → one small action → read it back → verify → one PASS/FAIL
verdict.** With it loaded, even a small local model becomes a dependable editor operator.

**How it was curated** — distilled from UE 5.7 state-of-the-art practice, not generic prompt fluff:
- **Data-driven by default** — new stats / abilities / economy go in DataTables, Structs, DataAssets, never hardcoded.
- **Shippability awareness** — it *flags* Experimental tech (Mass net replication, Nanite skeletal/foliage, Mover) instead of silently shipping on it.
- **Stability habits** — keep actions small (they run on the game thread), prefer the guarded helpers, save only when you mean to.

**How it drives this bridge.** It maps that loop onto the exact surface: call `project_info` first to
confirm the real editor, reach for the **8 native tools** for the common moves, drop to `run_python` for
everything else — with copy-paste recipes for spawning, asset discovery, Blueprint/Material creation, and
reading every change back. It's the on-ramp that turns OdysseusBridge from *"an API"* into *"an agent that
ships work."*

## Security (opt-in + locked down)
- **Loopback-only** — requests from anything other than `127.0.0.1` / `::1` are rejected (403).
- **Per-session bearer token** — generated at startup, written to
  `<Project>/Saved/OdysseusBridge/auth_token` (local only); every `/mcp` call must send
  `Authorization: Bearer <token>` or it returns 401.
- Inert unless you install + enable the plugin. Keep your project under version control so edits stay reviewable.

## Verify
```bash
python smoke_test.py 8762 <token>
# health -> initialize -> tools/list -> project_info -> run_python
```

## Add a tool
In `OdysseusBridge/Source/OdysseusBridge/Private/OdysseusBridge.cpp`:
1. add the schema to `HandleToolsList()` (name + description + inputSchema),
2. add an `else if (Tool == …)` branch in `HandleToolsCall()`,
3. rebuild. New module deps → `OdysseusBridge.Build.cs`; new plugin deps → `OdysseusBridge.uplugin`.

HTTP handlers run on the game thread, so editor/Python calls are safe.

## Requirements
- Unreal Engine 5.7 (Win64)
- Visual Studio 2022 with the C++ game-dev workload (to compile the plugin)

## Setup, verify, troubleshoot
See **`SETUP.md`** — prerequisites (incl. the .NET 4.8 Dev Pack gotcha), build, connect, verify, and a troubleshooting table so it runs without surprises.

## Built on appleweed/UnrealMCPBridge
OdysseusBridge **improves on [appleweed/UnrealMCPBridge](https://github.com/appleweed/UnrealMCPBridge)** (MIT) —
the project that pioneered the in‑editor plugin‑as‑MCP‑server for Unreal. It keeps that core idea and adds:
- **Standard MCP over HTTP + JSON‑RPC 2.0** instead of a raw socket — works with any compliant MCP client.
- **Security** — loopback‑only + a per‑session bearer token on every call.
- **A guarded helper toolkit** (`unreal_helpers.py`, 13 functions) so the agent doesn't fumble the raw API.
- **An Odysseus connector** + an optional **`UE_ENGINEER.md`** skill to drive it well.

Thanks to appleweed for the groundwork.

## Credits
MIT — see `LICENSE`. Built on the **open** Model Context Protocol (Anthropic) and Unreal's **public** plugin / HTTP / Python APIs (Epic Games) — see **`CREDITS.md`** for exactly what it uses.

---
thanks for everything — use it, fork it, make something good. 🤍

© 2026 d/ay/eşil — Anıl

<sub>Canım abim, Deniz'im adına / For my dear brother, my Deniz.</sub>
