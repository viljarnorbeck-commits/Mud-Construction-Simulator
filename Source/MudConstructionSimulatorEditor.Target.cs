using UnrealBuildTool;

public class MudConstructionSimulatorEditorTarget : TargetRules
{
    public MudConstructionSimulatorEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("MudConstructionSimulator");
    }
}
