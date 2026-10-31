#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CookieTypes.h"
#include "CookieGameMode.generated.h"

class ACookieSettledManager;

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

	// Cookie actors still simulating physics, kept up to date by ACookieLaunchable. Counts toward the physical cookie cap.
	int32 GetFlyingCookieCount() const { return FlyingCookies; }
	void AdjustFlyingCookieCount(int32 Delta) { FlyingCookies += Delta; }

	// Everything that has come to rest.
	ACookieSettledManager* GetSettled() const { return Settled; }

	// Rains free items down around the player (or the player start), to see how the game holds up.
	void StartStressTest(int32 ToasterOvens, int32 Grandmas);

private:
	void SpawnStressBatch();
	void LogStats(float DeltaSeconds);

	UPROPERTY(Transient)
	TObjectPtr<ACookieSettledManager> Settled;

	double Cookies = 0.0;
	int32 Owned[static_cast<int32>(ECookieItem::Count)] = {};
	int32 FlyingCookies = 0;

	// Stress test items still waiting to be dropped, indexed by ECookieItem.
	int32 StressPending[static_cast<int32>(ECookieItem::Count)] = {};
	FTimerHandle StressTimer;

	float GrandmaBudgetSeconds = 0.f;
	float StatsSeconds = 0.f;
	int32 StatsFrames = 0;
};
