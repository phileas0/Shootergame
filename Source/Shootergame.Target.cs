using UnrealBuildTool;
using System.Collections.Generic;

public class ShootergameTarget : TargetRules
{
	public ShootergameTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Shootergame");
	}
}
