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

## Thanks to
Built *with* AI, not just by a human — credit where it's due:
- **Claude** (Anthropic) — paired on every line of this.
- Deniz. For everything.

---
thanks for everything 🤍

© 2026 d/ay/eşil — Anıl
