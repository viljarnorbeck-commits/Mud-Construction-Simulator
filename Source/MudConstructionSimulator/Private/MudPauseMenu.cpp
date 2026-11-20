#include "MudGameMode.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "UnrealClient.h"

FBox2D AMudOperator::PauseButtonBounds(FVector2D ScreenSize, int32 Button)
{
    const double Scale = FMath::Clamp(ScreenSize.Y / 870.0, 0.75, 2.4);
    const FVector2D TopLeft = ScreenSize * 0.5 + FVector2D(-180, -82 + Button * 66) * Scale;
    return FBox2D(TopLeft, TopLeft + FVector2D(360, 54) * Scale);
}

void AMudOperator::TogglePauseMenu()
{
    if (bPauseMenuOpen)
    {
        ClosePauseMenu();
        return;
    }
    auto* PC = Cast<APlayerController>(GetController());
    if (!PC || !GEngine || !GEngine->GameViewport || bInitializeFailed || !PC->SetPause(true))
        return;

    Throttle = Steering = HydraulicA = HydraulicB = HydraulicKeyA = HydraulicKeyB = 0;
    PadHydraulicA = PadHydraulicB = FootPadForward = 0;
    VerticalTap = HorizontalTap = {};
    MouseHydraulics = {};
    bCruise = bSprint = bPadHydraulics = bPadSecondHydraulics = false;
    MessageTime = 0;
    if (PC->PlayerInput)
        PC->PlayerInput->FlushPressedKeys();

    bPauseMenuOpen = true;
    PC->bShowMouseCursor = true;
    PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false)
        .SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
    GEngine->GameViewport->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
    if (GEngine->GameViewport->Viewport)
    {
        const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
        PC->SetMouseLocation(Size.X / 2, Size.Y / 2);
    }
}

void AMudOperator::ClickPauseMenu()
{
    auto* PC = Cast<APlayerController>(GetController());
    if (!bPauseMenuOpen || !PC || !GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
        return;
    float X, Y;
    if (!PC->GetMousePosition(X, Y))
        return;
    const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
    for (int32 Button = 0; Button < 3; ++Button)
    {
        if (!PauseButtonBounds(FVector2D(Size.X, Size.Y), Button).IsInside(FVector2D(X, Y)))
            continue;
        switch (Button)
        {
        case 0:
            ClosePauseMenu();
            break;
        case 1:
            Save();
            break;
        case 2:
            if (WriteProgress())
            {
                bProtectSave = false;
                Quit();
            }
            else
                Notify(TEXT("Kunde inte spara världen. Försök igen."));
            break;
        }
        return;
    }
}

void AMudOperator::ClosePauseMenu()
{
    if (!bPauseMenuOpen)
        return;
    bPauseMenuOpen = false;
    if (auto* PC = Cast<APlayerController>(GetController()))
    {
        if (PC->PlayerInput)
            PC->PlayerInput->FlushPressedKeys();
        PC->SetPause(false);
        PC->bShowMouseCursor = false;
        PC->SetInputMode(FInputModeGameOnly().SetConsumeCaptureMouseDown(false));
        if (GEngine && GEngine->GameViewport)
        {
            GEngine->GameViewport->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
            GEngine->GameViewport->SetMouseLockMode(EMouseLockMode::LockOnCapture);
        }
    }
}
