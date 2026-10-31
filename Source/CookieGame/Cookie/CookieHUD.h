#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CookieHUD.generated.h"

/** Minimal canvas HUD: cookie total on top, a crosshair, and the selected item at the bottom. */
UCLASS()
class COOKIEGAME_API ACookieHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawCentered(const FString& Text, const FLinearColor& Color, float Y, float Scale);
};
