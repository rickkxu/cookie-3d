#include "CookieSettledManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "CookieGameMode.h"
#include "CookieGameSettings.h"
#include "CookieLaunchable.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
	// Heaps are half as tall as they are wide.
	constexpr double PileHeightRatio = 0.5;

	// Heaps narrower than this (cm) have no core: a handful of cookies is just cookies.
	constexpr double MinCoreRadius = 35.0;

	// Heaps wider than this (cm) block the player, so mountains can be climbed; smaller ones you wade through.
	constexpr double WalkablePileRadius = 150.0;

	// Layouts smaller than this many cookies are built at once instead of over several frames.
	constexpr int32 ImmediateBuildCookies = 3000;

	// Splits Amount across pools in proportion to their sizes, so every cookie is equally likely to go.
	// Largest remainder rounding, so the shares add up to exactly Amount (which must not exceed the total).
	TArray<int64> SplitProportionally(const TArray<int64>& Pools, int64 Amount)
	{
		int64 Total = 0;
		for (int64 Pool : Pools)
		{
			Total += Pool;
		}

		TArray<int64> Shares;
		Shares.SetNumZeroed(Pools.Num());
		if (Total <= 0 || Amount <= 0)
		{
			return Shares;
		}

		TArray<TPair<double, int32>> Remainders;
		int64 Assigned = 0;
		for (int32 Index = 0; Index < Pools.Num(); ++Index)
		{
			const double Exact = static_cast<double>(Amount) * Pools[Index] / Total;
			Shares[Index] = FMath::Min(Pools[Index], static_cast<int64>(Exact));
			Assigned += Shares[Index];
			// A pinch of noise so ties don't always favour the same pools.
			Remainders.Emplace(Exact - Shares[Index] + FMath::FRand() * 1e-6, Index);
		}
		Remainders.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key > B.Key; });
		for (int32 I = 0; Assigned < Amount && I < Remainders.Num() * 2; ++I)
		{
			const int32 Index = Remainders[I % Remainders.Num()].Value;
			if (Shares[Index] < Pools[Index])
			{
				++Shares[Index];
				++Assigned;
			}
		}
		return Shares;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Heap shape

double ACookieSettledManager::FPileShape::HeightAt(const FVector2D& P) const
{
	const double Angle = FMath::Atan2(P.Y, P.X);
	const double Edge = Radius * (1.0 + 0.1 * FMath::Sin(2.0 * Angle + Wobble2) + 0.07 * FMath::Sin(3.0 * Angle + Wobble3));
	const double Base = 1.0 - FMath::Pow(P.Size() / Edge, Profile);
	if (Base <= 0.0)
	{
		return 0.0;
	}
	double Z = Height * Base;
	for (const FLump& Lump : Lumps)
	{
		Z += Lump.Rise * Base * FMath::Exp(-FVector2D::DistSquared(P, Lump.Centre) / (2.0 * FMath::Square(Lump.Size)));
	}
	return FMath::Max(Z, 0.0);
}

FVector ACookieSettledManager::FPileShape::NormalAt(const FVector2D& P) const
{
	const double Step = FMath::Max(2.0, Radius * 0.01);
	const double DX = (HeightAt(P + FVector2D(Step, 0.0)) - HeightAt(P - FVector2D(Step, 0.0))) / (2.0 * Step);
	const double DY = (HeightAt(P + FVector2D(0.0, Step)) - HeightAt(P - FVector2D(0.0, Step))) / (2.0 * Step);
	return FVector(-DX, -DY, 1.0).GetSafeNormal();
}

ACookieSettledManager::FPileShape ACookieSettledManager::ShapeFor(const FPile& Pile) const
{
	FPileShape Shape;
	Shape.Ground = Pile.Ground;
	Shape.Radius = GetPileRadius(Pile.Count);
	Shape.Height = Shape.Radius * HeightRatioFor(Shape.Radius);
	Shape.Profile = ProfileFor(Shape.Radius);

	// Same seed every time, so the heap keeps its character as it grows. Big heaps get bigger lumps: a mountain
	// of cookies is all shoulders and gullies, not a smooth dome.
	FRandomStream Random(Pile.Seed);
	Shape.Wobble2 = Random.FRandRange(0.0, UE_DOUBLE_TWO_PI);
	Shape.Wobble3 = Random.FRandRange(0.0, UE_DOUBLE_TWO_PI);
	const double Rugged = FMath::Clamp((Shape.Radius - 150.0) / 600.0, 0.0, 1.0);
	for (FLump& Lump : Shape.Lumps)
	{
		Lump.Centre = FVector2D(Random.FRandRange(-0.6, 0.6), Random.FRandRange(-0.6, 0.6)) * Shape.Radius;
		Lump.Size = Random.FRandRange(0.16, 0.4) * Shape.Radius;
		Lump.Rise = Random.FRandRange(-0.08 - 0.06 * Rugged, 0.18 + 0.1 * Rugged) * Shape.Height;
	}
	return Shape;
}

double ACookieSettledManager::ProfileFor(double Radius)
{
	// Small heaps are domes; big ones are rounded cones, steeper toward the foot like real heaps.
	return FMath::Lerp(2.0, 1.5, FMath::Clamp((Radius - 100.0) / 500.0, 0.0, 1.0));
}

double ACookieSettledManager::GetPileRadius(int64 Count) const
{
	// The heap is height h = k r with profile h (1 - (d/r)^p): volume = pi r^2 h p / (p + 2) = pi k r^3 p / (p + 2).
	// k and p depend on r (small heaps are flatter and rounder), so solve with a guess, then refine twice.
	const double Volume = FMath::Max<int64>(Count, 1) * static_cast<double>(UCookieGameSettings::Get().PileCookieVolume);
	double Radius = FMath::Pow(2.0 * Volume / (UE_DOUBLE_PI * PileHeightRatio), 1.0 / 3.0);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const double P = ProfileFor(Radius);
		Radius = FMath::Pow(Volume * (P + 2.0) / (UE_DOUBLE_PI * HeightRatioFor(Radius) * P), 1.0 / 3.0);
	}
	return Radius;
}

double ACookieSettledManager::HeightRatioFor(double Radius)
{
	// A handful of cookies lies almost flat; only a real heap reaches the full 45 degree slope.
	return FMath::Lerp(0.12, PileHeightRatio, FMath::Clamp((Radius - 20.0) / 80.0, 0.0, 1.0));
}

bool ACookieSettledManager::IsLargePile(const FPile& Pile) const
{
	return !Pile.bIndoor && GetPileRadius(Pile.Count) > UCookieGameSettings::Get().PileLargeRadius;
}

bool ACookieSettledManager::IsInsideHouse(const FVector& Location) const
{
	return UCookieGameSettings::Get().HouseInterior.IsInsideOrOn(Location);
}

// ---------------------------------------------------------------------------------------------------------------
// Setup

