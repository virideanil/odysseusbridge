#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IHttpRouter.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"

class FJsonObject;

/**
 * OdysseusBridge — a Model Context Protocol server that runs inside the Unreal
 * Editor. It is assembled only from public engine modules (HTTPServer, Json,
 * PythonScriptPlugin) and the open MCP + JSON-RPC 2.0 specifications; see
 * CREDITS.md. Any MCP client (Odysseus, Claude Code, Cursor, your own) connects
 * over loopback and drives the live editor.
 *
 *   POST http://127.0.0.1:<ODYSSEUS_BRIDGE_PORT | 8762>/mcp
 *   GET  /odysseus/health
 */
class FOdysseusBridgeModule final : public IModuleInterface
{
public:
	void StartupModule() override;
	void ShutdownModule() override;

private:
	// Bring the loopback HTTP listener up / take it down.
	void OpenEndpoint();
	void CloseEndpoint();

	/** POST /mcp — one JSON-RPC request in, one reply out (loopback + token gated). */
	bool ServeMcp(const FHttpServerRequest& In, const FHttpResultCallback& Reply);

	/** Per-method result builders — the MCP capability surface. */
	FString HandleInitialize(const TSharedPtr<FJsonObject>& Root) const;
	FString HandleToolsList() const;
	FString HandleToolsCall(const TSharedPtr<FJsonObject>& Root) const;

	TSharedPtr<IHttpRouter> Listener;
	FHttpRouteHandle McpRoute;
	FHttpRouteHandle HealthRoute;
	uint32 BoundPort = 0;
	/** Per-run bearer secret; also written under <Project>/Saved/OdysseusBridge/auth_token. */
	FString SessionToken;
};
