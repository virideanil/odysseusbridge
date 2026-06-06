using UnrealBuildTool;

public class OdysseusBridge : ModuleRules
{
	public OdysseusBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		// HTTPServer = UE's built-in in-editor HTTP server (FHttpServerModule).
		// Json parses the JSON-RPC; PythonScriptPlugin powers run_python; Sockets
		// exposes the request peer address for the loopback-only check.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"HTTPServer",
			"Json",
			"PythonScriptPlugin",
			"Sockets",
		});
	}
}
