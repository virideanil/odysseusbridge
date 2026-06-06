#!/usr/bin/env python3
"""Register the OdysseusBridge (Unreal Editor) MCP server with Odysseus.

OdysseusBridge is a standalone UE 5.7 editor plugin (drop ``OdysseusBridge/`` into
your project's ``Plugins/``). This connector is the Odysseus-side glue; run it from
your Odysseus repo root (it needs ``src.database``). Other MCP clients use ``.mcp.json``.
While the editor is open it serves an MCP endpoint at
``http://127.0.0.1:<port>/mcp`` (default 8762). This script idempotently
registers an *enabled* ``McpServer`` row so Odysseus auto-connects on startup
(``McpManager.connect_all_enabled``) and exposes the editor's tools to the agent
as ``mcp__<id>__*`` (e.g. ``project_info``, ``run_python``).

Security: the bridge is loopback-only and requires a per-session bearer token
written to ``<UEProject>/Saved/OdysseusBridge/auth_token`` when the editor
starts. Pass it with ``--token-file`` (recommended) or ``--token``; it is stored
in the server row's env and sent as ``Authorization: Bearer`` on connect.

    python scripts/connect_unreal.py --token-file "<Proj>/Saved/OdysseusBridge/auth_token"
    python scripts/connect_unreal.py --port 9001 --token <hex>
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main() -> int:
    ap = argparse.ArgumentParser(description="Register the OdysseusBridge UE MCP server.")
    ap.add_argument("--port", type=int,
                    default=int(os.environ.get("ODYSSEUS_BRIDGE_PORT", "8762")),
                    help="Port the in-editor bridge listens on (default 8762).")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--id", default="odybridge", help="Stable MCP server id (idempotent key).")
    ap.add_argument("--name", default="Unreal Editor (OdysseusBridge)")
    ap.add_argument("--token", default=os.environ.get("ODYSSEUS_BRIDGE_TOKEN"),
                    help="Bearer token (contents of the auth_token file).")
    ap.add_argument("--token-file",
                    help="Path to <UEProject>/Saved/OdysseusBridge/auth_token (read instead of --token).")
    args = ap.parse_args()

    token = args.token
    if not token and args.token_file:
        try:
            with open(args.token_file, "r", encoding="utf-8") as fh:
                token = fh.read().strip()
        except OSError as exc:
            print(f"[connect_unreal] could not read token file {args.token_file}: {exc}", file=sys.stderr)
            return 1
    if not token:
        print("[connect_unreal] WARNING: no token (--token / --token-file / $ODYSSEUS_BRIDGE_TOKEN).\n"
              "  OdysseusBridge requires one — the connection will be rejected (401) without it.",
              file=sys.stderr)

    url = f"http://{args.host}:{args.port}/mcp"

    try:
        from src.database import SessionLocal, McpServer
    except Exception as exc:  # pragma: no cover - setup guard
        print(f"[connect_unreal] could not import the Odysseus DB layer ({exc}).\n"
              "  Run from the repo root after `pip install -r requirements.txt`.",
              file=sys.stderr)
        return 1

    db = SessionLocal()
    try:
        row = db.query(McpServer).filter(McpServer.id == args.id).first()
        action = "updated" if row is not None else "created"
        if row is None:
            row = McpServer(id=args.id)
            db.add(row)
        row.name = args.name
        row.transport = "http"
        row.url = url
        row.command = None
        row.args = "[]"
        row.env = json.dumps({"ODYSSEUS_BRIDGE_TOKEN": token}) if token else "{}"
        row.is_enabled = True
        db.commit()
    finally:
        db.close()

    print(f"[connect_unreal] {action} '{args.name}' -> {url} "
          f"(id={args.id}, transport=http, token={'set' if token else 'MISSING'}, enabled)")
    print("[connect_unreal] Open your UE project (OdysseusBridge installed), then start/restart Odysseus.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