ACookieSettledManager::ACookieSettledManager()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ACookieSettledManager::BeginPlay()
{
	Super::BeginPlay();

	const UCookieGameSettings& Settings = UCookieGameSettings::Get();

	CookieMesh = Settings.Cookie.Mesh.LoadSynchronous();
	if (CookieMesh)
	{
		const FVector Extent = CookieMesh->GetBounds().BoxExtent * Settings.Cookie.Scale;
		CookieRadius = Extent.X;
		CookieThickness = Extent.Z;
	}
	PileCoreMaterial = Settings.PileCoreMaterial.LoadSynchronous();

	for (ECookieItem Item : TEnumRange<ECookieItem>())
	{
		const FCookieItemSpec& Spec = Settings.GetItem(Item);
		ItemInstances.Add(MakeInstances(*FString::Printf(TEXT("SettledItem%d"), static_cast<int32>(Item)), Spec.Launch));
		if (!Spec.Spouts.IsEmpty())
		{
			// Seen through ProducerVisuals instead (see AddItem); the instances still collide.
			ItemInstances.Last()->SetVisibility(false);
		}
		EmitAccumulators.Add(0.f);
		SettledSpoutCarry.AddDefaulted();
	}

	GhostInstances = MakeInstances(TEXT("GhostCookies"), Settings.Cookie);
	GhostInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Small, fast and short-lived: a shadow would hardly show, but moving casters cost shadow rendering.
	GhostInstances->SetCastShadow(false);
	GhostInstances->bAffectDistanceFieldLighting = false;
}

UInstancedStaticMeshComponent* ACookieSettledManager::MakeInstances(const TCHAR* Name, const FCookieLaunchSpec& Spec)
{
	// Unique names: piles come and go, so "CookiePileSurface3" can be wanted again while an older one still exists.
	const FName UniqueName = MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), FName(Name));
	UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(this, UniqueName);
	Instances->SetupAttachment(RootComponent);
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->SetStaticMesh(Spec.Mesh.LoadSynchronous());
	Instances->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Instances->RegisterComponent();

	if (Spec.bPlaceholderMaterial)
	{
		Instances->SetMaterial(0, UCookieGameSettings::Get().PlaceholderMaterial.LoadSynchronous());
		if (UMaterialInstanceDynamic* Material = Instances->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Spec.Color);
		}
	}
	return Instances;
}

// ---------------------------------------------------------------------------------------------------------------
// Loose cookies and items

void ACookieSettledManager::AddCookie(const FTransform& Transform)
{
	// Already spent while it was in the air.
	if (RemovalDebt > 0)
	{
		--RemovalDebt;
		return;
	}

	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	if (SettledCookieCount >= Settings.MaxPhysicalCookies)
	{
		AddToPileNear(Transform.GetLocation());
		return;
	}

	// Settled cookies live in chunks: adding an instance rebuilds its component's render data, so keeping
	// components small keeps that cost flat however many thousands of cookies there are.
	if (CookieChunks.IsEmpty() || CookieChunks.Last()->GetInstanceCount() >= CookiesPerChunk)
	{
		UInstancedStaticMeshComponent* Chunk = MakeInstances(*FString::Printf(TEXT("SettledCookies%d"), CookieChunks.Num()), Settings.Cookie);
		// Settled cookies are too thin to walk on nicely; the player wades through them.
		Chunk->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		CookieChunks.Add(Chunk);
	}
	CookieChunks.Last()->AddInstance(Transform, /*bWorldSpace=*/ true);
	++SettledCookieCount;
}

void ACookieSettledManager::AddItem(ECookieItem Item, const FTransform& Transform)
{
	ItemInstances[static_cast<int32>(Item)]->AddInstance(Transform, /*bWorldSpace=*/ true);

	// Big producers are drawn as ordinary meshes: there are few of them, and the kit walls' two-tone material
	// in the merged bakery comes out as noise when instanced. The instance stays for collision and bookkeeping.
	const FCookieItemSpec& Spec = UCookieGameSettings::Get().GetItem(Item);
	if (!Spec.Spouts.IsEmpty())
	{
		const FName Name = MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("SettledProducer"));
		UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(this, Name);
		Visual->SetupAttachment(RootComponent);
		Visual->SetMobility(EComponentMobility::Movable);
		Visual->SetStaticMesh(Spec.Launch.Mesh.LoadSynchronous());
		Visual->SetWorldTransform(Transform);
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->RegisterComponent();
		ProducerVisuals.Add(Visual);
	}
}

FTransform ACookieSettledManager::RemoveItemInstance(ECookieItem Item, int32 Index)
{
	UInstancedStaticMeshComponent* Instances = ItemInstances[static_cast<int32>(Item)];
	FTransform Transform;
	Instances->GetInstanceTransform(Index, Transform, /*bWorldSpace=*/ true);
	Instances->RemoveInstance(Index);
	return Transform;
}

void ACookieSettledManager::SetGrandmaPose(UStaticMesh* Pose)
{
	// Grandmas never settle as the placeholder cylinder anymore, so their instance slot takes the baked pose.
	if (Pose)
	{
		ItemInstances[static_cast<int32>(ECookieItem::Grandma)]->SetStaticMesh(Pose);
		bHasGrandmaPose = true;
	}
}

int32 ACookieSettledManager::GetSettledCookieCount() const
{
	return SettledCookieCount;
}

int32 ACookieSettledManager::GetSettledItemCount(ECookieItem Item) const
{
	const int32 Index = static_cast<int32>(Item);
	return ItemInstances.IsValidIndex(Index) ? ItemInstances[Index]->GetInstanceCount() : 0;
}

int64 ACookieSettledManager::GetLargestPileCount() const
{
	int64 Largest = 0;
	for (const TUniquePtr<FPile>& Pile : Piles)
	{
		Largest = FMath::Max(Largest, Pile->Count);
	}
	return Largest;
}

int32 ACookieSettledManager::GetPileSurfaceInstanceCount() const
{
	int32 Total = 0;
	for (const TUniquePtr<FPile>& Pile : Piles)
	{
		for (const UInstancedStaticMeshComponent* Chunk : Pile->Chunks)
		{
			Total += Chunk->GetInstanceCount();
		}
	}
	return Total;
}

// ---------------------------------------------------------------------------------------------------------------
// Heaps

ACookieSettledManager::FPile& ACookieSettledManager::CreatePile(const FVector& Ground)
{
	TUniquePtr<FPile>& Pile = Piles.Emplace_GetRef(MakeUnique<FPile>());
	Pile->Ground = Ground;
	Pile->Seed = FMath::Rand();
	Pile->bIndoor = IsInsideHouse(Ground + FVector(0.0, 0.0, 10.0));
	return *Pile;
}

