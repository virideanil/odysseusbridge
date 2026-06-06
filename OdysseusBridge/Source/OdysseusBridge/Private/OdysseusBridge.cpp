// Canım abim, Deniz'im adına / For my dear brother, my Deniz.
//
// OdysseusBridge — an in-editor Model Context Protocol server for Unreal Engine.
// Assembled only from public engine modules and open specifications:
//   * HTTPServer         (Epic)      — the loopback listener
//   * Json               (Epic)      — request parsing + id handling
//   * PythonScriptPlugin (Epic)      — running the `unreal` Python API
//   * Model Context Protocol (Anthropic) + JSON-RPC 2.0 + RFC 8259 — the wire format
// See CREDITS.md for what it's built on.

#include "OdysseusBridge.h"

#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "HttpPath.h"
#include "IPythonScriptPlugin.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "Misc/EngineVersion.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "IPAddress.h"

IMPLEMENT_MODULE(FOdysseusBridgeModule, OdysseusBridge)

DEFINE_LOG_CATEGORY_STATIC(LogOdyBridge, Log, All);

namespace
{
	constexpr uint32 GDefaultPort = 8762;

	// ODYSSEUS_BRIDGE_PORT when it names a valid TCP port, else the default.
	uint32 PickPort()
	{
		const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("ODYSSEUS_BRIDGE_PORT"));
		const int32 Value = Env.IsEmpty() ? 0 : FCString::Atoi(*Env);
		return (Value > 0 && Value <= 65535) ? static_cast<uint32>(Value) : GDefaultPort;
	}

	// RFC 8259 string escaping — the named escapes plus every control byte (< 0x20)
	// as \u00XX, so editor output carrying a stray control character can never make
	// the JSON reply invalid.
	FString Escape(const FString& Raw)
	{
		FString Out;
		Out.Reserve(Raw.Len() + 8);
		for (const TCHAR Ch : Raw)
		{
			switch (Ch)
			{
			case TEXT('\"'): Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\b'): Out += TEXT("\\b");  break;
			case TEXT('\f'): Out += TEXT("\\f");  break;
			case TEXT('\n'): Out += TEXT("\\n");  break;
			case TEXT('\r'): Out += TEXT("\\r");  break;
			case TEXT('\t'): Out += TEXT("\\t");  break;
			default:
				if (Ch < 0x20) { Out += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(Ch)); }
				else           { Out.AppendChar(Ch); }
			}
		}
		return Out;
	}

	TUniquePtr<FHttpServerResponse> JsonReply(const FString& Payload)
	{
		return FHttpServerResponse::Create(Payload, TEXT("application/json"));
	}

	TUniquePtr<FHttpServerResponse> ErrorReply(EHttpServerResponseCodes Code, const TCHAR* Why)
	{
		TUniquePtr<FHttpServerResponse> R = FHttpServerResponse::Create(
			FString::Printf(TEXT("{\"error\":\"%s\"}"), Why), TEXT("application/json"));
		R->Code = Code;
		return R;
	}

	// First value of the named header (case-insensitive), or empty.
	FString HeaderValue(const FHttpServerRequest& Req, const TCHAR* Key)
	{
		for (const TPair<FString, TArray<FString>>& H : Req.Headers)
		{
			if (H.Key.Equals(Key, ESearchCase::IgnoreCase) && H.Value.Num() > 0)
			{
				return H.Value[0];
			}
		}
		return FString();
	}

	// True only when the peer is provably off this machine; an unknown peer is left
	// for the token check to judge.
	bool PeerIsRemote(const FHttpServerRequest& Req)
	{
		if (!Req.PeerAddress.IsValid()) { return false; }
		const FString Addr = Req.PeerAddress->ToString(false);
		return Addr != TEXT("127.0.0.1") && Addr != TEXT("::1") && !Addr.Contains(TEXT("127.0.0.1"));
	}

	// The JSON-RPC id serialized verbatim (number or quoted string), so the reply
	// echoes the client exactly. Empty => no id, i.e. a notification.
	FString IdAsJson(const TSharedPtr<FJsonObject>& Root)
	{
		const TSharedPtr<FJsonValue> Id = Root->TryGetField(TEXT("id"));
		if (!Id.IsValid()) { return FString(); }
		if (Id->Type == EJson::String)
		{
			return FString::Printf(TEXT("\"%s\""), *Escape(Id->AsString()));
		}
		if (Id->Type == EJson::Number)
		{
			const double N = Id->AsNumber();
			return (N == FMath::TruncToDouble(N)) ? FString::Printf(TEXT("%lld"), static_cast<int64>(N))
			                                      : FString::SanitizeFloat(N);
		}
		return FString();
	}

	// Decode the UTF-8 request body to an FString.
	FString BodyText(const FHttpServerRequest& Req)
	{
		if (Req.Body.Num() == 0) { return FString(); }
		const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Req.Body.GetData()), Req.Body.Num());
		return FString(Conv.Length(), Conv.Get());
	}

	// Run a Python script in the live editor; returns ok/result/output text, sets bFailed.
	// Shared by run_python and the native typed tools below.
	FString RunEditorPython(const FString& Script, bool& bFailed)
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		if (Python == nullptr || !Python->IsPythonAvailable())
		{
			bFailed = true;
			return TEXT("Python is unavailable (PythonScriptPlugin not loaded).");
		}
		// /mcp handlers run on the game thread, which is where UE Python must run.
		FPythonCommandEx Command;
		Command.ExecutionMode      = EPythonCommandExecutionMode::ExecuteFile;
		Command.FileExecutionScope = EPythonFileExecutionScope::Private;
		Command.Command            = Script;
		const bool bRan = Python->ExecPythonCommandEx(Command);
		FString Captured;
		for (const FPythonLogOutputEntry& Line : Command.LogOutput)
		{
			Captured += Line.Output;
			if (!Line.Output.EndsWith(TEXT("\n"))) { Captured += TEXT("\n"); }
		}
		bFailed = !bRan;
		return FString::Printf(TEXT("ok=%s\nresult=%s\n--- output ---\n%s"),
			bRan ? TEXT("true") : TEXT("false"), *Command.CommandResult, *Captured);
	}
}

