using UnrealBuildTool;
using System.Collections.Generic;

public class ShootergameServerTarget : TargetRules
{
	public ShootergameServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Shootergame");
	}
}
