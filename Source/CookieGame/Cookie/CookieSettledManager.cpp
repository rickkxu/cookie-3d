#include "CookieSettledManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "CookieGameSettings.h"
#include "CookieLaunchable.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// Pile cores are squashed spheres centred on the ground, so the top half shows as a mound.
	const TCHAR* SpherePath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	constexpr double SphereRadius = 50.0;

	// Heaps are half as tall as they are wide.
	constexpr double PileHeightRatio = 0.5;

	// Heaps narrower than this (cm) have no core: a handful of cookies is just cookies.
	constexpr double MinCoreRadius = 35.0;
}

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

	for (ECookieItem Item : TEnumRange<ECookieItem>())
	{
		const FCookieItemSpec& Spec = Settings.GetItem(Item);
		ItemInstances.Add(MakeInstances(*FString::Printf(TEXT("SettledItem%d"), static_cast<int32>(Item)), Spec.Launch));
		EmitAccumulators.Add(0.f);
	}

	// Pile cores: collision only. Two layers of cookies make up what you see, so nothing but cookies shows,
	// even where furniture cuts into a heap.
	FCookieLaunchSpec PileSpec = Settings.Cookie;
	PileSpec.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(SpherePath));
	PileSpec.bPlaceholderMaterial = true;
	PileInstances = MakeInstances(TEXT("CookiePiles"), PileSpec);
	PileInstances->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	PileInstances->SetVisibility(false);
}

UInstancedStaticMeshComponent* ACookieSettledManager::MakeInstances(const TCHAR* Name, const FCookieLaunchSpec& Spec)
{
	UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(this, Name);
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

void ACookieSettledManager::AddCookie(const FTransform& Transform)
{
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

void ACookieSettledManager::AddToPileNear(const FVector& Location)
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	++PiledCookies;

	// Grow the nearest pile that still has room and isn't behind a wall...
	TArray<TPair<double, int32>> Nearby;
	const double MergeDistSq = FMath::Square(Settings.PileMergeRadius);
	for (int32 Index = 0; Index < Piles.Num(); ++Index)
	{
		const double DistSq = FVector::DistSquared(Piles[Index].Ground, Location);
		if (Piles[Index].Count < Settings.MaxCookiesPerPile && DistSq < MergeDistSq)
		{
			Nearby.Emplace(DistSq, Index);
		}
	}
	Nearby.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CookiePileMerge), false);
	Params.AddIgnoredActor(this);
	for (const TPair<double, int32>& Candidate : Nearby)
	{
		FPile& Pile = Piles[Candidate.Value];
		const FVector PileTop = Pile.Ground + FVector(0.0, 0.0, FMath::Max(10.0, GetPileRadius(Pile) * HeightRatioFor(GetPileRadius(Pile)) * 0.8));
		if (!GetWorld()->LineTraceTestByObjectType(Location, PileTop, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			++Pile.Count;
			Pile.bDirty = true;
			return;
		}
	}

	// ...or start a new one beside the source, on the floor or furniture below.
	const FVector2D Offset = FMath::RandPointInCircle(Settings.PileMergeRadius * 0.5f);

	FPile& Pile = Piles.AddDefaulted_GetRef();
	Pile.Ground = FindPileGround(Location, Location + FVector(Offset.X, Offset.Y, 0.0));
	Pile.Count = 1;
	Pile.Seed = FMath::Rand();
	PileInstances->AddInstance(GetPileCoreTransform(Pile), /*bWorldSpace=*/ true);

	UInstancedStaticMeshComponent* Surface = MakeInstances(*FString::Printf(TEXT("CookiePileSurface%d"), Piles.Num() - 1), Settings.Cookie);
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PileSurfaces.Add(Surface);
	BuildPileSurface(Piles.Num() - 1);
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
	return Above;
}

double ACookieSettledManager::GetPileRadius(const FPile& Pile) const
{
	// The heap is a paraboloid of height r * k: volume = pi * r^2 * h / 2 = pi * k * r^3 / 2.
	// k depends on r (small heaps are flatter), so solve with a full heap's k, then refine once.
	const double Volume = Pile.Count * static_cast<double>(UCookieGameSettings::Get().PileCookieVolume);
	const double Guess = FMath::Pow(2.0 * Volume / (UE_DOUBLE_PI * PileHeightRatio), 1.0 / 3.0);
	return FMath::Pow(2.0 * Volume / (UE_DOUBLE_PI * HeightRatioFor(Guess)), 1.0 / 3.0);
}

FTransform ACookieSettledManager::GetPileCoreTransform(const FPile& Pile) const
{
	// The core gives the heap collision and fills any gaps between surface cookies. It's a squashed sphere
	// sitting fully inside the paraboloid, so it never pokes through. Small heaps are just cookies.
	const double Radius = GetPileRadius(Pile);
	const double Scale = Radius < MinCoreRadius ? 0.05 : Radius / SphereRadius;
	return FTransform(FQuat::Identity, Pile.Ground, FVector(Scale * 0.8, Scale * 0.8, Scale * HeightRatioFor(Radius) * 0.7));
}