void ACookieSettledManager::AddToPileNear(const FVector& Location, int64 Count)
{
	// Cookies already spent while they flew vanish as they land.
	if (RemovalDebt > 0)
	{
		const int64 Spent = FMath::Min(RemovalDebt, Count);
		RemovalDebt -= Spent;
		Count -= Spent;
	}
	if (Count <= 0)
	{
		return;
	}

	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	PiledCookies += Count;
	const bool bLandsIndoors = IsInsideHouse(Location);

	// Join the heap the cookies land on (the biggest, if heaps overlap), or failing that the nearest one close by
	// that isn't behind a wall. Indoor heaps stop growing at the cap, so cookies spread into new heaps across the
	// room instead of one heap filling it; but once there are already several full heaps around, the room is
	// simply filling up, and the cookies go onto the nearest of them.
	FPile* Best = nullptr;
	double BestScore = 0.0;
	FPile* NearestFull = nullptr;
	double NearestFullDist = 0.0;
	int32 FullNearby = 0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CookiePileMerge), false);
	Params.AddIgnoredActor(this);
	for (const TUniquePtr<FPile>& Candidate : Piles)
	{
		FPile& Pile = *Candidate;
		if (Pile.bIndoor != bLandsIndoors)
		{
			continue;
		}
		if (Pile.bIndoor && Pile.Count >= Settings.MaxCookiesPerPile)
		{
			const double Dist = FVector::Dist(Pile.Ground, Location);
			if (Dist < Settings.PileMergeRadius * 2.0)
			{
				++FullNearby;
				if (!NearestFull || Dist < NearestFullDist)
				{
					NearestFull = &Pile;
					NearestFullDist = Dist;
				}
			}
			continue;
		}
		const double Radius = GetPileRadius(Pile.Count);
		const double Dist = FVector2D::Distance(FVector2D(Pile.Ground), FVector2D(Location));
		const bool bOnIt = Dist < Radius * 0.95;
		if (!bOnIt && Dist > Settings.PileMergeRadius)
		{
			continue;
		}
		if (!bOnIt && FMath::Abs(Location.Z - Pile.Ground.Z) > 300.0)
		{
			continue;
		}
		if (!IsLargePile(Pile))
		{
			const FVector PileTop = Pile.Ground + FVector(0.0, 0.0, FMath::Max(10.0, Radius * HeightRatioFor(Radius) * 0.8));
			if (GetWorld()->LineTraceTestByObjectType(Location + FVector(0.0, 0.0, 5.0), PileTop, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				continue;
			}
		}
		// Landing on a heap beats being near one; among those, the biggest wins.
		const double Score = bOnIt ? 1e12 + static_cast<double>(Pile.Count) : 1e9 - Dist;
		if (!Best || Score > BestScore)
		{
			Best = &Pile;
			BestScore = Score;
		}
	}

	if (!Best && FullNearby >= 3)
	{
		Best = NearestFull;
	}
	if (Best)
	{
		Best->Count += Count;
		Best->bDirty = true;
		return;
	}

	// ...or start a new one where they land, on the floor or furniture below.
	FVector Target = Location;
	if (Count == 1)
	{
		const FVector2D Offset = FMath::RandPointInCircle(Settings.PileMergeRadius * 0.5f);
		Target += FVector(Offset.X, Offset.Y, 0.0);
	}
	FPile& Pile = CreatePile(FindPileGround(Location + FVector(0.0, 0.0, 20.0), Target + FVector(0.0, 0.0, 20.0)));
	Pile.Count = Count;
	Pile.bDirty = true;
	// Brand new heaps show their first cookies straight away.
	StartPileBuild(Pile);
}

FVector ACookieSettledManager::FindPileGround(const FVector& Source, const FVector& Target) const
{
	// Only floors, walls and furniture count: never a grandma's head, a flying toaster, or a frozen item
	// (which could be thawed or buried later and leave the pile floating).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CookiePileGround), false);
	Params.AddIgnoredActor(this);
	const FCollisionObjectQueryParams Static(ECC_WorldStatic);

	// Slide sideways from the source towards the target, stopping short of walls so the heap stays in the room.
	FVector Above = Target;
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Source, Target, Static, Params))
	{
		Above = Hit.Location - (Target - Source).GetSafeNormal() * 20.0;
	}

	// Then drop onto whatever is below.
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Above, Above - FVector(0.0, 0.0, 5000.0), Static, Params))
	{
		return Hit.ImpactPoint;
	}
	return FVector(Above.X, Above.Y, UCookieGameSettings::Get().GroundZ);
}

double ACookieSettledManager::GetLandingHeight(const FVector& Location) const
{
	// The ground (or roof, or table) under the point...
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CookieLanding), false);
	Params.AddIgnoredActor(this);
	FHitResult Hit;
	double Z = UCookieGameSettings::Get().GroundZ;
	const FVector Top(Location.X, Location.Y, Location.Z + 3000.0);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Top, Top - FVector(0.0, 0.0, 10000.0), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		Z = Hit.ImpactPoint.Z;
	}

	// ...or the top of any heap over it.
	for (const TUniquePtr<FPile>& Pile : Piles)
	{
		const FPileShape Shape = ShapeFor(*Pile);
		const FVector2D Local = FVector2D(Location) - FVector2D(Pile->Ground);
		if (Local.SizeSquared() < FMath::Square(Shape.Radius * 1.2))
		{
			Z = FMath::Max(Z, Pile->Ground.Z + Shape.HeightAt(Local) + CookieThickness);
		}
	}
	return Z;
}

void ACookieSettledManager::RemovePile(int32 Index)
{
	FPile& Pile = *Piles[Index];
	for (UInstancedStaticMeshComponent* Chunk : Pile.Chunks)
	{
		PileComponents.Remove(Chunk);
		Chunk->DestroyComponent();
	}
	if (Pile.Core)
	{
		PileComponents.Remove(Pile.Core);
		Pile.Core->DestroyComponent();
	}
	Piles.RemoveAt(Index);
}

void ACookieSettledManager::MergeOverlappingPiles()
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	bool bMerged = true;
	while (bMerged)
	{
		bMerged = false;
		for (int32 A = 0; A < Piles.Num() && !bMerged; ++A)
		{
			for (int32 B = A + 1; B < Piles.Num() && !bMerged; ++B)
			{
				FPile& PA = *Piles[A];
				FPile& PB = *Piles[B];
				if (PA.bIndoor || PB.bIndoor)
				{
					continue;
				}
				const double RA = GetPileRadius(PA.Count);
				const double RB = GetPileRadius(PB.Count);
				const double Dist = FVector2D::Distance(FVector2D(PA.Ground), FVector2D(PB.Ground));
				if (Dist > (RA + RB) * 0.6 || FMath::Abs(PA.Ground.Z - PB.Ground.Z) > FMath::Max(RA, RB))
				{
					continue;
				}
				// The bigger heap swallows the smaller, sliding toward it by their size ratio.
				FPile& Keep = PA.Count >= PB.Count ? PA : PB;
				FPile& Gone = PA.Count >= PB.Count ? PB : PA;
				const double W = static_cast<double>(Gone.Count) / static_cast<double>(Keep.Count + Gone.Count);
				Keep.Ground.X = FMath::Lerp(Keep.Ground.X, Gone.Ground.X, W);
				Keep.Ground.Y = FMath::Lerp(Keep.Ground.Y, Gone.Ground.Y, W);
				Keep.Ground.Z = FMath::Min(Keep.Ground.Z, Gone.Ground.Z);
				Keep.Count += Gone.Count;
				Keep.bDirty = true;
				RemovePile(&Gone == &PA ? A : B);
				bMerged = true;
			}
		}
	}

	// A heap that started on a roof or a table and outgrew it now stands on the ground.
	for (const TUniquePtr<FPile>& Pile : Piles)
	{
		if (IsLargePile(*Pile) && Pile->Ground.Z > Settings.GroundZ + 30.0)
		{
			Pile->Ground.Z = Settings.GroundZ;
			Pile->bDirty = true;
		}
	}
}

