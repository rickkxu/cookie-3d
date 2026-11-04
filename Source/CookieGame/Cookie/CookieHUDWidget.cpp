#include "CookieHUDWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "CookieCharacter.h"
#include "CookieGameMode.h"
#include "CookieGameSettings.h"
#include "CookieHUD.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

namespace CookieHUDStyle
{
	// Layout, in pixels at 1080p (everything scales with the viewport height).
	constexpr float ReferenceHeight = 1080.f;
	constexpr float CounterTop = 14.f;
	constexpr float CounterSize = 84.f;
	constexpr float CounterCookie = 100.f;
	constexpr float CounterGap = 22.f;
	constexpr float PillHeight = 42.f;
	constexpr float PillTextSize = 27.f;
	constexpr float CardWidth = 140.f;
	constexpr float CardHeight = 118.f;
	constexpr float CardGap = 20.f;
	constexpr float CardBottom = 18.f;
	constexpr float CardRadius = 18.f;
	constexpr float CardPicture = 100.f;
	constexpr float SelectedScale = 1.06f;
	constexpr float SelectedLift = 8.f;
	constexpr float PulseSeconds = 0.25f;
	constexpr float PulseAmount = 0.08f;

	// Candy palette.
	const FLinearColor Cream = FLinearColor::FromSRGBColor(FColor(255, 247, 222));
	const FLinearColor Gold = FLinearColor::FromSRGBColor(FColor(246, 178, 52));
	const FLinearColor DarkBrown = FLinearColor::FromSRGBColor(FColor(70, 33, 10));
	const FLinearColor Ink = FLinearColor::FromSRGBColor(FColor(92, 46, 14));
	const FLinearColor Caramel = FLinearColor::FromSRGBColor(FColor(184, 104, 40));
	const FLinearColor CaramelBright = FLinearColor::FromSRGBColor(FColor(214, 134, 52));
	const FLinearColor CardBorder = FLinearColor::FromSRGBColor(FColor(250, 196, 76));
	const FLinearColor SelectedBorder = FLinearColor::FromSRGBColor(FColor(255, 232, 40));
	const FLinearColor Glow = FLinearColor::FromSRGBColor(FColor(255, 206, 40));
	const FLinearColor Panel = FLinearColor::FromSRGBColor(FColor(255, 233, 178));
	const FLinearColor PillFill = FLinearColor::FromSRGBColor(FColor(66, 30, 9));
	const FLinearColor PillBorder = FLinearColor::FromSRGBColor(FColor(232, 176, 70));
	const FLinearColor GreyCard = FLinearColor::FromSRGBColor(FColor(112, 108, 104));
	const FLinearColor GreyBorder = FLinearColor::FromSRGBColor(FColor(160, 156, 150));
	const FLinearColor GreyPanel = FLinearColor::FromSRGBColor(FColor(176, 172, 166));
	const FLinearColor GreyInk = FLinearColor::FromSRGBColor(FColor(70, 68, 66));
	const FLinearColor GreyPill = FLinearColor::FromSRGBColor(FColor(62, 60, 58));
	const FLinearColor SpendTop = FLinearColor::FromSRGBColor(FColor(255, 120, 110));
	const FLinearColor SpendBottom = FLinearColor::FromSRGBColor(FColor(226, 24, 24));
	const FLinearColor SpendOutline = FLinearColor::FromSRGBColor(FColor(70, 0, 0));
}

namespace
{
	using namespace CookieHUDStyle;

	FString FormatCount(double Value)
	{
		// Past a quadrillion, whole numbers stop fitting on screen (and eventually in an int64).
		if (Value >= 1e15)
		{
			return FString::Printf(TEXT("%.3g"), Value);
		}
		return FText::AsNumber(FMath::FloorToInt64(Value)).ToString();
	}

	// "9,810" from 10 up, "0.5" below.
	FString FormatRate(double Value)
	{
		if (Value >= 10.0)
		{
			return FText::AsNumber(FMath::RoundToInt64(Value)).ToString();
		}
		FNumberFormattingOptions Options;
		Options.MaximumFractionalDigits = 1;
		return FText::AsNumber(Value, &Options).ToString();
	}

	FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A *= Alpha;
		return Color;
	}

	// Everything a paint call needs, with helpers that draw in local (Slate) units.
	struct FPainter
	{
		const FGeometry& Geometry;
		FSlateWindowElementList& Out;
		int32 Layer;
		const FSlateFontInfo& BaseFont;

		FSlateFontInfo Font(float SizePx, float OutlinePx = 0.f, const FLinearColor& OutlineColor = FLinearColor::Transparent) const
		{
			FSlateFontInfo Result = BaseFont;
			// Slate font sizes are points at 96 dpi.
			Result.Size = FMath::Max(1.f, FMath::RoundToFloat(SizePx * 0.75f * 2.f) * 0.5f);
			Result.OutlineSettings = FFontOutlineSettings(FMath::RoundToInt(OutlinePx), OutlineColor);
			return Result;
		}

		FVector2D Measure(const FString& Text, const FSlateFontInfo& InFont) const
		{
			return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, InFont);
		}

		void Box(const FVector2D& Position, const FVector2D& Size, const FLinearColor& Fill, float Radius,
			const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f)
		{
			// The fill colour goes in as the draw tint (a brush's own colour is only applied by widgets, not by MakeBox).
			const FSlateRoundedBoxBrush Brush(FLinearColor::White, Radius, Outline, OutlineWidth);
			FSlateDrawElement::MakeBox(Out, ++Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)), &Brush,
				ESlateDrawEffect::None, Fill);
		}

		void Image(UTexture2D* Texture, const FVector2D& Position, float Size, float Alpha = 1.f)
		{
			if (!Texture)
			{
				return;
			}
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			Brush.DrawAs = ESlateBrushDrawType::Image;
			FSlateDrawElement::MakeBox(Out, ++Layer, Geometry.ToPaintGeometry(FVector2D(Size, Size), FSlateLayoutTransform(Position)),
				&Brush, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, Alpha));
		}

		void Text(const FString& String, const FSlateFontInfo& InFont, const FVector2D& Position, const FLinearColor& Color)
		{
			FSlateDrawElement::MakeText(Out, ++Layer, Geometry.ToPaintGeometry(Measure(String, InFont), FSlateLayoutTransform(Position)),
				String, InFont, ESlateDrawEffect::None, Color);
		}

		// Plain text centred on CentreX, top at Top. Returns its size.
		FVector2D CentredText(const FString& String, const FSlateFontInfo& InFont, float CentreX, float Top, const FLinearColor& Color)
		{
			const FVector2D Size = Measure(String, InFont);
			Text(String, InFont, FVector2D(CentreX - Size.X * 0.5, Top), Color);
			return Size;
		}

		// Game-style text: a vertical gradient (TopColor to BottomColor) inside a thick outline, over a soft shadow.
		// The gradient is drawn as thin horizontal bands of the fill, each clipped to its strip.
		void FancyText(const FString& String, float SizePx, const FVector2D& Position, const FLinearColor& TopColor,
			const FLinearColor& BottomColor, const FLinearColor& OutlineColor, float OutlinePx, float ShadowPx, float Alpha = 1.f)
		{
			const FSlateFontInfo Outlined = Font(SizePx, OutlinePx, WithAlpha(OutlineColor, Alpha));
			const FVector2D Size = Measure(String, Outlined);

			const FLinearColor Shadow(0.f, 0.f, 0.f, 0.32f * Alpha);
			Text(String, Font(SizePx, OutlinePx, Shadow), Position + FVector2D(0.0, ShadowPx), Shadow);
			Text(String, Font(SizePx, OutlinePx, FLinearColor(0.f, 0.f, 0.f, 0.16f * Alpha)), Position + FVector2D(0.0, ShadowPx * 1.8), FLinearColor(0.f, 0.f, 0.f, 0.16f * Alpha));
			Text(String, Outlined, Position, WithAlpha(BottomColor, Alpha));

			// Same outline width (invisible) so the fill lands exactly where the outlined pass put it.
			const FSlateFontInfo FillOnly = Font(SizePx, OutlinePx, FLinearColor::Transparent);
			constexpr int32 Bands = 10;
			for (int32 Band = 0; Band < Bands; ++Band)
			{
				const double Y0 = Position.Y + Size.Y * Band / Bands;
				const double Y1 = Position.Y + Size.Y * (Band + 1) / Bands;
				// Glyphs fill roughly the middle 60% of the line: spread the gradient over that.
				const float T = FMath::Clamp(static_cast<float>(((Y0 + Y1) * 0.5 - Position.Y) / Size.Y - 0.2) / 0.6f, 0.f, 1.f);
				const FVector2D TopLeft = Geometry.LocalToAbsolute(FVector2D(Position.X - OutlinePx, Y0));
				const FVector2D BottomRight = Geometry.LocalToAbsolute(FVector2D(Position.X + Size.X + OutlinePx, Y1));
				Out.PushClip(FSlateClippingZone(FSlateRect(TopLeft.X, TopLeft.Y, BottomRight.X, BottomRight.Y)));
				Text(String, FillOnly, Position, WithAlpha(FLinearColor::LerpUsingHSV(TopColor, BottomColor, T), Alpha));
				Out.PopClip();
			}
		}
	};
}

