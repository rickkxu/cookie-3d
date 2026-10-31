#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CookieTypes.h"
#include "CookieHUD.generated.h"

class ACookieGameMode;

/** Minimal canvas HUD: cookie total and per-second rate on top, a crosshair, and the item hotbar at the bottom. */
UCLASS()
class COOKIEGAME_API ACookieHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawHotbar(const ACookieGameMode& GameMode, ECookieItem SelectedItem);
	void DrawCentered(const FString& Text, const FLinearColor& Color, float Y, float Scale);
};