void FOdysseusBridgeModule::StartupModule()  { OpenEndpoint(); }
void FOdysseusBridgeModule::ShutdownModule() { CloseEndpoint(); }

void FOdysseusBridgeModule::OpenEndpoint()
{
	BoundPort = PickPort();

	// Mint a per-run secret and write it where only a same-machine process can read
	// it. Every /mcp call must present it; with the loopback check the endpoint is
	// unreachable off-box and unusable without the token.
	SessionToken = FGuid::NewGuid().ToString(EGuidFormats::Digits)
	             + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("OdysseusBridge");
	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/ true);
	const FString TokenFile = Dir / TEXT("auth_token");
	if (!FFileHelper::SaveStringToFile(SessionToken, *TokenFile))
	{
		UE_LOG(LogOdyBridge, Warning, TEXT("OdysseusBridge: could not write %s"), *TokenFile);
	}

	FHttpServerModule& Server = FModuleManager::LoadModuleChecked<FHttpServerModule>(TEXT("HTTPServer"));
	Listener = Server.GetHttpRouter(BoundPort);
	if (!Listener.IsValid())
	{
		UE_LOG(LogOdyBridge, Error, TEXT("OdysseusBridge: could not bind port %u"), BoundPort);
		return;
	}

	HealthRoute = Listener->BindRoute(
		FHttpPath(TEXT("/odysseus/health")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([](const FHttpServerRequest& In, const FHttpResultCallback& Reply)
		{
			if (PeerIsRemote(In))
			{
				Reply(ErrorReply(EHttpServerResponseCodes::Forbidden, TEXT("loopback only")));
				return true;
			}
			Reply(JsonReply(TEXT("{\"ok\":true,\"server\":\"OdysseusBridge\",\"version\":\"0.1.0\"}")));
			return true;
		}));

	McpRoute = Listener->BindRoute(
		FHttpPath(TEXT("/mcp")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FOdysseusBridgeModule::ServeMcp));

	Server.StartAllListeners();
	UE_LOG(LogOdyBridge, Log, TEXT("OdysseusBridge MCP listening at http://127.0.0.1:%u/mcp"), BoundPort);
}

