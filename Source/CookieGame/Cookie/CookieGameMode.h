#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CookieTypes.h"
#include "CookieGameMode.generated.h"

/** Owns the cookie bank, how many of each item the player has bought, and passive production. */
UCLASS()
class COOKIEGAME_API ACookieGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACookieGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	double GetCookies() const { return Cookies; }
	void AddCookies(double Amount) { Cookies += Amount; }

	// Banked production from everything owned, whether or not it's still physically in the world.
	double GetCookiesPerSecond() const;

	int32 GetOwned(ECookieItem Item) const { return Owned[static_cast<int32>(Item)]; }
	double GetCost(ECookieItem Item) const;
	bool CanAfford(ECookieItem Item) const { return Cookies >= GetCost(Item); }

	// Deducts the cost and counts the purchase. Returns false if the player can't afford it.
	bool TryBuy(ECookieItem Item);

	// Live cookie actors in the world, kept up to date by ACookieLaunchable. Used for the physical cookie cap.
	int32 GetPhysicalCookieCount() const { return PhysicalCookies; }
	void AdjustPhysicalCookieCount(int32 Delta) { PhysicalCookies += Delta; }

private:
	double Cookies = 0.0;
	int32 Owned[static_cast<int32>(ECookieItem::Count)] = {};
	int32 PhysicalCookies = 0;
};