void ACookieSettledManager::RefreshDirtyPiles()
{
	MergeOverlappingPiles();
	for (const TUniquePtr<FPile>& Pile : Piles)
	{
		if (!Pile->bDirty || Pile->bBuilding)
		{
			continue;
		}
		Pile->bDirty = false;

		// Re-lay the surface once the heap has grown or shrunk noticeably, so it changes in steps rather than crawling.
		// Small heaps re-lay every time: each cookie is visible there.
		const double Radius = GetPileRadius(Pile->Count);
		if (Pile->Count <= 30 || Radius > Pile->BuiltRadius * 1.02 || Radius < Pile->BuiltRadius * 0.98)
		{
			StartPileBuild(*Pile);
		}
	}
}

int32 ACookieSettledManager::SurfaceCookieCount(const FPile& Pile, const FPileShape& Shape, int32& OutClutter, int32& OutSkirt) const
{
	// Enough cookies to cover the footprint Coverage times over, but never more than the heap holds, plus a second
	// layer just under them (on heaps tall enough to have one) so the gaps between cookies show more cookies.
	// The returned surface count includes both layers: the top layer is the first 1 / (1 + UnderLayer) of it.
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	const int64 Top = FMath::Min<int64>(Pile.Count, FMath::CeilToInt64(Settings.PileSurfaceCoverage * FMath::Square(Shape.Radius / CookieRadius)));
	const bool bUnder = Shape.Height > CookieThickness * 3.0;
	const int64 Surface = bUnder ? FMath::Min<int64>(Pile.Count, FMath::CeilToInt64(Top * (1.0 + UnderLayer))) : Top;
	OutClutter = static_cast<int32>(FMath::Min<int64>(Pile.Count - Surface, FMath::CeilToInt64(Top * 0.12)));
	OutSkirt = static_cast<int32>(FMath::Min<int64>(Pile.Count - Surface - OutClutter, FMath::CeilToInt64(Top * Settings.PileSkirtFraction)));
	return static_cast<int32>(Surface);
}

bool ACookieSettledManager::MakeSurfaceCookie(const FPile& Pile, const FPileShape& Shape, int32 Index, int32 SurfaceCount, int32 ClutterCount, FTransform& Out) const
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	// Each cookie has its own random stream, so a heap can be laid out in any order (and over several frames).
	FRandomStream Random(HashCombine(static_cast<uint32>(Pile.Seed), static_cast<uint32>(Index) * 2654435761u));
	const double Radius = Shape.Radius;
	const bool bLarge = !Pile.bIndoor && Radius > Settings.PileLargeRadius;

	FVector Position;
	FVector Normal = FVector::UpVector;
	double Messiness = Settings.PileSurfaceJitter;

	if (Index < SurfaceCount)
	{
		// The surface: an evenly spread spiral, then shuffled by up to half a cookie so no pattern shows. The top
		// layer comes first; the rest is a second spiral a little deeper, turned so it fills the top layer's gaps.
		const bool bTallEnough = Shape.Height > CookieThickness * 3.0;
		const int32 TopCount = bTallEnough ? FMath::Max(1, FMath::CeilToInt32(SurfaceCount / (1.0 + UnderLayer))) : SurfaceCount;
		const bool bUnder = Index >= TopCount;
		const int32 LayerIndex = bUnder ? Index - TopCount : Index;
		const int32 LayerCount = bUnder ? FMath::Max(1, SurfaceCount - TopCount) : TopCount;
		const double GoldenAngle = UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
		const double Phase = static_cast<double>(Pile.Seed % 6283) * 0.001 + (bUnder ? 1.7 : 0.0);
		const double Distance = Radius * (bUnder ? 1.02 : 1.12) * FMath::Sqrt((LayerIndex + 0.5) / LayerCount);
		const double Angle = Phase + LayerIndex * GoldenAngle;
		const FVector2D P = FVector2D(Distance * FMath::Cos(Angle), Distance * FMath::Sin(Angle))
			+ FVector2D(Random.FRandRange(-0.5, 0.5), Random.FRandRange(-0.5, 0.5)) * CookieRadius;
		const double Z = Shape.HeightAt(P);
		if (Z <= 0.0 && Distance > Radius * 0.5)
		{
			return false;
		}
		Normal = Shape.NormalAt(P);
		const double Lift = bUnder ? Random.FRandRange(-0.6, 0.5) : Random.FRandRange(0.6, 2.2);
		Position = Shape.Ground + FVector(P.X, P.Y, Z) + Normal * CookieThickness * Lift;
		if (bUnder)
		{
			Messiness *= 1.5;
		}
	}
	else if (Index < SurfaceCount + ClutterCount)
	{
		// Clutter: a loose top layer, lying any old way on the others.
		const FVector2D P = FVector2D(Random.FRandRange(-1.0, 1.0), Random.FRandRange(-1.0, 1.0)) * Radius * 0.8;
		const double Z = Shape.HeightAt(P);
		if (Z <= 0.0)
		{
			return false;
		}
		Normal = Shape.NormalAt(P);
		Position = Shape.Ground + FVector(P.X, P.Y, Z) + Normal * CookieThickness * Random.FRandRange(2.0, 3.5);
		Messiness *= 2.0;
	}
	else
	{
		// The skirt: cookies that slid off, lying on whatever floor is around the base.
		const double Angle = Random.FRandRange(0.0, UE_DOUBLE_TWO_PI);
		const FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector2D P = Dir * Radius * Random.FRandRange(0.95, 1.4);
		Messiness *= 0.5;
		if (bLarge)
		{
			Position = Shape.Ground + FVector(P.X, P.Y, CookieThickness);
		}
		else
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CookiePileSkirt), false);
			Params.AddIgnoredActor(this);
			const FVector Above = Shape.Ground + FVector(P.X, P.Y, Shape.Height + 20.0);
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByObjectType(Hit, Above, Above - FVector(0.0, 0.0, Shape.Height + 60.0), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				return false;
			}
			Position = Hit.ImpactPoint + FVector(0.0, 0.0, CookieThickness);
			Normal = Hit.ImpactNormal;
		}
	}

	// Mountains pass straight through the house shells around them, but never show inside the player's house.
	if (!Pile.bIndoor && IsInsideHouse(Position))
	{
		return false;
	}
	// Small heaps leave out cookies that would sit inside a wall or furniture.
	if (!bLarge)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CookiePileClip), false);
		Params.AddIgnoredActor(this);
		const FCollisionShape Probe = FCollisionShape::MakeSphere(CookieRadius * 0.3);
		if (GetWorld()->OverlapAnyTestByObjectType(Position + FVector(0.0, 0.0, CookieRadius * 0.3 + 1.0), FQuat::Identity,
				FCollisionObjectQueryParams(ECC_WorldStatic), Probe, Params))
		{
			return false;
		}
	}

	// Mostly along the slope, some leaning steeply against their neighbours, a fifth upside down.
	FVector Tilt = (Normal + Random.VRand() * Messiness).GetSafeNormal();
	if (Random.FRand() < 0.15)
	{
		const FVector Sideways = FVector(Random.FRandRange(-1.0, 1.0), Random.FRandRange(-1.0, 1.0), 0.0).GetSafeNormal();
		Tilt = (Normal + Sideways * Random.FRandRange(0.8, 1.6)).GetSafeNormal();
	}
	if (Random.FRand() < 0.2)
	{
		Tilt = -Tilt;
	}
	const FRotator Rotation = FRotationMatrix::MakeFromZX(Tilt, Random.VRand()).Rotator();
	Out = FTransform(Rotation, Position, Settings.Cookie.Scale * Random.FRandRange(0.92, 1.08));
	return true;
}

