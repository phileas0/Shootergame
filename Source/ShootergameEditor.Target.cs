using UnrealBuildTool;
using System.Collections.Generic;

public class ShootergameEditorTarget : TargetRules
{
	public ShootergameEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		bOverrideBuildEnvironment = true;
		ExtraModuleNames.Add("Shootergame");
	}
}
