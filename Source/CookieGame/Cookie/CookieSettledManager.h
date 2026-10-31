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

	const UInstancedStaticMeshComponent* GetItemInstances(ECookieItem Item) const { return ItemInstances[static_cast<int32>(Item)]; }

	// Removes one settled item and returns where it was.
	FTransform RemoveItemInstance(ECookieItem Item, int32 Index);

	// Frozen grandmas are drawn with a static copy of a live grandma's pose, baked at runtime.
	bool HasGrandmaPose() const { return bHasGrandmaPose; }
	void SetGrandmaPose(UStaticMesh* Pose);

	// Total milliseconds spent emitting from settled items and refreshing piles since the last call.
	void ConsumeTimings(double& OutEmitMs, double& OutPileMs);
	int32 GetPileCount() const { return Piles.Num(); }
	int64 GetPiledCookieCount() const { return PiledCookies; }

private:
	struct FPile
	{
		FVector Ground;
		int32 Count = 0;
		int32 Seed = 0;
		// Heap radius when its surface cookies were last laid out.
		double BuiltRadius = 0.0;
		bool bDirty = false;
	};

	UInstancedStaticMeshComponent* MakeInstances(const TCHAR* Name, const FCookieLaunchSpec& Spec);
	void EmitFromSettledItems(float DeltaSeconds);
	void RefreshDirtyPiles();

	// Where a new heap goes: on the floor or furniture below Target, without crossing a wall from Source.
	// Ignores characters, flying things and everything settled.
	FVector FindPileGround(const FVector& Source, const FVector& Target) const;
	double GetPileRadius(const FPile& Pile) const;
	FTransform GetPileCoreTransform(const FPile& Pile) const;

	// Heap height as a fraction of its radius: flat for a handful of cookies, 0.5 for a real heap.
	static double HeightRatioFor(double Radius);

	// Lays out the cookies covering pile Index at its current size.
	void BuildPileSurface(int32 Index);

	// Settled cookies, CookiesPerChunk to a component.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> CookieChunks;
	int32 SettledCookieCount = 0;
	static constexpr int32 CookiesPerChunk = 500;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CookieMesh;

	// Indexed by ECookieItem.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> ItemInstances;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> PileInstances;

	// The cookies on each pile, one component per pile so a pile can be re-laid without touching the others.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> PileSurfaces;

	// Pile i is instance i of PileInstances and PileSurfaces[i].
	TArray<FPile> Piles;
	int64 PiledCookies = 0;

	// Fractional emissions carried between frames, indexed by ECookieItem.
	TArray<float> EmitAccumulators;

	float PileRefreshTimer = 0.f;
	bool bHasGrandmaPose = false;

	// Time spent in Tick since the last ConsumeTimings, for the stats log.
	double EmitMs = 0.0;
	double PileMs = 0.0;
};
