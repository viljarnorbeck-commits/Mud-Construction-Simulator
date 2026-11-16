using UnrealBuildTool;

public class MudConstructionSimulatorTarget : TargetRules
{
    public MudConstructionSimulatorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("MudConstructionSimulator");
    }
}