double ACookieSettledManager::HeightRatioFor(double Radius)
{
	// A handful of cookies lies almost flat; only a real heap reaches the full 45 degree slope.
	return FMath::Lerp(0.12, PileHeightRatio, FMath::Clamp((Radius - 20.0) / 80.0, 0.0, 1.0));
}

void ACookieSettledManager::BuildPileSurface(int32 Index)
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	FPile& Pile = Piles[Index];
	const double Radius = GetPileRadius(Pile);
	const double Height = Radius * HeightRatioFor(Radius);
	Pile.BuiltRadius = Radius;

	const FVector CookieExtent = CookieMesh->GetBounds().BoxExtent * Settings.Cookie.Scale;
	const double CookieRadius = CookieExtent.X;
	const double Thickness = CookieExtent.Z;

	// Same seed every rebuild, so cookies only shift a little as the heap grows.
	FRandomStream Random(Pile.Seed);

	// The heap's shape: a paraboloid with a wobbly outline and a few lumps on top, so no two heaps match
	// and none is a perfect dome.
	const double Wobble2 = Random.FRandRange(0.0, UE_DOUBLE_TWO_PI);
	const double Wobble3 = Random.FRandRange(0.0, UE_DOUBLE_TWO_PI);
	struct FLump { FVector2D Centre; double Size; double Rise; };
	FLump Lumps[3];
	for (FLump& Lump : Lumps)
	{
		Lump.Centre = FVector2D(Random.FRandRange(-0.5, 0.5), Random.FRandRange(-0.5, 0.5)) * Radius;
		Lump.Size = Random.FRandRange(0.25, 0.45) * Radius;
		Lump.Rise = Random.FRandRange(-0.08, 0.18) * Height;
	}
	auto HeightAt = [&](const FVector2D& P)
	{
		const double Angle = FMath::Atan2(P.Y, P.X);
		const double Edge = Radius * (1.0 + 0.1 * FMath::Sin(2.0 * Angle + Wobble2) + 0.07 * FMath::Sin(3.0 * Angle + Wobble3));
		const double Base = 1.0 - FVector2D::DistSquared(P, FVector2D::ZeroVector) / FMath::Square(Edge);
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
	};
	auto NormalAt = [&](const FVector2D& P)
	{
		constexpr double Step = 2.0;
		const double DX = (HeightAt(P + FVector2D(Step, 0.0)) - HeightAt(P - FVector2D(Step, 0.0))) / (2.0 * Step);
		const double DY = (HeightAt(P + FVector2D(0.0, Step)) - HeightAt(P - FVector2D(0.0, Step))) / (2.0 * Step);
		return FVector(-DX, -DY, 1.0).GetSafeNormal();
	};

	// Leave out cookies that would sit inside a wall or furniture.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CookiePileClip), false);
	Params.AddIgnoredActor(this);
	const FCollisionObjectQueryParams Static(ECC_WorldStatic);
	const FCollisionShape Probe = FCollisionShape::MakeSphere(CookieRadius * 0.3);
	auto IsClear = [&](const FVector& Position)
	{
		return !GetWorld()->OverlapAnyTestByObjectType(Position + FVector(0.0, 0.0, CookieRadius * 0.3 + 1.0), FQuat::Identity, Static, Probe, Params);
	};

	TArray<FTransform> Cookies;
	auto AddCookie = [&](const FVector& Position, const FVector& Normal, double Messiness)
	{
		if (!IsClear(Position))
		{
			return;
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
		Cookies.Emplace(Rotation, Position, Settings.Cookie.Scale * Random.FRandRange(0.92, 1.08));
	};

	// Enough cookies to cover the footprint Coverage times over, but never more than the heap holds.
	const int32 Surface = FMath::Min(Pile.Count, FMath::CeilToInt32(Settings.PileSurfaceCoverage * FMath::Square(Radius / CookieRadius)));
	const int32 Clutter = FMath::Min(Pile.Count - Surface, FMath::CeilToInt32(Surface * 0.12));
	const int32 Skirt = FMath::Min(Pile.Count - Surface - Clutter, FMath::CeilToInt32(Surface * Settings.PileSkirtFraction));
	Cookies.Reserve(Surface + Clutter + Skirt);

	// The surface: an evenly spread spiral, then shuffled by up to half a cookie so no pattern shows.
	const double GoldenAngle = UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
	const double Phase = Random.FRandRange(0.0, UE_DOUBLE_TWO_PI);
	for (int32 I = 0; I < Surface; ++I)
	{
		const double Distance = Radius * 1.12 * FMath::Sqrt((I + 0.5) / Surface);
		const double Angle = Phase + I * GoldenAngle;
		const FVector2D P = FVector2D(Distance * FMath::Cos(Angle), Distance * FMath::Sin(Angle))
			+ FVector2D(Random.FRandRange(-0.5, 0.5), Random.FRandRange(-0.5, 0.5)) * CookieRadius;
		const double Z = HeightAt(P);
		if (Z <= 0.0 && Distance > Radius * 0.5)
		{
			continue;
		}
		const FVector Normal = NormalAt(P);
		AddCookie(Pile.Ground + FVector(P.X, P.Y, Z) + Normal * Thickness * Random.FRandRange(0.6, 2.2), Normal, Settings.PileSurfaceJitter);
	}

	// An under-layer, a little deeper, so gaps between surface cookies show more cookies rather than a core.
	const int32 UnderLayer = FMath::RoundToInt32(Surface * 0.7);
	const double UnderPhase = Random.FRandRange(0.0, UE_DOUBLE_TWO_PI);
	for (int32 I = 0; I < UnderLayer && Height > Thickness * 3.0; ++I)
	{
		const double Distance = Radius * FMath::Sqrt((I + 0.5) / UnderLayer);
		const double Angle = UnderPhase + I * GoldenAngle;
		const FVector2D P = FVector2D(Distance * FMath::Cos(Angle), Distance * FMath::Sin(Angle))
			+ FVector2D(Random.FRandRange(-0.5, 0.5), Random.FRandRange(-0.5, 0.5)) * CookieRadius;
		const double Z = HeightAt(P) - Thickness * 1.5;
		if (Z <= Thickness)
		{
			continue;
		}
		const FVector Normal = NormalAt(P);
		AddCookie(Pile.Ground + FVector(P.X, P.Y, Z), Normal, Settings.PileSurfaceJitter * 1.5);
	}

	// Clutter: a loose top layer, lying any old way on the others.
	for (int32 I = 0; I < Clutter; ++I)
	{
		const FVector2D P = FVector2D(Random.FRandRange(-1.0, 1.0), Random.FRandRange(-1.0, 1.0)) * Radius * 0.8;
		const double Z = HeightAt(P);
		if (Z <= 0.0)
		{
			continue;
		}
		const FVector Normal = NormalAt(P);
		AddCookie(Pile.Ground + FVector(P.X, P.Y, Z) + Normal * Thickness * Random.FRandRange(2.0, 3.5), Normal, Settings.PileSurfaceJitter * 2.0);
	}

	// The skirt: cookies that slid off, lying on whatever floor is around the base.
	FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(CookiePileSkirt), false);
	GroundParams.AddIgnoredActor(this);
	for (int32 I = 0; I < Skirt; ++I)
	{
		const double Angle = Random.FRandRange(0.0, UE_DOUBLE_TWO_PI);
		const FVector Above = Pile.Ground + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * Radius * Random.FRandRange(0.95, 1.4)
			+ FVector(0.0, 0.0, Height + 20.0);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Above, Above - FVector(0.0, 0.0, Height + 60.0), Static, GroundParams))
		{
			AddCookie(Hit.ImpactPoint + FVector(0.0, 0.0, Thickness), Hit.ImpactNormal, Settings.PileSurfaceJitter * 0.5);
		}
	}

	UInstancedStaticMeshComponent* SurfaceInstances = PileSurfaces[Index];
	SurfaceInstances->ClearInstances();
	SurfaceInstances->AddInstances(Cookies, /*bShouldReturnIndices=*/ false, /*bWorldSpace=*/ true);
}

