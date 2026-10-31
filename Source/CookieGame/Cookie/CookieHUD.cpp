#include "CookieHUD.h"
#include "CookieCharacter.h"
#include "CookieGameMode.h"
#include "CookieGameSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

namespace
{
	FString FormatCount(double Value)
	{
		return FText::AsNumber(FMath::FloorToInt64(Value)).ToString();
	}

	const FLinearColor Affordable = FLinearColor::White;
	const FLinearColor TooExpensive(0.4f, 0.4f, 0.4f);
}

void ACookieHUD::DrawHUD()
{
	Super::DrawHUD();

	const ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>();
	if (!GameMode || !Canvas)
	{
		return;
	}

	DrawRect(FLinearColor::White, Canvas->ClipX * 0.5f - 2.f, Canvas->ClipY * 0.5f - 2.f, 4.f, 4.f);

	DrawCentered(FString::Printf(TEXT("%s cookies"), *FormatCount(GameMode->GetCookies())), FLinearColor::White, 24.f, 2.f);

	// Placeholder for the full hotbar (build step 3): just the selected item.
	if (const ACookieCharacter* Player = Cast<ACookieCharacter>(GetOwningPawn()))
	{
		const ECookieItem Item = Player->GetSelectedItem();
		const FString Text = FString::Printf(TEXT("[Hold RMB] %s    cost %s    owned %d"),
			*UCookieGameSettings::Get().GetItem(Item).DisplayName.ToString(),
			*FormatCount(GameMode->GetCost(Item)),
			GameMode->GetOwned(Item));
		DrawCentered(Text, GameMode->CanAfford(Item) ? Affordable : TooExpensive, Canvas->ClipY - 60.f, 1.5f);
	}
}

void ACookieHUD::DrawCentered(const FString& Text, const FLinearColor& Color, float Y, float Scale)
{
	UFont* Font = GEngine->GetLargeFont();
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, Font, Scale);
	DrawText(Text, Color, (Canvas->ClipX - Width) * 0.5f, Y, Font, Scale);
}
