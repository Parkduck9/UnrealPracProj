// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/**
 * Dedicated server target. Building this produces PYJ_P38Server.exe, the binary that
 * registers itself with the web registry on startup. See Docs/SETUP.md.
 */
public class PYJ_P38ServerTarget : TargetRules
{
	public PYJ_P38ServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("PYJ_P38");
	}
}
