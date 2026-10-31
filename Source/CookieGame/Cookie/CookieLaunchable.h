#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CookieTypes.h"
#include "CookieLaunchable.generated.h"

class UStaticMeshComponent;

/**
 * A physical thing in the world: a cookie, a toaster oven, a grandma.
 * Purchased items pop a cookie out of themselves every few seconds, wherever they land.
 * For now everything simulates physics forever. Freeze-when-settled + instancing is build step 5.
 */
UCLASS()
class COOKIEGAME_API ACookieLaunchable : public AActor
{
	GENERATED_BODY()

public:
	ACookieLaunchable();

	// Spawns and throws a cookie. Returns null if the physical cookie cap is reached.
	static ACookieLaunchable* SpawnCookie(UWorld* World, const FVector& Location, const FRotator& Rotation, const FVector& Velocity);

	// Spawns and throws a purchased item, which then starts producing physical cookies.
	static ACookieLaunchable* SpawnItem(UWorld* World, ECookieItem Item, const FVector& Location, const FRotator& Rotation, const FVector& Velocity);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	static ACookieLaunchable* SpawnLaunched(UWorld* World, const FCookieLaunchSpec& Spec, const FVector& Location, const FRotator& Rotation, const FVector& Velocity);

	// Applies the look from Spec and throws it with Velocity.
	void Launch(const FCookieLaunchSpec& Spec, const FVector& Velocity);

	void EmitCookie();

	UPROPERTY(VisibleAnywhere, Category = "Cookie")
	TObjectPtr<UStaticMeshComponent> Mesh;

	FTimerHandle EmitTimer;
	bool bIsCookie = false;
};