void SCookieHUDWidget::Construct(const FArguments& InArgs)
{
	HUD = InArgs._HUD;
	FontPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Fonts/LilitaOne-Regular.ttf"));
	BaseFont = FSlateFontInfo(FontPath, 24.f);
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(false);
	ForceVolatile(true);
}

int32 SCookieHUDWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const ACookieHUD* Hud = HUD.Get();
	const UWorld* World = Hud ? Hud->GetWorld() : nullptr;
	const ACookieGameMode* GameMode = World ? World->GetAuthGameMode<ACookieGameMode>() : nullptr;
	if (!GameMode)
	{
		return LayerId;
	}
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	const ACookieCharacter* Player = Cast<ACookieCharacter>(Hud->GetOwningPawn());

	FPainter P{ AllottedGeometry, OutDrawElements, LayerId, BaseFont };
	const FVector2D View = AllottedGeometry.GetLocalSize();
	const float S = View.Y / ReferenceHeight;
	UTexture2D* CookieTexture = Hud->GetCookieIcon().Color;

	// Crosshair: a small cream dot ringed in brown.
	P.Box(View * 0.5 - FVector2D(4.0 * S), FVector2D(8.0 * S), Cream, 4.f * S, DarkBrown, 2.f * S);

	// --- Top: the count and a cookie, then the per-second pill.
	const FString Count = FormatCount(GameMode->GetCookies());
	const float CounterOutline = 6.f * S;
	const FVector2D CountSize = P.Measure(Count, P.Font(CounterSize * S, CounterOutline));
	const float CookieSize = CounterCookie * S;
	const float GroupWidth = CountSize.X + CounterGap * S + CookieSize;
	const FVector2D CountPos((View.X - GroupWidth) * 0.5, CounterTop * S);
	P.FancyText(Count, CounterSize * S, CountPos, Cream, Gold, DarkBrown, CounterOutline, 5.f * S);
	const FVector2D CookiePos(CountPos.X + CountSize.X + CounterGap * S, CountPos.Y + (CountSize.Y - CookieSize) * 0.5);
	if (CookieTexture)
	{
		P.Image(CookieTexture, CookiePos + FVector2D(0.0, 5.f * S), CookieSize, 0.25f);
		P.Image(CookieTexture, CookiePos, CookieSize);
	}

	const FString Rate = FormatRate(GameMode->GetCookiesPerSecond()) + TEXT("/s");
	const FSlateFontInfo RateFont = P.Font(PillTextSize * S);
	const FVector2D RateSize = P.Measure(Rate, RateFont);
	const FVector2D PillSize(RateSize.X + 48.f * S, PillHeight * S);
	const FVector2D PillPos((View.X - PillSize.X) * 0.5, CountPos.Y + CountSize.Y - 4.f * S);
	P.Box(PillPos + FVector2D(0.0, 3.f * S), PillSize, FLinearColor(0.f, 0.f, 0.f, 0.25f), PillSize.Y * 0.5f);
	P.Box(PillPos, PillSize, WithAlpha(PillFill, 0.92f), PillSize.Y * 0.5f, PillBorder, 2.f * S);
	P.CentredText(Rate, RateFont, View.X * 0.5, PillPos.Y + (PillSize.Y - RateSize.Y) * 0.5, Cream);

	// --- Bottom: the item cards.
	const int32 CardCount = static_cast<int32>(ECookieItem::Count);
	const float RowWidth = (CardCount * CardWidth + (CardCount - 1) * CardGap) * S;
	const float RowLeft = (View.X - RowWidth) * 0.5f;
	const float CardTop = View.Y - (CardBottom + CardHeight) * S;
	TArray<FVector2D> CardTops;

	for (ECookieItem Item : TEnumRange<ECookieItem>())
	{
		const int32 Index = static_cast<int32>(Item);
		const FCookieItemSpec& Spec = Settings.GetItem(Item);
		const bool bSelected = Player && Player->GetSelectedItem() == Item;
		const bool bAffordable = GameMode->CanAfford(Item);

		// Selected cards sit bigger and higher; a purchase gives a quick swell.
		const float SincePurchase = Hud->GetSecondsSinceBought(Item);
		const float Pulse = SincePurchase < PulseSeconds ? PulseAmount * FMath::Sin(PI * SincePurchase / PulseSeconds) : 0.f;
		const float K = S * (bSelected ? SelectedScale : 1.f) * (1.f + Pulse);
		const FVector2D Centre(RowLeft + (Index * (CardWidth + CardGap) + CardWidth * 0.5f) * S,
			CardTop + CardHeight * 0.5f * S - (bSelected ? SelectedLift * S : 0.f));
		// Card space (design pixels from the card's top-left) to the screen.
		auto At = [&](float X, float Y) { return Centre + FVector2D((X - CardWidth * 0.5f) * K, (Y - CardHeight * 0.5f) * K); };
		CardTops.Add(At(CardWidth * 0.5f, 0.f));

		// Shadow under the card, then the glow round a selected one.
		P.Box(At(-2.f, 6.f), FVector2D(CardWidth + 4.f, CardHeight + 4.f) * K, FLinearColor(0.f, 0.f, 0.f, 0.18f), (CardRadius + 2.f) * K);
		P.Box(At(0.f, 9.f), FVector2D(CardWidth, CardHeight) * K, FLinearColor(0.f, 0.f, 0.f, 0.22f), CardRadius * K);
		if (bSelected)
		{
			for (int32 Ring = 7; Ring >= 1; --Ring)
			{
				const float Grow = Ring * 5.f;
				P.Box(At(-Grow, -Grow), FVector2D(CardWidth + 2.f * Grow, CardHeight + 2.f * Grow) * K,
					WithAlpha(Glow, 0.2f * (1.f - Ring / 8.f)), (CardRadius + Grow) * K);
			}
		}

		const FLinearColor Fill = !bAffordable ? GreyCard : (bSelected ? CaramelBright : Caramel);
		const FLinearColor Border = !bAffordable ? GreyBorder : (bSelected ? SelectedBorder : CardBorder);
		P.Box(At(0.f, 0.f), FVector2D(CardWidth, CardHeight) * K, Fill, CardRadius * K, Border, (bSelected ? 5.f : 3.f) * K);
		// A soft sheen across the top half gives the card some roundness.
		P.Box(At(6.f, 5.f), FVector2D(CardWidth - 12.f, 36.f) * K, FLinearColor(1.f, 1.f, 1.f, bSelected ? 0.16f : 0.1f), (CardRadius - 5.f) * K);

		// Name, on the card under the picture.
		const FString Name = Spec.DisplayName.ToString();
		const FSlateFontInfo NameFont = P.Font(19.f * K, 3.f * K, bAffordable ? DarkBrown : GreyInk);
		P.CentredText(Name, NameFont, At(CardWidth * 0.5f, 0.f).X, At(0.f, 21.f).Y, bAffordable ? Cream : FLinearColor(0.86f, 0.85f, 0.84f));

		// Cream panel: cost (with a cookie) and how many you own.
		P.Box(At(9.f, 45.f), FVector2D(CardWidth - 18.f, 48.f) * K, bAffordable ? Panel : GreyPanel, 12.f * K);
		const FLinearColor TextInk = bAffordable ? Ink : GreyInk;
		const FString Cost = FormatCount(GameMode->GetCost(Item));
		const FSlateFontInfo CostFont = P.Font(20.f * K);
		const FVector2D CostSize = P.Measure(Cost, CostFont);
		const float IconSize = 22.f * K;
		const float CostLeft = At(CardWidth * 0.5f, 0.f).X - (IconSize + 4.f * K + CostSize.X) * 0.5f;
		const float CostTop = At(0.f, 46.f).Y;
		if (CookieTexture)
		{
			P.Image(bAffordable ? CookieTexture : Hud->GetCookieIcon().Grey.Get(), FVector2D(CostLeft, CostTop + (CostSize.Y - IconSize) * 0.5f), IconSize);
		}
		P.Text(Cost, CostFont, FVector2D(CostLeft + IconSize + 4.f * K, CostTop), TextInk);
		P.CentredText(FString::Printf(TEXT("x %d"), GameMode->GetOwned(Item)), P.Font(18.f * K), At(CardWidth * 0.5f, 0.f).X, At(0.f, 68.f).Y, TextInk);

		// What one more adds.
		P.Box(At(14.f, 96.f), FVector2D(CardWidth - 28.f, 18.f) * K, bAffordable ? PillFill : GreyPill, 9.f * K);
		const FString Gain = TEXT("+") + FormatRate(Settings.GetProduction(Item)) + TEXT("/s");
		const FSlateFontInfo GainFont = P.Font(15.f * K);
		const FVector2D GainSize = P.Measure(Gain, GainFont);
		P.CentredText(Gain, GainFont, At(CardWidth * 0.5f, 0.f).X, At(0.f, 105.f).Y - GainSize.Y * 0.5, bAffordable ? Cream : FLinearColor(0.7f, 0.69f, 0.68f));

		// The picture stands on the card, rising out of its top.
		const float PictureSize = CardPicture * K;
		const FVector2D PicturePos = At(CardWidth * 0.5f, 0.f) + FVector2D(-PictureSize * 0.5f, -PictureSize + 22.f * K);
		if (const FCookieIcon* Icon = Hud->GetItemIcon(Item))
		{
			P.Image(bAffordable ? Icon->Color.Get() : Icon->Grey.Get(), PicturePos, PictureSize, bAffordable ? 1.f : 0.85f);
		}
		else
		{
			P.Box(PicturePos + FVector2D(PictureSize * 0.25f), FVector2D(PictureSize * 0.5f), Spec.Launch.Color, 14.f * K);
		}

		// Key number badge on the top-left corner.
		P.Box(At(-7.f, -7.f), FVector2D(26.f, 26.f) * K, bAffordable ? PillFill : GreyPill, 13.f * K, Border, 2.f * K);
		const FSlateFontInfo KeyFont = P.Font(16.f * K);
		const FString Key = FString::FromInt(Index + 1);
		const FVector2D KeySize = P.Measure(Key, KeyFont);
		P.Text(Key, KeyFont, At(6.f, 6.f) - KeySize * 0.5, Cream);
	}

	// --- Pop-ups.
	for (const ACookieHUD::FPopup& Popup : Hud->GetPopups())
	{
		const float Life = ACookieHUD::GetPopupLife(Popup.Kind);
		const float T = Popup.Age / Life;
		const float Fade = 1.f - FMath::SmoothStep(0.55f, 1.f, T);
		// Pops in from small over its first tenth of a second.
		const float Pop = FMath::Lerp(0.6f, 1.f, FMath::Clamp(Popup.Age / 0.1f, 0.f, 1.f));
		switch (Popup.Kind)
		{
		case ACookieHUD::EPopupKind::Click:
		{
			const FString Text = TEXT("+1");
			const float Size = 30.f * S * Pop;
			const FVector2D TextSize = P.Measure(Text, P.Font(Size, 3.f * S));
			const FVector2D Pos = View * 0.5 + FVector2D(Popup.Jitter * 26.f * S - TextSize.X * 0.5, -40.f * S - 60.f * S * FMath::Sin(T * HALF_PI) - TextSize.Y);
			P.FancyText(Text, Size, Pos, Cream, Gold, DarkBrown, 3.f * S, 3.f * S, Fade);
			break;
		}
		case ACookieHUD::EPopupKind::Gain:
		{
			const int32 Index = static_cast<int32>(Popup.Item);
			if (!CardTops.IsValidIndex(Index))
			{
				break;
			}
			const FString Text = FString::Printf(TEXT("+%d"), FMath::RoundToInt(Popup.Amount));
			const float Size = 38.f * S * Pop;
			const FVector2D TextSize = P.Measure(Text, P.Font(Size, 4.f * S));
			const FVector2D Pos = CardTops[Index] + FVector2D(-TextSize.X * 0.5, -(CardPicture - 30.f) * S - TextSize.Y - 50.f * S * FMath::Sin(T * HALF_PI));
			P.FancyText(Text, Size, Pos, Cream, Gold, DarkBrown, 4.f * S, 3.f * S, Fade);
			break;
		}
		case ACookieHUD::EPopupKind::Spend:
		{
			// Drops away from the count under gravity, drifting right.
			const FString Text = TEXT("-") + FormatCount(Popup.Amount);
			const float Size = 40.f * S * Pop;
			const FVector2D Start(CookiePos.X + CookieSize * 0.55f, CookiePos.Y + CookieSize * 0.45f);
			const FVector2D Pos = Start + FVector2D(40.f * S * Popup.Age, 520.f * S * Popup.Age * Popup.Age);
			P.FancyText(Text, Size, Pos, SpendTop, SpendBottom, SpendOutline, 4.f * S, 3.f * S, Fade);
			break;
		}
		}
	}

	return P.Layer;
}
