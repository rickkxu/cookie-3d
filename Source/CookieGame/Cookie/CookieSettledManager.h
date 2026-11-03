#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CookieTypes.h"
#include "CookieSettledManager.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
struct FCookieLaunchSpec;

/**
 * Holds everything that has come to rest, as instanced static meshes: one draw call per kind,
 * no physics simulation, but still solid so new throws stack on top.
 * Settled items keep producing: this actor pops cookies out of random settled instances at the
 * same overall rate the items would have emitted on their own. Big producers (trees, bakeries,
 * factories) spew streams of cookies out of their spouts instead, onto heaps beside them.
 *
 * Past the loose cookie cap, cookies join heaps. A heap's visible surface is made entirely of real
 * cookies (Nanite instances), laid out over a few frames when it grows; underneath, a core mesh in
 * the heap's exact shape gives it collision you can walk on and a shadow. Outdoors heaps have no
 * size limit and merge as they grow, so they become mountains.
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

	// Adds Count cookies to the heap around Location, or starts a new heap on the ground there.
	void AddToPileNear(const FVector& Location, int64 Count = 1);

	// A produced cookie that flies its arc without simulating physics (used once too many cookies are in the air),
	// then lands like any other cookie. It still collides with the house and everything settled, it just doesn't bounce.
	void LaunchGhostCookie(const FVector& Start, const FVector& Velocity);
	int32 GetGhostCookieCount() const { return Ghosts.Num(); }

	// Big producers: spews this item's output for DeltaSeconds out of its spouts, as seen at ItemTransform.
	// Carry holds each spout's not-yet-launched cookies between calls (one float per spout).
	// Every launched cookie is banked straight away and lands on the heap by the spout.
	void EmitSpouts(ECookieItem Item, const FTransform& ItemTransform, float DeltaSeconds, float* Carry);

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

	// Height of the heap surface (or ground) under a point, for aiming streams of cookies at it.
	double GetLandingHeight(const FVector& Location) const;

	// Total milliseconds spent emitting from settled items and refreshing piles since the last call.
	void ConsumeTimings(double& OutEmitMs, double& OutPileMs);
	int32 GetPileCount() const { return Piles.Num(); }
	int64 GetPiledCookieCount() const { return PiledCookies; }
	int64 GetLargestPileCount() const;
	int32 GetPileSurfaceInstanceCount() const;

private:
	struct FLump
	{
		FVector2D Centre;
		double Size = 0.0;
		double Rise = 0.0;
	};

	// A heap's shape at a given cookie count: a paraboloid with a wobbly outline and a few lumps.
	struct FPileShape
	{
		FVector Ground = FVector::ZeroVector;
		double Radius = 0.0;
		double Height = 0.0;
		double Wobble2 = 0.0;
		double Wobble3 = 0.0;
		// Height falls off as 1 - (d / r)^Profile: 2 is a dome, lower is more conical.
		double Profile = 2.0;
		FLump Lumps[5];

		double HeightAt(const FVector2D& P) const;
		FVector NormalAt(const FVector2D& P) const;
	};

	struct FPile
	{
		FVector Ground = FVector::ZeroVector;
		int64 Count = 0;
		int32 Seed = 0;
		// Inside the player's house: capped in size so it doesn't fill the room, and clipped against furniture.
		bool bIndoor = false;
		bool bDirty = false;
		// Heap radius when its surface cookies were last laid out.
		double BuiltRadius = 0.0;

		// Surface cookies, CookiesPerSurfaceChunk to a component, and the core in the heap's shape.
		TArray<UInstancedStaticMeshComponent*> Chunks;
		UProceduralMeshComponent* Core = nullptr;

		// A layout in progress: transforms generated so far for the shape being built, and how far the
		// generator and the hand-over to the chunks have got.
		bool bBuilding = false;
		FPileShape BuildShape;
		int32 BuildTotal = 0;
		int32 BuildNext = 0;
		int32 ApplyNextChunk = -1;
		TArray<FTransform> Staging;
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
		// Cookies this one stands for. Streams from big producers send several per visible cookie.
		int64 Payload = 1;
		// Lands straight onto the heap at Anchor (streams), instead of resting as a loose cookie at Landing.
		bool bToPile = false;
		FVector Anchor = FVector::ZeroVector;
	};

	UInstancedStaticMeshComponent* MakeInstances(const TCHAR* Name, const FCookieLaunchSpec& Spec);
	void EmitFromSettledItems(float DeltaSeconds);
	void TickGhosts(float DeltaSeconds);

	// Loose cookies to remove, Count of them: the top ones in each spot first, spread over the whole house.
	void RemoveLooseCookies(int32 Count);

	// A stream cookie from a spout, landing at Target after FlightSeconds and joining the heap at Anchor
	// (each spout feeds one heap, so its cookies build one mountain rather than a scatter of puddles).
	void LaunchStreamCookie(const FVector& Start, const FVector& Target, const FVector& Anchor, float FlightSeconds, int64 Payload);

	// Radius of the biggest heap whose footprint covers Location, or 0.
	double GetPileRadiusAt(const FVector& Location) const;

	FPile& CreatePile(const FVector& Ground);
	void RemovePile(int32 Index);
	void MergeOverlappingPiles();
	void RefreshDirtyPiles();
	void TickPileBuilds(double BudgetSeconds);
	// Lifts the player onto a heap that has them buried (outside the house only).
	void KeepPlayerOnTopOfHeaps();

	FPileShape ShapeFor(const FPile& Pile) const;
	double GetPileRadius(int64 Count) const;
	bool IsLargePile(const FPile& Pile) const;
	bool IsInsideHouse(const FVector& Location) const;

	// Where a new heap goes: on the floor or furniture below Target, without crossing a wall from Source.
	// Ignores characters, flying things and everything settled.
	FVector FindPileGround(const FVector& Source, const FVector& Target) const;

	// Heap height as a fraction of its radius: flat for a handful of cookies, 0.5 for a real heap.
	static double HeightRatioFor(double Radius);
	static double ProfileFor(double Radius);

	// Surface layout. Cookie Index of the layout for Shape; false when that cookie is left out
	// (inside a wall or furniture, or under the ground).
	bool MakeSurfaceCookie(const FPile& Pile, const FPileShape& Shape, int32 Index, int32 SurfaceCount, int32 ClutterCount, FTransform& Out) const;
	int32 SurfaceCookieCount(const FPile& Pile, const FPileShape& Shape, int32& OutClutter, int32& OutSkirt) const;
	void StartPileBuild(FPile& Pile);
	// Hands generated transforms to the chunk components. Returns true once every chunk has them.
	bool ApplyPileChunks(FPile& Pile, int32 MaxChunks);
	void BuildPileCore(FPile& Pile, const FPileShape& Shape);
	UInstancedStaticMeshComponent* MakeSurfaceChunk(const FPile& Pile);

	// Settled cookies, CookiesPerChunk to a component.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> CookieChunks;
	int32 SettledCookieCount = 0;
	static constexpr int32 CookiesPerChunk = 500;
	static constexpr int32 CookiesPerSurfaceChunk = 8192;
	// The second cookie layer under a heap's surface, as a fraction of the top layer.
	static constexpr double UnderLayer = 0.8;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CookieMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PileCoreMaterial;

	// Indexed by ECookieItem.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> ItemInstances;

	// What settled big producers look like (their instances are hidden).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ProducerVisuals;

	// Every component that belongs to a heap, so the garbage collector keeps them while the heaps exist.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UActorComponent>> PileComponents;

	TArray<TUniquePtr<FPile>> Piles;
	int64 PiledCookies = 0;

	// Fractional emissions carried between frames, indexed by ECookieItem.
	TArray<float> EmitAccumulators;

	// Spout carries for settled big producers: per item, SpoutCount floats per instance.
	TArray<TArray<float>> SettledSpoutCarry;

	// Produced cookies flying without physics, drawn by GhostInstances (instance i = Ghosts[i]).
	// The instances are a pool: GhostSlotsInUse were shown last frame, the rest are shrunk to nothing.
	int32 GhostSlotsInUse = 0;
	TArray<FGhost> Ghosts;
	int64 GhostPayloadInFlight = 0;
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> GhostInstances;

	// Cookies already spent that were still in the air: this many landing cookies vanish instead of staying.
	int64 RemovalDebt = 0;

	float PileRefreshTimer = 0.f;
	// Cookies laid out immediately (outside the time-sliced builds) this frame.
	int32 ImmediateCookiesThisFrame = 0;
	bool bHasGrandmaPose = false;

	// Cookie size, cached from the mesh.
	double CookieRadius = 13.0;
	double CookieThickness = 2.8;

	// Time spent in Tick since the last ConsumeTimings, for the stats log.
	double EmitMs = 0.0;
	double PileMs = 0.0;
};
