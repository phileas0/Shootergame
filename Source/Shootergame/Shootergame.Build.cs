using UnrealBuildTool;

public class Shootergame : ModuleRules
{
    public Shootergame(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            // File I/O for CSV logging
            "ApplicationCore",
        });
    }
}