void ACookieSettledManager::RefreshDirtyPiles()
{
	for (int32 Index = 0; Index < Piles.Num(); ++Index)
	{
		FPile& Pile = Piles[Index];
		if (!Pile.bDirty)
		{
			continue;
		}
		Pile.bDirty = false;
		PileInstances->UpdateInstanceTransform(Index, GetPileCoreTransform(Pile), /*bWorldSpace=*/ true, /*bMarkRenderStateDirty=*/ true);

		// Re-lay the surface only once the heap has grown noticeably, so it settles in steps rather than crawling.
		// Small heaps re-lay every time: each new cookie is visible there.
		const double Radius = GetPileRadius(Pile);
		if (Pile.Count <= 30 || Radius > Pile.BuiltRadius * 1.08)
		{
			BuildPileSurface(Index);
		}
	}
}

void ACookieSettledManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Start = FPlatformTime::Seconds();

	EmitFromSettledItems(DeltaSeconds);
	const double Emitted = FPlatformTime::Seconds();

	// Resizing a pile rebuilds its collision, so batch growth a few times a second.
	PileRefreshTimer += DeltaSeconds;
	if (PileRefreshTimer >= UCookieGameSettings::Get().PileRefreshSeconds)
	{
		PileRefreshTimer = 0.f;
		RefreshDirtyPiles();
	}

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
	// N settled items that each emit every I seconds emit N / I cookies per second in total.
	// Pick random instances to pop them from instead of keeping a timer per instance.
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

		EmitAccumulators[Index] += Count / Settings.GetItem(Item).EmitIntervalSeconds * DeltaSeconds;
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
