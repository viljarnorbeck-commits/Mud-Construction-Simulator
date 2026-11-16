using UnrealBuildTool;

public class MudConstructionSimulator : ModuleRules
{
    public MudConstructionSimulator(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "ImageCore" });
    }
}
