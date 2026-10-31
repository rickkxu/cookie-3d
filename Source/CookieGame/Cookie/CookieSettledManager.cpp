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

	CookieInstances = MakeInstances(TEXT("SettledCookies"), Settings.Cookie);
	// Settled cookies are too thin to walk on nicely; the player wades through them.
	CookieInstances->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	for (ECookieItem Item : TEnumRange<ECookieItem>())
	{
		const FCookieItemSpec& Spec = Settings.GetItem(Item);
		ItemInstances.Add(MakeInstances(*FString::Printf(TEXT("SettledItem%d"), static_cast<int32>(Item)), Spec.Launch));
		EmitAccumulators.Add(0.f);
	}

	// Pile cores: cookie-dough colored, so any glimpse between surface cookies reads as more cookie.
	FCookieLaunchSpec PileSpec = Settings.Cookie;
	PileSpec.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(SpherePath));
	PileSpec.bPlaceholderMaterial = true;
	PileSpec.Color = Settings.Cookie.Color * Settings.PileCoreBrightness;
	PileInstances = MakeInstances(TEXT("CookiePiles"), PileSpec);
	PileInstances->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
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
	if (GetSettledCookieCount() >= UCookieGameSettings::Get().MaxPhysicalCookies)
	{
		AddToPileNear(Transform.GetLocation());
		return;
	}
	CookieInstances->AddInstance(Transform, /*bWorldSpace=*/ true);
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
	return CookieInstances ? CookieInstances->GetInstanceCount() : 0;
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
		const FVector PileTop = Pile.Ground + FVector(0.0, 0.0, FMath::Max(10.0, GetPileRadius(Pile) * PileHeightRatio * 0.8));
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
	// The heap is a paraboloid half as tall as it is wide (45 degree slope at the edge, about how loose
	// cookies settle): volume = pi * r^2 * h / 2 = pi * r^3 / 4.
	const double Volume = Pile.Count * static_cast<double>(UCookieGameSettings::Get().PileCookieVolume);
	return FMath::Pow(4.0 * Volume / UE_DOUBLE_PI, 1.0 / 3.0);
}

FTransform ACookieSettledManager::GetPileCoreTransform(const FPile& Pile) const
{
	// The core gives the heap collision and fills any gaps between surface cookies. It's a squashed sphere
	// sitting fully inside the paraboloid, so it never pokes through. Small heaps are just cookies.
	const double Radius = GetPileRadius(Pile);
	const double Scale = Radius < MinCoreRadius ? 0.05 : Radius / SphereRadius;
	return FTransform(FQuat::Identity, Pile.Ground, FVector(Scale * 0.85, Scale * 0.85, Scale * PileHeightRatio * 0.8));
}

void ACookieSettledManager::BuildPileSurface(int32 Index)
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	FPile& Pile = Piles[Index];
	const double Radius = GetPileRadius(Pile);
	const double Height = Radius * PileHeightRatio;
	Pile.BuiltRadius = Radius;

	const FVector CookieExtent = CookieInstances->GetStaticMesh()->GetBounds().BoxExtent * Settings.Cookie.Scale;
	const double CookieRadius = CookieExtent.X;

	// Enough cookies to cover the heap's footprint Coverage times over (they overlap like scales),
	// but never more than the heap actually holds.
	const int32 Surface = FMath::Min(Pile.Count, FMath::CeilToInt32(Settings.PileSurfaceCoverage * FMath::Square(Radius / CookieRadius)));
	const int32 Skirt = FMath::Min(Pile.Count - Surface, FMath::CeilToInt32(Surface * Settings.PileSkirtFraction));

	// Same seed every rebuild, so cookies only shift a little as the heap grows.
	FRandomStream Random(Pile.Seed);
	const double GoldenAngle = UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
	const double Phase = Random.FRandRange(0.0, UE_DOUBLE_TWO_PI);

	TArray<FTransform> Cookies;
	Cookies.Reserve(Surface + Skirt);
	auto AddCookie = [&](double Distance, double Angle, const FVector& Normal, double Lift)
	{
		const double Z = Height * FMath::Max(0.0, 1.0 - FMath::Square(Distance / Radius));
		const FVector Position = Pile.Ground + FVector(Distance * FMath::Cos(Angle), Distance * FMath::Sin(Angle), Z) + Normal * Lift;
		const FVector Tilted = (Normal + Random.VRand() * Settings.PileSurfaceJitter).GetSafeNormal();
		const FRotator Rotation = FRotationMatrix::MakeFromZX(Tilted, Random.VRand()).Rotator();
		Cookies.Emplace(Rotation, Position, Settings.Cookie.Scale);
	};

	// The heap: a sunflower spiral spreads cookies evenly over the footprint, so there are no bald patches.
	for (int32 I = 0; I < Surface; ++I)
	{
		const double Distance = Radius * FMath::Sqrt((I + 0.5) / Surface) * Random.FRandRange(0.94, 1.0);
		const double Angle = Phase + I * GoldenAngle + Random.FRandRange(-0.15, 0.15);
		// Lie along the slope: the paraboloid's normal at this distance from the centre.
		const double Slope = 2.0 * Height * Distance / FMath::Square(Radius);
		const FVector Normal = FVector(Slope * FMath::Cos(Angle), Slope * FMath::Sin(Angle), 1.0).GetSafeNormal();
		AddCookie(Distance, Angle, Normal, CookieExtent.Z + Random.FRand() * CookieExtent.Z * 2.0);
	}

	// The skirt: a few cookies that slid off and lie flat-ish around the base.
	for (int32 I = 0; I < Skirt; ++I)
	{
		const double Distance = Radius * Random.FRandRange(0.9, 1.3);
		AddCookie(Distance, Random.FRandRange(0.0, UE_DOUBLE_TWO_PI), FVector::UpVector, CookieExtent.Z);
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