void FOdysseusBridgeModule::CloseEndpoint()
{
	if (Listener.IsValid())
	{
		if (McpRoute.IsValid())    { Listener->UnbindRoute(McpRoute); }
		if (HealthRoute.IsValid()) { Listener->UnbindRoute(HealthRoute); }
	}
	McpRoute.Reset();
	HealthRoute.Reset();
	Listener.Reset();
}

bool FOdysseusBridgeModule::ServeMcp(const FHttpServerRequest& In, const FHttpResultCallback& Reply)
{
	// Two-part gate, default-safe: the caller must be on this machine AND present the
	// per-run token. Neither alone admits a request.
	if (PeerIsRemote(In))
	{
		Reply(ErrorReply(EHttpServerResponseCodes::Forbidden, TEXT("loopback only")));
		return true;
	}
	{
		const FString Bearer = HeaderValue(In, TEXT("Authorization"));
		const FString Plain  = HeaderValue(In, TEXT("X-Odysseus-Bridge-Token"));
		const bool bAuthorized = !SessionToken.IsEmpty()
			&& (Bearer == FString::Printf(TEXT("Bearer %s"), *SessionToken) || Plain == SessionToken);
		if (!bAuthorized)
		{
			Reply(ErrorReply(EHttpServerResponseCodes::Denied, TEXT("unauthorized")));
			return true;
		}
	}

	// Parse the JSON-RPC envelope.
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyText(In));
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		// Unparseable → JSON-RPC -32700; the id is unknowable, so null.
		Reply(JsonReply(TEXT("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}")));
		return true;
	}

	const FString Id = IdAsJson(Root);
	if (Id.IsEmpty())   // notification: accept silently, no result.
	{
		Reply(JsonReply(TEXT("{}")));
		return true;
	}

	FString Method;
	Root->TryGetStringField(TEXT("method"), Method);

	FString Result;
	FString Error;
	if      (Method == TEXT("initialize")) { Result = HandleInitialize(Root); }
	else if (Method == TEXT("tools/list")) { Result = HandleToolsList(); }
	else if (Method == TEXT("tools/call")) { Result = HandleToolsCall(Root); }
	else if (Method == TEXT("ping"))       { Result = TEXT("{}"); }
	else                                   { Error  = FString::Printf(TEXT("Method not found: %s"), *Method); }

	const FString Envelope = Error.IsEmpty()
		? FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":%s}"), *Id, *Result)
		: FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":-32601,\"message\":\"%s\"}}"),
			*Id, *Escape(Error));
	Reply(JsonReply(Envelope));
	return true;
}

FString FOdysseusBridgeModule::HandleInitialize(const TSharedPtr<FJsonObject>& Root) const
{
	// Echo the client's protocolVersion if it offered one (MCP version negotiation),
	// otherwise advertise a recent stable revision.
	FString Proto = TEXT("2025-06-18");
	const TSharedPtr<FJsonObject>* Params = nullptr;
	if (Root->TryGetObjectField(TEXT("params"), Params) && Params)
	{
		FString Offered;
		if ((*Params)->TryGetStringField(TEXT("protocolVersion"), Offered) && !Offered.IsEmpty())
		{
			Proto = Offered;
		}
	}
	return FString::Printf(
		TEXT("{\"protocolVersion\":\"%s\",\"capabilities\":{\"tools\":{\"listChanged\":false}},")
		TEXT("\"serverInfo\":{\"name\":\"OdysseusBridge\",\"version\":\"0.1.0\"}}"),
		*Escape(Proto));
}

