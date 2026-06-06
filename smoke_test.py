#!/usr/bin/env python3
"""End-to-end smoke test for OdysseusBridge — run with the UE editor open.

Hits the raw MCP/HTTP endpoint directly (no client lib needed):
  health -> initialize -> tools/list -> project_info -> run_python

The bridge is loopback-only and token-gated, so pass the per-session token
(contents of <UEProject>/Saved/OdysseusBridge/auth_token):

    python smoke_test.py [port] [token]
    # or:  ODYSSEUS_BRIDGE_TOKEN=<hex> python smoke_test.py
"""
import json
import os
import sys
import urllib.request

PORT = sys.argv[1] if len(sys.argv) > 1 else "8762"
TOKEN = (sys.argv[2] if len(sys.argv) > 2 else None) or os.environ.get("ODYSSEUS_BRIDGE_TOKEN")
BASE = f"http://127.0.0.1:{PORT}"


def post(method, params=None, rid=1):
    body = {"jsonrpc": "2.0", "id": rid, "method": method}
    if params is not None:
        body["params"] = params
    headers = {"Content-Type": "application/json", "Accept": "application/json"}
    if TOKEN:
        headers["Authorization"] = f"Bearer {TOKEN}"
    req = urllib.request.Request(BASE + "/mcp", data=json.dumps(body).encode("utf-8"),
                                headers=headers, method="POST")
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read().decode("utf-8"))


def get(path):  # health is loopback-only, no token needed
    with urllib.request.urlopen(BASE + path, timeout=5) as r:
        return r.read().decode("utf-8")


def main():
    print(f"[smoke] {BASE}  (token={'set' if TOKEN else 'MISSING'})")
    print("[health]      ", get("/odysseus/health"))
    print("[initialize]  ", json.dumps(post("initialize", {
        "protocolVersion": "2024-11-05", "capabilities": {},
        "clientInfo": {"name": "smoke", "version": "0"}}))[:200])
    tl = post("tools/list", rid=2)
    print("[tools]       ", [t.get("name") for t in tl.get("result", {}).get("tools", [])])
    print("[project_info]", json.dumps(post("tools/call",
        {"name": "project_info", "arguments": {}}, rid=3))[:300])
    print("[run_python]  ", json.dumps(post("tools/call", {"name": "run_python", "arguments": {
        "script": "import unreal\nprint('hello from UE', unreal.SystemLibrary.get_engine_version())"}}, rid=4))[:500])
    print("[smoke] OK")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"[smoke] FAILED: {exc}\n  Editor open? Port right? Token correct (loopback + bearer required)?")
        sys.exit(1)
