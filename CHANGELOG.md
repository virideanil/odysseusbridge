# Changelog

Notable changes to OdysseusBridge. Loosely follows Keep a Changelog.

## 0.2.0 — 2026-06-06
Reframed **Odysseus-first**: the README and plugin description now lead with Odysseus as the agent that
drives the editor, and promotional phrasing was removed from the docs (kept factual). No behaviour change
— same eight native tools, ~454 lines of C++, no external deps. *(Planned: data-driven tool dispatch to
trim the C++ further.)*

## 0.1.0 — 2026-06-05
First public release — a minimal in-editor MCP server for Unreal Engine 5.7.

### Added
- **In-editor MCP server** over loopback HTTP (JSON-RPC 2.0): `initialize`, `tools/list`, `tools/call`, `ping`.
- **Tools (8 native):** `project_info`, `run_python` (full `unreal` API), `list_assets`, `spawn_actor`, `datatable_rows`, `create_blueprint`, `create_material`, `import_asset` — all over `IPythonScriptPlugin` via one shared exec helper; return captured output + result/traceback.
- **Runtime-verified live (UE 5.7):** health, initialize, tools/list, `project_info`, `run_python`, `list_assets`, `spawn_actor`, `create_blueprint`, `create_material`, and `import_asset` (imported a PNG → Texture2D, confirmed in the content browser). `datatable_rows` builds + loads (standard `get_data_table_row_names`); not exercised against a real DataTable in testing.
- **Security:** loopback-only (127.0.0.1 / ::1) + per-session bearer token written to `<Project>/Saved/OdysseusBridge/auth_token`.
- **Connectors:** `scripts/connect_unreal.py` (Odysseus), a `.mcp.json` recipe (Claude Code), and `smoke_test.py` (end-to-end check).
- **`unreal_helpers.py`** — optional curated wrappers over `run_python` (project / actors / spawn / screenshot / datatable rows / save) so agents don't fumble the raw `unreal` API; each call is guarded (returns `ERR: …`, never a crash).
- **CREDITS.md** — what it's built on: the open MCP spec, Unreal's public APIs, JSON-RPC.

### Engineering notes (honest)
- The C++ (~415-line `.cpp` + 39-line header) is a small, self-contained implementation written from the public Epic APIs + the open MCP / JSON-RPC specs.
- Hardened: full RFC-8259 JSON escaping (control bytes → `\u00XX`); JSON-RPC ids echoed verbatim (number or string); MCP `protocolVersion` negotiation; `-32700` / `-32601` errors; notification handling.
- **Not yet compiled in CI** — review-hardened + structurally checked (balanced, careful UE 5.7 API). **Build-verify in your editor** (UE 5.7 + VS2022). The Python helpers are likewise editor-verify, not CI-tested.
