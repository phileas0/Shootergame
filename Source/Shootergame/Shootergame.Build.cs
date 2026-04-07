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
            "EnhancedInput",      // Für IA_Shoot und Enhanced Input Actions im Blueprint
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "ApplicationCore",    // Platform-Utilities
        });
    }
}