void ACookieSettledManager::StartPileBuild(FPile& Pile)
{
	Pile.BuildShape = ShapeFor(Pile);
	int32 Clutter = 0;
	int32 Skirt = 0;
	const int32 Surface = SurfaceCookieCount(Pile, Pile.BuildShape, Clutter, Skirt);
	Pile.BuildTotal = Surface + Clutter + Skirt;
	Pile.BuildNext = 0;
	Pile.ApplyNextChunk = -1;
	Pile.Staging.Reset();
	Pile.Staging.Reserve(Pile.BuildTotal);
	Pile.bBuilding = true;

	// A brand-new heap gets its core (its collision) straight away, so nothing falls through while its cookies are
	// still being laid out. A growing heap swaps its core with its cookies, so the core never shows above them.
	if (Pile.Chunks.IsEmpty())
	{
		BuildPileCore(Pile, Pile.BuildShape);
	}

	// Small heaps are laid out at once: each new cookie shows there, and it's cheap. (Within a per-frame allowance:
	// past it, they wait their turn with the big ones, so a burst of new heaps can't stall a frame.)
	if (Pile.BuildTotal <= ImmediateBuildCookies && ImmediateCookiesThisFrame < ImmediateBuildCookies * 2)
	{
		ImmediateCookiesThisFrame += Pile.BuildTotal;
		FTransform Transform;
		for (int32 Index = 0; Index < Pile.BuildTotal; ++Index)
		{
			if (MakeSurfaceCookie(Pile, Pile.BuildShape, Index, Surface, Clutter, Transform))
			{
				Pile.Staging.Add(Transform);
			}
		}
		Pile.BuildNext = Pile.BuildTotal;
		BuildPileCore(Pile, Pile.BuildShape);
		Pile.ApplyNextChunk = 0;
		ApplyPileChunks(Pile, MAX_int32);
	}
}

void ACookieSettledManager::TickPileBuilds(double BudgetSeconds)
{
	// Big heaps are laid out a slice at a time, within a per-frame time budget: first their cookies are worked out,
	// then the new core goes in and the cookies are handed to their chunk components a few chunks per frame.
	const double Deadline = FPlatformTime::Seconds() + BudgetSeconds;
	for (const TUniquePtr<FPile>& PilePtr : Piles)
	{
		FPile& Pile = *PilePtr;
		if (!Pile.bBuilding)
		{
			continue;
		}
		int32 Clutter = 0;
		int32 Skirt = 0;
		const int32 Surface = SurfaceCookieCount(Pile, Pile.BuildShape, Clutter, Skirt);
		FTransform Transform;
		while (Pile.BuildNext < Pile.BuildTotal)
		{
			const int32 End = FMath::Min(Pile.BuildNext + 512, Pile.BuildTotal);
			for (; Pile.BuildNext < End; ++Pile.BuildNext)
			{
				if (MakeSurfaceCookie(Pile, Pile.BuildShape, Pile.BuildNext, Surface, Clutter, Transform))
				{
					Pile.Staging.Add(Transform);
				}
			}
			if (FPlatformTime::Seconds() > Deadline)
			{
				return;
			}
		}
		if (Pile.ApplyNextChunk < 0)
		{
			BuildPileCore(Pile, Pile.BuildShape);
			Pile.ApplyNextChunk = 0;
		}
		while (!ApplyPileChunks(Pile, 1))
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				return;
			}
		}
	}
}

UInstancedStaticMeshComponent* ACookieSettledManager::MakeSurfaceChunk(const FPile& Pile)
{
	UInstancedStaticMeshComponent* Chunk = MakeInstances(TEXT("CookiePileSurface"), UCookieGameSettings::Get().Cookie);
	// The core under the cookies does the colliding; the cookies are only seen.
	Chunk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Hundreds of thousands of tiny cookies would swamp Lumen's scene and distance fields; the core stands in for them.
	Chunk->bAffectDistanceFieldLighting = false;
	Chunk->bAffectDynamicIndirectLighting = false;
	PileComponents.Add(Chunk);
	return Chunk;
}

bool ACookieSettledManager::ApplyPileChunks(FPile& Pile, int32 MaxChunks)
{
	const int32 Needed = FMath::DivideAndRoundUp(Pile.Staging.Num(), CookiesPerSurfaceChunk);
	// Cookies shadow each other: without it a heap reads as a flat, evenly lit dome. (Nanite draws the shadows.)
	const bool bCastShadow = true;
	int32 Done = 0;
	while (Pile.ApplyNextChunk < Needed && Done < MaxChunks)
	{
		const int32 ChunkIndex = Pile.ApplyNextChunk;
		if (!Pile.Chunks.IsValidIndex(ChunkIndex))
		{
			Pile.Chunks.Add(MakeSurfaceChunk(Pile));
		}
		UInstancedStaticMeshComponent* Chunk = Pile.Chunks[ChunkIndex];
		const int32 First = ChunkIndex * CookiesPerSurfaceChunk;
		const int32 Num = FMath::Min(CookiesPerSurfaceChunk, Pile.Staging.Num() - First);
		TArray<FTransform> Slice(Pile.Staging.GetData() + First, Num);
		if (Chunk->GetInstanceCount() == Num)
		{
			Chunk->BatchUpdateInstancesTransforms(0, Slice, /*bWorldSpace=*/ true, /*bMarkRenderStateDirty=*/ true, /*bTeleport=*/ true);
		}
		else
		{
			Chunk->ClearInstances();
			Chunk->AddInstances(Slice, /*bShouldReturnIndices=*/ false, /*bWorldSpace=*/ true);
		}
		if (Chunk->CastShadow != bCastShadow)
		{
			Chunk->SetCastShadow(bCastShadow);
		}
		++Pile.ApplyNextChunk;
		++Done;
	}
	if (Pile.ApplyNextChunk < Needed)
	{
		return false;
	}

	// Chunks the smaller layout no longer needs go away.
	while (Pile.Chunks.Num() > Needed)
	{
		UInstancedStaticMeshComponent* Spare = Pile.Chunks.Pop();
		PileComponents.Remove(Spare);
		Spare->DestroyComponent();
	}
	Pile.bBuilding = false;
	Pile.BuiltRadius = Pile.BuildShape.Radius;
	Pile.Staging.Empty();
	return true;
}