FString FOdysseusBridgeModule::HandleToolsList() const
{
	// Eight native tools; all share RunEditorPython, so more typed tools can be added
	// with no new transport.
	return
		TEXT("{\"tools\":[")
		TEXT("{\"name\":\"project_info\",")
		TEXT("\"description\":\"Name, absolute directory and engine version of the open Unreal project.\",")
		TEXT("\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},")
		TEXT("{\"name\":\"run_python\",")
		TEXT("\"description\":\"Run Python inside the live Unreal Editor (the `unreal` module is in scope) and return its captured output plus result/traceback. The full `unreal` editor API is reachable - spawn actors, edit DataTables, import assets, run commandlets, etc. - you write the Python.\",")
		TEXT("\"inputSchema\":{\"type\":\"object\",\"properties\":{\"script\":{\"type\":\"string\",\"description\":\"Python source to run in the editor.\"}},\"required\":[\"script\"],\"additionalProperties\":false}},")
		TEXT("{\"name\":\"list_assets\",")
		TEXT("\"description\":\"List asset paths under a content folder (recursive).\",")
		TEXT("\"inputSchema\":{\"type\":\"object\",\"properties\":{\"folder\":{\"type\":\"string\",\"description\":\"Content path, e.g. /Game.\"}},\"additionalProperties\":false}},")
		TEXT("{\"name\":\"spawn_actor\",")
		TEXT("\"description\":\"Spawn N StaticMeshActors with the given mesh, in a row.\",")
		TEXT("\"inputSchema\":{\"type\":\"object\",\"properties\":{\"mesh_path\":{\"type\":\"string\",\"description\":\"Mesh asset path, e.g. /Engine/BasicShapes/Cube.Cube.\"},\"count\":{\"type\":\"integer\",\"description\":\"How many (default 1).\"}},\"required\":[\"mesh_path\"],\"additionalProperties\":false}},")
		TEXT("{\"name\":\"datatable_rows\",")
		TEXT("\"description\":\"List the row names of a DataTable asset.\",")
		TEXT("\"inputSchema\":{\"type\":\"object\",\"properties\":{\"asset_path\":{\"type\":\"string\",\"description\":\"DataTable asset path.\"}},\"required\":[\"asset_path\"],\"additionalProperties\":false}},")
		TEXT("{\"name\":\"create_blueprint\",")
		TEXT("\"description\":\"Create a Blueprint asset with the given parent class.\",")
		TEXT("\"inputSchema\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"path\":{\"type\":\"string\",\"description\":\"Content path, default /Game.\"},\"parent_class\":{\"type\":\"string\",\"description\":\"unreal class name, default Actor.\"}},\"required\":[\"name\"],\"additionalProperties\":false}},")
		TEXT("{\"name\":\"create_material\",")
		TEXT("\"description\":\"Create a new Material asset.\",")
		TEXT("\"inputSchema\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"path\":{\"type\":\"string\",\"description\":\"Content path, default /Game.\"}},\"required\":[\"name\"],\"additionalProperties\":false}},")
		TEXT("{\"name\":\"import_asset\",")
		TEXT("\"description\":\"Import a file (FBX, image, etc.) into the content browser.\",")
		TEXT("\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\",\"description\":\"Absolute file path on disk.\"},\"destination\":{\"type\":\"string\",\"description\":\"Content path, default /Game.\"}},\"required\":[\"source\"],\"additionalProperties\":false}}")
		TEXT("]}");
}

