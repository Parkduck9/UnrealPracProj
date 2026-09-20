// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PYJ_P38 : ModuleRules
{
	public PYJ_P38(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Niagara",
			// ServerRegistrySubsystem.h exposes HTTP types, so this one has to be public.
			"HTTP"}
		);

		// Server Registry feature: HTTP talks to the web backend, Json parses its payloads,
		// Sockets resolves the local address to publish, DeveloperSettings backs the
		// Project Settings page. See Docs/ARCHITECTURE.md.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Json",
			"JsonUtilities",
			"Sockets",
			"Networking",
			"DeveloperSettings"}
		);

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