void ACookieSettledManager::BuildPileCore(FPile& Pile, const FPileShape& Shape)
{
	if (Shape.Radius < MinCoreRadius)
	{
		if (Pile.Core)
		{
			Pile.Core->SetVisibility(false);
			Pile.Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		return;
	}

	if (!Pile.Core)
	{
		const FName Name = MakeUniqueObjectName(this, UProceduralMeshComponent::StaticClass(), TEXT("CookiePileCore"));
		Pile.Core = NewObject<UProceduralMeshComponent>(this, Name);
		Pile.Core->SetupAttachment(RootComponent);
		Pile.Core->SetMobility(EComponentMobility::Movable);
		Pile.Core->bUseAsyncCooking = true;
		Pile.Core->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Pile.Core->RegisterComponent();
		PileComponents.Add(Pile.Core);
	}

	// The core follows the heap's exact shape just under the surface cookies, so anything showing through a gap
	// reads as more cookie, its collision is where the cookies appear to be, and its shadow is the heap's shadow.
	const bool bLarge = IsLargePile(Pile);
	const int32 Rings = bLarge ? 36 : 14;
	const int32 Segments = bLarge ? 72 : 28;
	// Under both cookie layers.
	const double Sink = CookieThickness * 1.8;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	Vertices.Reserve(1 + Rings * Segments);
	Vertices.Add(FVector(0.0, 0.0, Shape.HeightAt(FVector2D::ZeroVector) - Sink));
	for (int32 Ring = 1; Ring <= Rings; ++Ring)
	{
		// Rings bunch up toward the rim, where the slope changes fastest, and reach past the wobbly outline.
		const double Distance = Shape.Radius * 1.25 * FMath::Pow(static_cast<double>(Ring) / Rings, 0.85);
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const double Angle = UE_DOUBLE_TWO_PI * Segment / Segments;
			const FVector2D P(Distance * FMath::Cos(Angle), Distance * FMath::Sin(Angle));
			// Near the rim the surface cookies thin out, so the core drops away faster there and stays hidden.
			const double Z = Shape.HeightAt(P);
			const double RimSink = Sink + CookieThickness * 3.0 * FMath::Clamp(1.0 - Z / FMath::Max(Shape.Height * 0.35, 1.0), 0.0, 1.0);
			Vertices.Add(FVector(P.X, P.Y, Z > 0.0 ? Z - RimSink : -RimSink - Sink));
		}
	}
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const int32 Next = (Segment + 1) % Segments;
		Triangles.Append({ 0, 1 + Next, 1 + Segment });
	}
	for (int32 Ring = 1; Ring < Rings; ++Ring)
	{
		const int32 Inner = 1 + (Ring - 1) * Segments;
		const int32 Outer = 1 + Ring * Segments;
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const int32 Next = (Segment + 1) % Segments;
			Triangles.Append({ Inner + Segment, Inner + Next, Outer + Segment });
			Triangles.Append({ Inner + Next, Outer + Next, Outer + Segment });
		}
	}
	Normals.Reserve(Vertices.Num());
	UVs.Reserve(Vertices.Num());
	for (const FVector& Vertex : Vertices)
	{
		Normals.Add(Shape.NormalAt(FVector2D(Vertex)));
		UVs.Add(FVector2D(Vertex) / 100.0);
	}

	Pile.Core->SetWorldLocation(Shape.Ground);
	Pile.Core->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision=*/ true);
	if (PileCoreMaterial)
	{
		Pile.Core->SetMaterial(0, PileCoreMaterial);
	}
	Pile.Core->SetVisibility(true);
	Pile.Core->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Pile.Core->SetCollisionResponseToChannel(ECC_Pawn, Shape.Radius > WalkablePileRadius ? ECR_Block : ECR_Ignore);
}

// ---------------------------------------------------------------------------------------------------------------
// Spending

void ACookieSettledManager::RemoveCookies(int64 Amount)
{
	// The pools: all loose cookies, then each pile. Each gives up a share the size of its part of the total.
	TArray<int64> Pools;
	Pools.Add(SettledCookieCount);
	for (const TUniquePtr<FPile>& Pile : Piles)
	{
		Pools.Add(Pile->Count);
	}
	const int64 FromWorld = FMath::Min(Amount, static_cast<int64>(SettledCookieCount) + PiledCookies);
	const TArray<int64> Shares = SplitProportionally(Pools, FromWorld);

	RemoveLooseCookies(static_cast<int32>(Shares[0]));

	// Piles shrink by their share (their surface is re-laid lower); emptied ones go away.
	// Backwards, so removing one doesn't shift those still to visit.
	for (int32 PileIndex = Piles.Num() - 1; PileIndex >= 0; --PileIndex)
	{
		const int64 Take = Shares[1 + PileIndex];
		if (Take <= 0)
		{
			continue;
		}
		FPile& Pile = *Piles[PileIndex];
		Pile.Count -= Take;
		PiledCookies -= Take;
		if (Pile.Count <= 0)
		{
			RemovePile(PileIndex);
		}
		else
		{
			Pile.bDirty = true;
		}
	}

	// The rest were bought with cookies still in the air: they vanish as they land.
	const ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>();
	const int64 InAir = (GameMode ? GameMode->GetFlyingCookieCount() : 0) + GhostPayloadInFlight;
	RemovalDebt = FMath::Min(RemovalDebt + (Amount - FromWorld), InAir);
}

