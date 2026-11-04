#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CookieIconStudio.h"
#include "CookieTypes.h"
#include "CookieHUD.generated.h"

class SCookieHUDWidget;

/**
 * The HUD: the cookie count with its per-second pill on top, a card per item at the bottom (picture, cost, how many
 * you own, what one more adds), and pop-ups for clicks and purchases. Drawn by SCookieHUDWidget (Slate); this actor
 * owns the item pictures and the animation state.
 */
UCLASS()
class COOKIEGAME_API ACookieHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACookieHUD();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// One item was bought for Cost cookies: its card pulses, "+1" floats above it, and "-Cost" falls from the count.
	// Rapid-fire purchases add up into one "+N" and one running "-total".
	void NotifyPurchase(ECookieItem Item, double Cost);

	// A cookie was shot: "+1" floats up from the crosshair.
	void NotifyShot();

	enum class EPopupKind : uint8 { Gain, Spend, Click };

	struct FPopup
	{
		EPopupKind Kind = EPopupKind::Click;
		ECookieItem Item = ECookieItem::ToasterOven;
		double Amount = 0.0;
		float Age = 0.f;
		// Random sideways offset (click pop-ups), so quick clicks don't stack exactly.
		float Jitter = 0.f;
	};

	const TArray<FPopup>& GetPopups() const { return Popups; }
	static float GetPopupLife(EPopupKind Kind);

	// Seconds since this item was last bought (large if never), for the buy pulse.
	float GetSecondsSinceBought(ECookieItem Item) const { return Now - LastBought[static_cast<int32>(Item)]; }

	// Pictures rendered from the real models, once they're ready (null until then).
	const FCookieIcon* GetItemIcon(ECookieItem Item) const;
	const FCookieIcon& GetCookieIcon() const { return CookieIcon; }

private:
	void OnIconsReady(const TArray<FCookieIcon>& Items, const FCookieIcon& Cookie);

	TSharedPtr<SCookieHUDWidget> Widget;

	UPROPERTY(Transient)
	TArray<FCookieIcon> ItemIcons;

	UPROPERTY(Transient)
	FCookieIcon CookieIcon;

	TArray<FPopup> Popups;
	float LastBought[static_cast<int32>(ECookieItem::Count)];
	float Now = 0.f;
};
