# Credits & thanks

OdysseusBridge is built on open standards — these are the things it actually uses, nothing more.

## Built on
- **Model Context Protocol (MCP)** — open-sourced by **Anthropic**. The open standard OdysseusBridge
  speaks; it's just an MCP server living inside the Unreal editor. · https://modelcontextprotocol.io
- **Unreal Engine** — **Epic Games**. Built with the public engine modules — `FHttpServerModule`,
  `IPythonScriptPlugin`, and the plugin system — like any UE plugin.
- **JSON-RPC 2.0** — the open RPC standard MCP rides on. · https://www.jsonrpc.org/specification

## Improves on
- **[appleweed/UnrealMCPBridge](https://github.com/appleweed/UnrealMCPBridge)** (MIT) — the project that
  pioneered the in-editor plugin-as-MCP-server for Unreal. OdysseusBridge builds on that idea and adds
  standard MCP-over-HTTP / JSON-RPC, per-session token auth, and a guarded helper toolkit. Thanks for the
  groundwork.

## Where each part comes from
Honest provenance — every piece traced to its origin:
- **The plugin** (in-editor HTTP server, JSON parsing, editor Python execution) — Unreal's own public
  modules: `FHttpServerModule`, `Json`, `IPythonScriptPlugin`, the plugin system (**Epic Games**).
- **The wire format** — the open **Model Context Protocol** (Anthropic) over **JSON-RPC 2.0**, with
  **RFC 8259** string escaping.
- **The security model** (loopback-only + per-session bearer token) — the standard local-MCP safety pattern.
- **`unreal_helpers.py`** — thin guarded wrappers over the public `unreal` editor API.
- **`UE_ENGINEER.md`** — distilled from our own **UE 5.7 state-of-the-art game-developer discipline**
  (right-system selection, shippability tiers, data-driven design) + the recon → act → read-back → verify
  operating loop. Curated, not generated.
- **The overall pattern** — an in-editor plugin acting as an MCP server — was pioneered by the projects
  under *Improves on* below; this is an independent, standards-based take on it.

## Thanks to
Built *with* AI, not just by a human — credit where it's due:
- **Claude** (Anthropic) — paired on every line of this.
- Deniz. For everything.

---
thanks for everything 🤍

© 2026 d/ay/eşil — Anıl