void ACookieSettledManager::RemoveLooseCookies(int32 Count)
{
	if (Count <= 0 || !CookieMesh)
	{
		return;
	}

	// Bucket cookies into cookie-sized columns. Within a column the highest cookie is layer 0, the one
	// under it layer 1, and so on. Removing all the layer 0s (in random order) before any layer 1s
	// takes cookies off the top everywhere, so nothing is left floating and every spot thins evenly.
	const double Cell = FMath::Max(CookieRadius, 1.0);

	struct FLoose
	{
		int32 Chunk;
		int32 Index;
		double Z;
		int32 Layer = 0;
		float Shuffle = 0.f;
	};
	TArray<FLoose> Loose;
	Loose.Reserve(SettledCookieCount);
	TMap<FIntPoint, TArray<int32>> Columns;
	for (int32 ChunkIndex = 0; ChunkIndex < CookieChunks.Num(); ++ChunkIndex)
	{
		const UInstancedStaticMeshComponent* Chunk = CookieChunks[ChunkIndex];
		for (int32 Index = 0; Index < Chunk->GetInstanceCount(); ++Index)
		{
			FTransform Transform;
			Chunk->GetInstanceTransform(Index, Transform, /*bWorldSpace=*/ true);
			const FVector Location = Transform.GetLocation();
			const FIntPoint Column(FMath::FloorToInt32(Location.X / Cell), FMath::FloorToInt32(Location.Y / Cell));
			Columns.FindOrAdd(Column).Add(Loose.Num());
			Loose.Add({ ChunkIndex, Index, Location.Z, 0, FMath::FRand() });
		}
	}
	for (TPair<FIntPoint, TArray<int32>>& Column : Columns)
	{
		Column.Value.Sort([&Loose](int32 A, int32 B) { return Loose[A].Z > Loose[B].Z; });
		for (int32 Layer = 0; Layer < Column.Value.Num(); ++Layer)
		{
			Loose[Column.Value[Layer]].Layer = Layer;
		}
	}
	Loose.Sort([](const FLoose& A, const FLoose& B) { return A.Layer != B.Layer ? A.Layer < B.Layer : A.Shuffle < B.Shuffle; });

	TArray<TArray<int32>> PerChunk;
	PerChunk.SetNum(CookieChunks.Num());
	for (int32 I = 0; I < FMath::Min(Count, Loose.Num()); ++I)
	{
		PerChunk[Loose[I].Chunk].Add(Loose[I].Index);
	}
	for (int32 ChunkIndex = 0; ChunkIndex < CookieChunks.Num(); ++ChunkIndex)
	{
		if (!PerChunk[ChunkIndex].IsEmpty())
		{
			CookieChunks[ChunkIndex]->RemoveInstances(PerChunk[ChunkIndex]);
			SettledCookieCount -= PerChunk[ChunkIndex].Num();
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Flying cookies

void ACookieSettledManager::LaunchGhostCookie(const FVector& Start, const FVector& Velocity)
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	const double GravityZ = GetWorld()->GetGravityZ();
	auto PositionAt = [&](double T) { return Start + Velocity * T + FVector(0.0, 0.0, 0.5 * GravityZ * T * T); };

	FGhost& Ghost = Ghosts.AddDefaulted_GetRef();
	Ghost.Start = Start;
	Ghost.Velocity = Velocity;
	Ghost.Spin0 = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Quaternion();
	Ghost.SpinAxis = FMath::VRand();
	Ghost.SpinSpeed = FMath::DegreesToRadians(Settings.Cookie.TumbleDegreesPerSecond);
	++GhostPayloadInFlight;

	// Work out once where the arc meets the house or anything settled, so flying costs nothing but a transform update.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CookieGhostArc), false);
	const FCollisionObjectQueryParams Static(ECC_WorldStatic);
	constexpr double Step = 1.0 / 30.0;
	constexpr double MaxSeconds = 5.0;
	FVector Previous = Start;
	for (double T = Step; T <= MaxSeconds; T += Step)
	{
		const FVector Next = PositionAt(T);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Previous, Next, Static, Params))
		{
			Ghost.LandTime = T - Step + Step * Hit.Time;
			const FRotator Rotation = FRotationMatrix::MakeFromZX(Hit.ImpactNormal, FMath::VRand()).Rotator();
			Ghost.Landing = FTransform(Rotation, Hit.ImpactPoint + Hit.ImpactNormal * CookieThickness, Settings.Cookie.Scale);
			return;
		}
		Previous = Next;
	}
	// Fell out of the world: it lands wherever it got to.
	Ghost.LandTime = MaxSeconds;
	Ghost.Landing = FTransform(Ghost.Spin0, Previous, Settings.Cookie.Scale);
}

double ACookieSettledManager::GetPileRadiusAt(const FVector& Location) const
{
	double Best = 0.0;
	for (const TUniquePtr<FPile>& Pile : Piles)
	{
		const double Radius = GetPileRadius(Pile->Count);
		if (FVector2D::DistSquared(FVector2D(Pile->Ground), FVector2D(Location)) < FMath::Square(Radius))
		{
			Best = FMath::Max(Best, Radius);
		}
	}
	return Best;
}

void ACookieSettledManager::LaunchStreamCookie(const FVector& Start, const FVector& Target, const FVector& Anchor, float FlightSeconds, int64 Payload)
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	const double T = FMath::Max(0.2, static_cast<double>(FlightSeconds));
	const double GravityZ = GetWorld()->GetGravityZ();

	// The arc that lands exactly on Target after T seconds: no tracing needed, the heap is already there.
	FGhost& Ghost = Ghosts.AddDefaulted_GetRef();
	Ghost.Start = Start;
	Ghost.Velocity = (Target - Start) / T - FVector(0.0, 0.0, 0.5 * GravityZ * T);
	Ghost.Spin0 = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Quaternion();
	Ghost.SpinAxis = FMath::VRand();
	Ghost.SpinSpeed = FMath::DegreesToRadians(Settings.Cookie.TumbleDegreesPerSecond);
	Ghost.LandTime = T;
	Ghost.Landing = FTransform(FRotationMatrix::MakeFromZX(FVector::UpVector + FMath::VRand() * 0.3, FMath::VRand()).Rotator(), Target, Settings.Cookie.Scale);
	Ghost.Payload = Payload;
	Ghost.bToPile = true;
	Ghost.Anchor = Anchor;
	GhostPayloadInFlight += Payload;
}

void ACookieSettledManager::EmitSpouts(ECookieItem Item, const FTransform& ItemTransform, float DeltaSeconds, float* Carry)
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	const FCookieItemSpec& Spec = Settings.GetItem(Item);
	ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>();
	if (!GameMode)
	{
		return;
	}
	const bool bGhostsFull = Ghosts.Num() >= Settings.MaxGhostCookies;

	for (int32 SpoutIndex = 0; SpoutIndex < Spec.Spouts.Num(); ++SpoutIndex)
	{
		const FCookieSpout& Spout = Spec.Spouts[SpoutIndex];
		float& Pending = Carry[SpoutIndex];
		Pending += Settings.GetProduction(Item) * Spout.Share * DeltaSeconds;
		if (Pending < 1.f)
		{
			continue;
		}

		// The spout feeds the heap at its target. Each cookie lands somewhere on that heap's current surface (on a
		// new or small heap, within TargetSpread), so the heap grows up and out evenly.
		// The anchor is on whatever is under the target (an item still flying has its target up in the air with it).
		FVector Anchor = ItemTransform.TransformPosition(Spout.Target);
		Anchor.Z = GetLandingHeight(Anchor);
		const double HeapRadius = GetPileRadiusAt(Anchor);
		const double Spread = HeapRadius > 0.0 ? FMath::Max(HeapRadius * 0.85, 120.0) : Spout.TargetSpread * 0.4;
		auto RandomTarget = [&]()
		{
			const FVector2D Offset = FMath::RandPointInCircle(Spread);
			FVector Target = Anchor + FVector(Offset.X, Offset.Y, 0.0);
			Target.Z = GetLandingHeight(Target);
			return Target;
		};

		// Past the flying cookie budget, cookies go straight onto the heap (still banked, so the count stays true).
		if (bGhostsFull)
		{
			const int64 Whole = FMath::FloorToInt64(Pending);
			Pending -= static_cast<float>(Whole);
			GameMode->AddCookies(static_cast<double>(Whole));
			AddToPileNear(Anchor, Whole);
			continue;
		}

		// A steady stream of visible cookies; when the item makes more than that, each visible one carries several.
		const float Expected = Spout.VisibleCookiesPerSecond * DeltaSeconds;
		int32 Visible = FMath::FloorToInt32(Expected + FMath::FRand());
		const int64 Whole = FMath::FloorToInt64(Pending);
		Visible = static_cast<int32>(FMath::Min<int64>(Visible, Whole));
		if (Visible <= 0)
		{
			continue;
		}
		const int64 Each = Whole / Visible;
		int64 Left = Whole;
		for (int32 V = 0; V < Visible; ++V)
		{
			const int64 Payload = V == Visible - 1 ? Left : Each;
			Left -= Payload;
			const FVector Jitter(FMath::FRandRange(-1.f, 1.f) * Spout.StartJitter.X, FMath::FRandRange(-1.f, 1.f) * Spout.StartJitter.Y,
				FMath::FRandRange(-1.f, 1.f) * Spout.StartJitter.Z);
			const FVector Start = ItemTransform.TransformPosition(Spout.Start + Jitter);
			GameMode->AddCookies(static_cast<double>(Payload));
			LaunchStreamCookie(Start, RandomTarget(), Anchor, Spout.FlightSeconds * FMath::FRandRange(0.85f, 1.15f), Payload);
		}
		Pending -= static_cast<float>(Whole);
	}
}

