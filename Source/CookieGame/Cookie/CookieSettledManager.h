#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CookieTypes.h"
#include "CookieSettledManager.generated.h"

class UInstancedStaticMeshComponent;
struct FCookieLaunchSpec;

/**
 * Holds everything that has come to rest, as instanced static meshes: one draw call per kind,
 * no physics simulation, but still solid so new throws stack on top.
 * Settled items keep producing: this actor pops cookies out of random settled instances at the
 * same overall rate the items would have emitted on their own.
 * Past the physical cookie cap, new cookies grow pile meshes instead of adding individual ones.
 */
UCLASS()
class COOKIEGAME_API ACookieSettledManager : public AActor
{
	GENERATED_BODY()

public:
	ACookieSettledManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void AddCookie(const FTransform& Transform);
	void AddItem(ECookieItem Item, const FTransform& Transform);

	// Adds one cookie to the nearest pile around Location, or starts a new pile on the ground there.
	void AddToPileNear(const FVector& Location);

	int32 GetSettledCookieCount() const;
	int32 GetSettledItemCount(ECookieItem Item) const;
	int32 GetPileCount() const { return Piles.Num(); }
	int64 GetPiledCookieCount() const { return PiledCookies; }

private:
	struct FPile
	{
		FVector Ground;
		int32 Count = 0;
		bool bDirty = false;
	};

	UInstancedStaticMeshComponent* MakeInstances(const TCHAR* Name, const FCookieLaunchSpec& Spec);
	void EmitFromSettledItems(float DeltaSeconds);
	void RefreshDirtyPiles();
	FTransform GetPileTransform(const FPile& Pile) const;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> CookieInstances;

	// Indexed by ECookieItem.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> ItemInstances;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> PileInstances;

	// Pile i is instance i of PileInstances.
	TArray<FPile> Piles;
	int64 PiledCookies = 0;

	// Fractional emissions carried between frames, indexed by ECookieItem.
	TArray<float> EmitAccumulators;

	float PileRefreshTimer = 0.f;
};
