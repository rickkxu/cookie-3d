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

	// Hotbar layout, in screen pixels.
	const float SlotWidth = 210.f;
	const float SlotHeight = 76.f;
	const float SlotGap = 10.f;
	const float BottomMargin = 24.f;
	const float IconSize = 40.f;
	const float SelectedBorder = 3.f;

	const FLinearColor SlotBackground(0.f, 0.f, 0.f, 0.55f);
	const FLinearColor SelectedOutline(1.f, 0.85f, 0.3f, 1.f);
	const FLinearColor Affordable = FLinearColor::White;
	const FLinearColor TooExpensive(0.4f, 0.4f, 0.4f, 1.f);
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
	DrawCentered(FString::Printf(TEXT("per second: %.1f"), GameMode->GetCookiesPerSecond()), FLinearColor::White, 80.f, 1.f);

	if (const ACookieCharacter* Player = Cast<ACookieCharacter>(GetOwningPawn()))
	{
		DrawHotbar(*GameMode, Player->GetSelectedItem());
	}
}

void ACookieHUD::DrawHotbar(const ACookieGameMode& GameMode, ECookieItem SelectedItem)
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	UFont* Font = GEngine->GetMediumFont();

	const int32 SlotCount = static_cast<int32>(ECookieItem::Count);
	const float TotalWidth = SlotCount * SlotWidth + (SlotCount - 1) * SlotGap;
	const float Top = Canvas->ClipY - BottomMargin - SlotHeight;
	float Left = (Canvas->ClipX - TotalWidth) * 0.5f;

	for (ECookieItem Item : TEnumRange<ECookieItem>())
	{
		const int32 SlotNumber = static_cast<int32>(Item) + 1;
		const FCookieItemSpec& Spec = Settings.GetItem(Item);
		const bool bAffordable = GameMode.CanAfford(Item);
		const FLinearColor TextColor = bAffordable ? Affordable : TooExpensive;

		if (Item == SelectedItem)
		{
			DrawRect(SelectedOutline, Left - SelectedBorder, Top - SelectedBorder,
				SlotWidth + 2.f * SelectedBorder, SlotHeight + 2.f * SelectedBorder);
		}
		DrawRect(SlotBackground, Left, Top, SlotWidth, SlotHeight);

		// Icon: a swatch in the item's placeholder color, dimmed when unaffordable.
		const float IconX = Left + 10.f;
		const float IconY = Top + (SlotHeight - IconSize) * 0.5f;
		DrawRect(bAffordable ? Spec.Launch.Color : Spec.Launch.Color * 0.4f, IconX, IconY, IconSize, IconSize);
		DrawText(FString::FromInt(SlotNumber), FLinearColor::White, IconX + 3.f, IconY + 1.f, Font, 0.8f);

		const float TextX = IconX + IconSize + 10.f;
		DrawText(Spec.DisplayName.ToString(), TextColor, TextX, Top + 8.f, Font, 1.f);
		DrawText(FString::Printf(TEXT("%s cookies"), *FormatCount(GameMode.GetCost(Item))), TextColor, TextX, Top + 30.f, Font, 0.9f);
		DrawText(FString::Printf(TEXT("owned %d"), GameMode.GetOwned(Item)), TextColor, TextX, Top + 50.f, Font, 0.9f);

		Left += SlotWidth + SlotGap;
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
