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
	// Piles are squashed spheres centred on the ground, so the top half shows as a mound.
	const TCHAR* SpherePath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	constexpr double SphereRadius = 50.0;
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

	// Pile core: a darker mound, so gaps between the surface cookies read as shadowed cookies further in.
	FCookieLaunchSpec PileSpec = Settings.Cookie;
	PileSpec.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(SpherePath));
	PileSpec.Color = Settings.Cookie.Color * Settings.PileCoreBrightness;
	PileInstances = MakeInstances(TEXT("CookiePiles"), PileSpec);
	PileInstances->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	// Cookies lying on the pile's surface. Decoration only: the core handles collision.
	PileSurfaceInstances = MakeInstances(TEXT("CookiePileSurface"), Settings.Cookie);
	PileSurfaceInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

UInstancedStaticMeshComponent* ACookieSettledManager::MakeInstances(const TCHAR* Name, const FCookieLaunchSpec& Spec)
{
	UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(this, Name);
	Instances->SetupAttachment(RootComponent);
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->SetStaticMesh(Spec.Mesh.LoadSynchronous());
	Instances->SetMaterial(0, UCookieGameSettings::Get().PlaceholderMaterial.LoadSynchronous());
	Instances->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Instances->RegisterComponent();

	if (UMaterialInstanceDynamic* Material = Instances->CreateDynamicMaterialInstance(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), Spec.Color);
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

	// Grow the nearest pile that still has room...
	FPile* Nearest = nullptr;
	double NearestDistSq = FMath::Square(Settings.PileMergeRadius);
	for (FPile& Pile : Piles)
	{
		const double DistSq = FVector::DistSquared(Pile.Ground, Location);
		if (Pile.Count < Settings.MaxCookiesPerPile && DistSq < NearestDistSq)
		{
			Nearest = &Pile;
			NearestDistSq = DistSq;
		}
	}
	if (Nearest)
	{
		++Nearest->Count;
		Nearest->bDirty = true;
		return;
	}

	// ...or start a new one on whatever is below, a little to the side so it doesn't bury its source.
	const FVector2D Offset = FMath::RandPointInCircle(Settings.PileMergeRadius * 0.5f);
	const FVector Start = Location + FVector(Offset.X, Offset.Y, 0.0);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CookiePileGround), false);
	Params.AddIgnoredComponent(PileInstances.Get());
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0, 0.0, 5000.0), ECC_Visibility, Params);

	FPile& Pile = Piles.AddDefaulted_GetRef();
	Pile.Ground = bHit ? Hit.ImpactPoint : Location;
	Pile.Count = 1;
	PileInstances->AddInstance(GetPileTransform(Pile), /*bWorldSpace=*/ true);
	CoverPileSurface(Pile);
}

double ACookieSettledManager::GetPileRadius(const FPile& Pile) const
{
	// The mound above ground is a half-spheroid half as tall as it is wide: volume = pi * r^3 / 3.
	const double Volume = Pile.Count * static_cast<double>(UCookieGameSettings::Get().PileCookieVolume);
	return FMath::Pow(3.0 * Volume / UE_DOUBLE_PI, 1.0 / 3.0);
}

FTransform ACookieSettledManager::GetPileTransform(const FPile& Pile) const
{
	const double Scale = GetPileRadius(Pile) / SphereRadius;
	return FTransform(FQuat::Identity, Pile.Ground, FVector(Scale, Scale, Scale * 0.5));
}

void ACookieSettledManager::CoverPileSurface(FPile& Pile)
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	const double Radius = GetPileRadius(Pile);
	const double Height = Radius * 0.5;

	// A half-spheroid this flat has about 1.38 * pi * r^2 of surface; a cookie covers pi * c^2.
	const FVector CookieExtent = CookieInstances->GetStaticMesh()->GetBounds().BoxExtent * Settings.Cookie.Scale;
	const double CookieRadius = CookieExtent.X;
	const int32 Wanted = FMath::CeilToInt32(Settings.PileSurfaceCoverage * 1.38 * FMath::Square(Radius / CookieRadius));
	if (Wanted <= Pile.SurfaceCookies)
	{
		return;
	}

	// Add only the new ones. Cookies placed while the pile was smaller end up buried inside the core.
	TArray<FTransform> NewCookies;
	NewCookies.Reserve(Wanted - Pile.SurfaceCookies);
	for (int32 Index = Pile.SurfaceCookies; Index < Wanted; ++Index)
	{
		// Random point on the upper half of the spheroid. Uniform height gives roughly even coverage.
		const double Yaw = FMath::FRandRange(0.0, UE_DOUBLE_TWO_PI);
		const double Up = FMath::FRand();
		const double Out = FMath::Sqrt(1.0 - Up * Up);
		const FVector Offset(Radius * Out * FMath::Cos(Yaw), Radius * Out * FMath::Sin(Yaw), Height * Up);

		// Lie flat against the slope (the spheroid's surface normal), tilted a little at random.
		const FVector Normal = FVector(Offset.X / (Radius * Radius), Offset.Y / (Radius * Radius), Offset.Z / (Height * Height)).GetSafeNormal();
		const FVector Tilted = (Normal + FMath::VRand() * Settings.PileSurfaceJitter).GetSafeNormal();
		const FRotator Rotation = FRotationMatrix::MakeFromZX(Tilted, FMath::VRand()).Rotator();

		// Rest on the surface rather than sinking halfway in, some a little proud of it for a lumpier outline.
		const double Lift = CookieExtent.Z + FMath::FRand() * CookieRadius * 0.4;
		NewCookies.Emplace(Rotation, Pile.Ground + Offset + Normal * Lift, Settings.Cookie.Scale);
	}
	PileSurfaceInstances->AddInstances(NewCookies, /*bShouldReturnIndices=*/ false, /*bWorldSpace=*/ true);
	Pile.SurfaceCookies = Wanted;
}

void ACookieSettledManager::RefreshDirtyPiles()
{
	for (int32 Index = 0; Index < Piles.Num(); ++Index)
	{
		FPile& Pile = Piles[Index];
		if (Pile.bDirty)
		{
			Pile.bDirty = false;
			PileInstances->UpdateInstanceTransform(Index, GetPileTransform(Pile), /*bWorldSpace=*/ true, /*bMarkRenderStateDirty=*/ true);
			CoverPileSurface(Pile);
		}
	}
}

void ACookieSettledManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EmitFromSettledItems(DeltaSeconds);

	// Resizing a pile rebuilds its collision, so batch growth a few times a second.
	PileRefreshTimer += DeltaSeconds;
	if (PileRefreshTimer >= UCookieGameSettings::Get().PileRefreshSeconds)
	{
		PileRefreshTimer = 0.f;
		RefreshDirtyPiles();
	}
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
