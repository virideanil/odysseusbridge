# Setup — get OdysseusBridge running with no surprises

## Prerequisites
- **Unreal Engine 5.7** (Win64).
- **Visual Studio 2022** with the **"Game development with C++"** workload.
- **.NET Framework 4.8 Developer Pack** — UE 5.7 C++ builds need the *standalone* pack; the VS2022 ".NET desktop" component alone often doesn't register `NETFXSDK`. Install: `winget install Microsoft.DotNet.Framework.DeveloperPack_4`. Without it the build fails with `NETFXSDK ... not found`.
- A **C++ UE project** is simplest. A Blueprint-only project must be converted to C++ first (add any C++ class once, or build via the `.uproject`) so it can compile the module.
- The **Python Editor Script Plugin** — the `.uplugin` already depends on it, so enabling OdysseusBridge pulls it in.

## Install
1. Copy `OdysseusBridge/` into `<YourProject>/Plugins/OdysseusBridge/`.
2. Enable it in `<YourProject>.uproject`:
   ```json
   "Plugins": [ { "Name": "OdysseusBridge", "Enabled": true } ]
   ```
3. Build the editor target (editor CLOSED):
   ```powershell
   & "<UE>\Engine\Build\BatchFiles\Build.bat" <Project>Editor Win64 Development `
     -Project="<...>\<YourProject>.uproject" -WaitMutex
   ```
   …or just open the project — UE offers to compile the missing module.

## Run
Open the editor. The Output Log shows:
```
OdysseusBridge MCP listening at http://127.0.0.1:8762/mcp
OdysseusBridge auth token written to <Project>/Saved/OdysseusBridge/auth_token
```

## Connect (pick your client)
- **Claude Code** — `.mcp.json` at your workspace root (token from the file above):
  ```json
  { "mcpServers": { "ody-bridge": { "type": "http", "url": "http://127.0.0.1:8762/mcp",
    "headers": { "Authorization": "Bearer <token>" } } } }
  ```
- **Odysseus** — `python scripts/connect_unreal.py --token-file "<Project>/Saved/OdysseusBridge/auth_token"`.
- **Any MCP HTTP client** — URL + `Authorization: Bearer <token>`.

## Verify (proves it works)
```bash
python smoke_test.py 8762 <token>      # health → initialize → tools/list → project_info → run_python
```
A real call:
```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"run_python",
 "arguments":{"script":"import unreal; print(unreal.SystemLibrary.get_engine_version())"}}}
```

## Troubleshooting
| Symptom | Cause / fix |
|---|---|
| `NETFXSDK ... not found` at build | Install the .NET Framework 4.8 Developer Pack (Prerequisites). |
| `401 unauthorized` | Wrong/stale token. It's regenerated every editor launch — re-read `<Project>/Saved/OdysseusBridge/auth_token`. |
| `403 loopback only` | Connecting from another machine/IP. By design only `127.0.0.1`/`::1` is allowed. |
| `Python is not available` from run_python | Python Editor Script Plugin not enabled/initialized → enable it, restart the editor. |
| Port 8762 in use / won't bind | Set `ODYSSEUS_BRIDGE_PORT` before launching the editor; check nothing else holds the port. |
| No "listening" log line | Module didn't load/bind — check the Output Log for `LogOdysseusBridge` and that the plugin built. |
| Build fails after editing the `.cpp` | Build with the editor CLOSED (Live Coding locks the binary). |

## Security
Loopback-only + a per-session bearer token under `<Project>/Saved/` (keep `Saved/` gitignored). `run_python` executes in the live editor, so treat the token like a local key and keep the project under version control.