FString FOdysseusBridgeModule::HandleToolsCall(const TSharedPtr<FJsonObject>& Root) const
{
	FString Tool;
	TSharedPtr<FJsonObject> Arguments;
	const TSharedPtr<FJsonObject>* Params = nullptr;
	if (Root->TryGetObjectField(TEXT("params"), Params) && Params)
	{
		(*Params)->TryGetStringField(TEXT("name"), Tool);
		const TSharedPtr<FJsonObject>* ArgsPtr = nullptr;
		if ((*Params)->TryGetObjectField(TEXT("arguments"), ArgsPtr) && ArgsPtr)
		{
			Arguments = *ArgsPtr;
		}
	}

	FString Text;
	bool bFailed = false;

	if (Tool == TEXT("project_info"))
	{
		Text = FString::Printf(TEXT("Project: %s\nDir: %s\nEngine: %s"),
			FApp::GetProjectName(),
			*FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()),
			*FEngineVersion::Current().ToString());
	}
	else if (Tool == TEXT("run_python"))
	{
		FString Script;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("script"), Script);
		}

		Text = RunEditorPython(Script, bFailed);
	}
	else if (Tool == TEXT("list_assets"))
	{
		FString Folder = TEXT("/Game");
		if (Arguments.IsValid()) { Arguments->TryGetStringField(TEXT("folder"), Folder); }
		const FString Py = FString::Printf(
			TEXT("import unreal\nfor p in unreal.EditorAssetLibrary.list_assets('%s', True, False):\n    print(p)"),
			*Folder);
		Text = RunEditorPython(Py, bFailed);
	}
	else if (Tool == TEXT("spawn_actor"))
	{
		FString Mesh;
		int32 Count = 1;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("mesh_path"), Mesh);
			Arguments->TryGetNumberField(TEXT("count"), Count);
		}
		if (Count < 1) { Count = 1; }
		const FString Py = FString::Printf(
			TEXT("import unreal\nm=unreal.load_asset('%s')\neas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\nn=0\nfor i in range(%d):\n    a=eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(i*200.0,0,0), unreal.Rotator())\n    if a and m:\n        a.static_mesh_component.set_static_mesh(m)\n        n+=1\nprint('spawned', n, 'of', %d)"),
			*Mesh, Count, Count);
		Text = RunEditorPython(Py, bFailed);
	}
	else if (Tool == TEXT("datatable_rows"))
	{
		FString Path;
		if (Arguments.IsValid()) { Arguments->TryGetStringField(TEXT("asset_path"), Path); }
		const FString Py = FString::Printf(
			TEXT("import unreal\ndt=unreal.load_asset('%s')\nprint(', '.join(str(x) for x in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)))"),
			*Path);
		Text = RunEditorPython(Py, bFailed);
	}
	else if (Tool == TEXT("create_blueprint"))
	{
		FString Name = TEXT("NewBlueprint"), Path = TEXT("/Game"), Parent = TEXT("Actor");
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("name"), Name);
			Arguments->TryGetStringField(TEXT("path"), Path);
			Arguments->TryGetStringField(TEXT("parent_class"), Parent);
		}
		const FString Py = FString::Printf(
			TEXT("import unreal\nf=unreal.BlueprintFactory()\nf.set_editor_property('parent_class', getattr(unreal,'%s',unreal.Actor))\nbp=unreal.AssetToolsHelpers.get_asset_tools().create_asset('%s','%s',unreal.Blueprint,f)\nunreal.EditorAssetLibrary.save_loaded_asset(bp)\nprint(bp.get_path_name() if bp else 'FAILED')"),
			*Parent, *Name, *Path);
		Text = RunEditorPython(Py, bFailed);
	}
	else if (Tool == TEXT("create_material"))
	{
		FString Name = TEXT("NewMaterial"), Path = TEXT("/Game");
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("name"), Name);
			Arguments->TryGetStringField(TEXT("path"), Path);
		}
		const FString Py = FString::Printf(
			TEXT("import unreal\nm=unreal.AssetToolsHelpers.get_asset_tools().create_asset('%s','%s',unreal.Material,unreal.MaterialFactoryNew())\nunreal.EditorAssetLibrary.save_loaded_asset(m)\nprint(m.get_path_name() if m else 'FAILED')"),
			*Name, *Path);
		Text = RunEditorPython(Py, bFailed);
	}
	else if (Tool == TEXT("import_asset"))
	{
		FString Source, Dest = TEXT("/Game");
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("source"), Source);
			Arguments->TryGetStringField(TEXT("destination"), Dest);
		}
		const FString Py = FString::Printf(
			TEXT("import unreal\nt=unreal.AssetImportTask()\nt.set_editor_property('filename', r'%s')\nt.set_editor_property('destination_path', '%s')\nt.set_editor_property('automated', True)\nt.set_editor_property('replace_existing', True)\nt.set_editor_property('save', True)\nunreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])\nprint('imported:', list(t.get_editor_property('imported_object_paths')))"),
			*Source, *Dest);
		Text = RunEditorPython(Py, bFailed);
	}
	else
	{
		Text = FString::Printf(TEXT("Unknown tool: %s"), *Tool);
		bFailed = true;
	}

	return FString::Printf(
		TEXT("{\"content\":[{\"type\":\"text\",\"text\":\"%s\"}],\"isError\":%s}"),
		*Escape(Text), bFailed ? TEXT("true") : TEXT("false"));
}