void ACookieSettledManager::TickGhosts(float DeltaSeconds)
{
	if (Ghosts.IsEmpty() && GhostSlotsInUse == 0)
	{
		return;
	}

	struct FLanded
	{
		FTransform Transform;
		int64 Payload;
		bool bToPile;
		FVector Anchor;
	};
	TArray<FLanded> Landed;
	for (int32 Index = Ghosts.Num() - 1; Index >= 0; --Index)
	{
		FGhost& Ghost = Ghosts[Index];
		Ghost.Age += DeltaSeconds;
		if (Ghost.Age >= Ghost.LandTime)
		{
			Landed.Add({ Ghost.Landing, Ghost.Payload, Ghost.bToPile, Ghost.Anchor });
			GhostPayloadInFlight -= Ghost.Payload;
			Ghosts.RemoveAtSwap(Index);
		}
	}

	const double GravityZ = GetWorld()->GetGravityZ();
	const FVector Scale = UCookieGameSettings::Get().Cookie.Scale;
	// The instances are a pool that only ever grows: each frame just moves them. Adding or removing
	// instances rebuilds the component's render data, which cost the render thread tens of milliseconds
	// a frame with cookies landing and launching constantly. Slots past the live ghosts are shrunk away.
	const int32 Slots = FMath::Max(Ghosts.Num(), GhostSlotsInUse);
	if (Slots > GhostInstances->GetInstanceCount())
	{
		TArray<FTransform> Spare;
		Spare.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Slots - GhostInstances->GetInstanceCount() + 256);
		GhostInstances->AddInstances(Spare, /*bShouldReturnIndices=*/ false, /*bWorldSpace=*/ true);
	}

	TArray<FTransform> Transforms;
	Transforms.Reserve(Slots);
	for (const FGhost& Ghost : Ghosts)
	{
		const double T = Ghost.Age;
		const FVector Location = Ghost.Start + Ghost.Velocity * T + FVector(0.0, 0.0, 0.5 * GravityZ * T * T);
		// Tumbles in flight, easing into the way it will lie just before it lands.
		const FQuat Tumble = FQuat(Ghost.SpinAxis, Ghost.SpinSpeed * T) * Ghost.Spin0;
		const double Settle = FMath::SmoothStep(0.0, 1.0, (T - (Ghost.LandTime - 0.15)) / 0.15);
		Transforms.Emplace(FQuat::Slerp(Tumble, Ghost.Landing.GetRotation(), Settle), Location, Scale);
	}

	while (Transforms.Num() < Slots)
	{
		Transforms.Emplace(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
	}
	if (Slots > 0)
	{
		GhostInstances->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace=*/ true, /*bMarkRenderStateDirty=*/ true, /*bTeleport=*/ true);
	}
	GhostSlotsInUse = Ghosts.Num();

	for (const FLanded& Land : Landed)
	{
		if (Land.bToPile)
		{
			AddToPileNear(Land.Anchor, Land.Payload);
		}
		else
		{
			AddCookie(Land.Transform);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Ticking

void ACookieSettledManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Start = FPlatformTime::Seconds();
	ImmediateCookiesThisFrame = 0;

	EmitFromSettledItems(DeltaSeconds);
	TickGhosts(DeltaSeconds);
	const double Emitted = FPlatformTime::Seconds();

	// Resizing a pile rebuilds its collision, so batch growth a few times a second.
	PileRefreshTimer += DeltaSeconds;
	if (PileRefreshTimer >= UCookieGameSettings::Get().PileRefreshSeconds)
	{
		PileRefreshTimer = 0.f;
		RefreshDirtyPiles();
	}
	TickPileBuilds(UCookieGameSettings::Get().PileBuildBudgetMs / 1000.0);

	EmitMs += (Emitted - Start) * 1000.0;
	PileMs += (FPlatformTime::Seconds() - Emitted) * 1000.0;
}

void ACookieSettledManager::ConsumeTimings(double& OutEmitMs, double& OutPileMs)
{
	OutEmitMs = EmitMs;
	OutPileMs = PileMs;
	EmitMs = 0.0;
	PileMs = 0.0;
}

void ACookieSettledManager::EmitFromSettledItems(float DeltaSeconds)
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	for (ECookieItem Item : TEnumRange<ECookieItem>())
	{
		const int32 Index = static_cast<int32>(Item);
		UInstancedStaticMeshComponent* Instances = ItemInstances[Index];
		const int32 Count = Instances->GetInstanceCount();
		if (Count == 0)
		{
			continue;
		}
		const FCookieItemSpec& Spec = Settings.GetItem(Item);

		// Big producers: every settled one spews from its own spouts.
		if (!Spec.Spouts.IsEmpty())
		{
			const int32 Spouts = Spec.Spouts.Num();
			TArray<float>& Carry = SettledSpoutCarry[Index];
			if (Carry.Num() < Count * Spouts)
			{
				Carry.SetNumZeroed(Count * Spouts);
			}
			for (int32 Instance = 0; Instance < Count; ++Instance)
			{
				FTransform InstanceTransform;
				Instances->GetInstanceTransform(Instance, InstanceTransform, /*bWorldSpace=*/ true);
				EmitSpouts(Item, InstanceTransform, DeltaSeconds, Carry.GetData() + Instance * Spouts);
			}
			continue;
		}

		// N settled items that each make R cookies a second pop N * R cookies a second in total.
		// Pick random instances to pop them from instead of keeping a timer per instance.
		EmitAccumulators[Index] += Count * Settings.GetProduction(Item) * DeltaSeconds;
		const FBox MeshBox = Instances->GetStaticMesh()->GetBounds().GetBox();
		while (EmitAccumulators[Index] >= 1.f)
		{
			EmitAccumulators[Index] -= 1.f;

			FTransform InstanceTransform;
			Instances->GetInstanceTransform(FMath::RandRange(0, Count - 1), InstanceTransform, /*bWorldSpace=*/ true);
			const FVector Center = InstanceTransform.GetLocation();
			const double Top = MeshBox.TransformBy(InstanceTransform).Max.Z;
			ACookieLaunchable::PopCookie(GetWorld(), FVector(Center.X, Center.Y, Top));
		}
	}
}
