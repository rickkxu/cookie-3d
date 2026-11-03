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

	// A cookie came to rest here: it stays as a loose cookie, or joins a pile past the physical cookie cap.
	void AddCookie(const FTransform& Transform);
	void AddItem(ECookieItem Item, const FTransform& Transform);

	// Adds one cookie to the nearest pile around Location, or starts a new pile on the ground there.
	void AddToPileNear(const FVector& Location);

	// A produced cookie that flies its arc without simulating physics (used once too many cookies are in the air),
	// then lands like any other cookie. It still collides with the house and everything settled, it just doesn't bounce.
	void LaunchGhostCookie(const FVector& Start, const FVector& Velocity);
	int32 GetGhostCookieCount() const { return Ghosts.Num(); }

	// Spending cookies: makes Amount cookies vanish. They come off the top of every pile and every
	// scattering of loose cookies at once, so the whole house thins out evenly from the surface down.
	// If not enough have landed yet, cookies still in the air vanish as they land.
	void RemoveCookies(int64 Amount);

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

	struct FGhost
	{
		FVector Start;
		FVector Velocity;
		FQuat Spin0;
		FVector SpinAxis;
		double SpinSpeed = 0.0;
		double Age = 0.0;
		// When and how it lands, worked out once at launch.
		double LandTime = 0.0;
		FTransform Landing;
	};

	UInstancedStaticMeshComponent* MakeInstances(const TCHAR* Name, const FCookieLaunchSpec& Spec);
	void EmitFromSettledItems(float DeltaSeconds);
	void TickGhosts(float DeltaSeconds);

	// Loose cookies to remove, Count of them: the top ones in each spot first, spread over the whole house.
	void RemoveLooseCookies(int32 Count);
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

	// Takes pile Index away entirely, keeping pile i = instance i of PileInstances and PileSurfaces[i].
	void RemovePile(int32 Index);

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

	// Produced cookies flying without physics, drawn by GhostInstances (instance i = Ghosts[i]).
	// The instances are a pool: GhostSlotsInUse were shown last frame, the rest are shrunk to nothing.
	int32 GhostSlotsInUse = 0;
	TArray<FGhost> Ghosts;
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> GhostInstances;

	// Cookies already spent that were still in the air: this many landing cookies vanish instead of staying.
	int64 RemovalDebt = 0;

	float PileRefreshTimer = 0.f;
	bool bHasGrandmaPose = false;

	// Time spent in Tick since the last ConsumeTimings, for the stats log.
	double EmitMs = 0.0;
	double PileMs = 0.0;
};
