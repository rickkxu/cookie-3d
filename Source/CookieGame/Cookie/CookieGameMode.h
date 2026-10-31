#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CookieTypes.h"
#include "CookieGameMode.generated.h"

/** Owns the cookie bank and how many of each item the player has bought. */
UCLASS()
class COOKIEGAME_API ACookieGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACookieGameMode();

	virtual void BeginPlay() override;

	double GetCookies() const { return Cookies; }
	void AddCookies(double Amount) { Cookies += Amount; }

	int32 GetOwned(ECookieItem Item) const { return Owned[static_cast<int32>(Item)]; }
	double GetCost(ECookieItem Item) const;
	bool CanAfford(ECookieItem Item) const { return Cookies >= GetCost(Item); }

	// Deducts the cost and counts the purchase. Returns false if the player can't afford it.
	bool TryBuy(ECookieItem Item);

private:
	double Cookies = 0.0;
	int32 Owned[static_cast<int32>(ECookieItem::Count)] = {};
};
