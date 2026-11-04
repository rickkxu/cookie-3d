#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/SLeafWidget.h"

class ACookieHUD;

/**
 * Draws the whole HUD in one pass, laid out for 1080p and scaled to the viewport:
 *  - top: the cookie count (cream-to-gold, dark outline, soft shadow) with a cookie, and a "per second" pill
 *  - bottom: one portrait card per item, its picture standing up out of the card's top
 *  - pop-ups: "+1" from the crosshair, "+N" above a card, and the cookies spent falling from the count
 * Everything is in Lilita One. Hit-test invisible: the game keeps the mouse.
 */
class SCookieHUDWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCookieHUDWidget) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ACookieHUD>, HUD)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(100.0, 100.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<ACookieHUD> HUD;
	FString FontPath;
	// Loaded from the .ttf once; every size and outline is a copy of it (sharing one font keeps Slate's cache small).
	FSlateFontInfo BaseFont;
};
