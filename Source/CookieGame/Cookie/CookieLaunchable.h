#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CookieTypes.h"
#include "CookieLaunchable.generated.h"

class UStaticMeshComponent;

/**
 * A physical thing in flight: a cookie, a toaster oven, a grandma.
 * Purchased items pop a cookie out of themselves every few seconds while they fly.
 * Once it comes to rest it hands itself to ACookieSettledManager as an instance and is destroyed,
 * so only recently launched things simulate physics.
 */
UCLASS()
class COOKIEGAME_API ACookieLaunchable : public AActor
{
	GENERATED_BODY()

public:
	ACookieLaunchable();

	// Spawns and throws a cookie. Always physical: used for the player's own shots.
	static ACookieLaunchable* SpawnCookie(UWorld* World, const FVector& Location, const FRotator& Rotation, const FVector& Velocity);

	// Spawns and throws a purchased item, which then starts producing physical cookies.
	// Grandmas are their own actor (ACookieGrandma); everything else is one of these.
	static AActor* SpawnItem(UWorld* World, ECookieItem Item, const FVector& Location, const FRotator& Rotation, const FVector& Velocity);

	// A produced cookie pops up out of Top. Past the physical cookie cap it goes onto a pile instead.
	static void PopCookie(UWorld* World, const FVector& Top);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	static ACookieLaunchable* SpawnLaunched(UWorld* World, const FCookieLaunchSpec& Spec, const FVector& Location, const FRotator& Rotation, const FVector& Velocity);

	// Applies the look from Spec and throws it with Velocity.
	void Launch(const FCookieLaunchSpec& Spec, const FVector& Velocity);

	void EmitCookie();
	void EmitFromSpouts();

	// Runs a few times a second. Freezes this into an instance once it has been still long enough.
	void CheckSettled();

	UPROPERTY(VisibleAnywhere, Category = "Cookie")
	TObjectPtr<UStaticMeshComponent> Mesh;

	FTimerHandle EmitTimer;
	FTimerHandle SettleTimer;
	bool bIsCookie = false;
	ECookieItem Item = ECookieItem::Grandma;
	// Big producers' cookies not yet launched, one per spout.
	TArray<float> SpoutCarry;
	float StillSeconds = 0.f;
	float FlyingSeconds = 0.f;
};
