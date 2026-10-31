#include "CookieLaunchable.h"
#include "Components/StaticMeshComponent.h"
#include "CookieGameMode.h"
#include "CookieGrandma.h"
#include "CookieGameSettings.h"
#include "CookieSettledManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "TimerManager.h"

namespace
{
	constexpr float SettleCheckSeconds = 0.1f;
}

ACookieLaunchable::ACookieLaunchable()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	RootComponent = Mesh;
}

ACookieLaunchable* ACookieLaunchable::SpawnCookie(UWorld* World, const FVector& Location, const FRotator& Rotation, const FVector& Velocity)
{
	ACookieGameMode* GameMode = World ? World->GetAuthGameMode<ACookieGameMode>() : nullptr;
	if (!GameMode)
	{
		return nullptr;
	}

	ACookieLaunchable* Cookie = SpawnLaunched(World, UCookieGameSettings::Get().Cookie, Location, Rotation, Velocity);
	if (Cookie)
	{
		Cookie->bIsCookie = true;
		GameMode->AdjustFlyingCookieCount(1);
	}
	return Cookie;
}

AActor* ACookieLaunchable::SpawnItem(UWorld* World, ECookieItem Item, const FVector& Location, const FRotator& Rotation, const FVector& Velocity)
{
	if (Item == ECookieItem::Grandma)
	{
		return ACookieGrandma::Spawn(World, Location, Rotation, Velocity);
	}

	const FCookieItemSpec& Spec = UCookieGameSettings::Get().GetItem(Item);
	ACookieLaunchable* Thing = SpawnLaunched(World, Spec.Launch, Location, Rotation, Velocity);
	if (Thing)
	{
		Thing->Item = Item;
		// Random first delay so a burst of purchases doesn't emit in lockstep.
		const float FirstDelay = FMath::FRandRange(0.5f, 1.f) * Spec.EmitIntervalSeconds;
		World->GetTimerManager().SetTimer(Thing->EmitTimer, Thing, &ACookieLaunchable::EmitCookie,
			Spec.EmitIntervalSeconds, true, FirstDelay);
	}
	return Thing;
}

void ACookieLaunchable::PopCookie(UWorld* World, const FVector& Top)
{
	ACookieGameMode* GameMode = World ? World->GetAuthGameMode<ACookieGameMode>() : nullptr;
	ACookieSettledManager* Settled = GameMode ? GameMode->GetSettled() : nullptr;
	if (!Settled)
	{
		return;
	}

	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	const int32 Flying = GameMode->GetFlyingCookieCount();
	if (Flying >= Settings.MaxFlyingCookies || Flying + Settled->GetSettledCookieCount() >= Settings.MaxPhysicalCookies)
	{
		Settled->AddToPileNear(Top);
		return;
	}

	// Pop straight up, scattering a little sideways.
	const FVector2D Sideways = FMath::RandPointInCircle(Settings.EmitSpreadSpeed);
	const FVector Velocity(Sideways.X, Sideways.Y, Settings.EmitPopSpeed);
	const FRotator Rotation(0.f, FMath::FRandRange(0.f, 360.f), 0.f);
	SpawnCookie(World, Top + FVector::UpVector * 15.0, Rotation, Velocity);
}

ACookieLaunchable* ACookieLaunchable::SpawnLaunched(UWorld* World, const FCookieLaunchSpec& Spec, const FVector& Location, const FRotator& Rotation, const FVector& Velocity)
{
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACookieLaunchable* Thing = World->SpawnActor<ACookieLaunchable>(Location, Rotation, Params);
	if (Thing)
	{
		Thing->Launch(Spec, Velocity);
		World->GetTimerManager().SetTimer(Thing->SettleTimer, Thing, &ACookieLaunchable::CheckSettled, SettleCheckSeconds, true);
	}
	return Thing;
}

void ACookieLaunchable::Launch(const FCookieLaunchSpec& Spec, const FVector& Velocity)
{
	Mesh->SetStaticMesh(Spec.Mesh.LoadSynchronous());
	Mesh->SetWorldScale3D(Spec.Scale);

	if (Spec.bPlaceholderMaterial)
	{
		Mesh->SetMaterial(0, UCookieGameSettings::Get().PlaceholderMaterial.LoadSynchronous());
		if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Spec.Color);
		}
	}

	if (Spec.Bounciness > 0.f)
	{
		// Max combine so the item bounces even off floors that have no bounce of their own.
		UPhysicalMaterial* Bouncy = NewObject<UPhysicalMaterial>(this);
		Bouncy->Restitution = Spec.Bounciness;
		Bouncy->bOverrideRestitutionCombineMode = true;
		Bouncy->RestitutionCombineMode = EFrictionCombineMode::Max;
		Mesh->SetPhysMaterialOverride(Bouncy);
	}

	Mesh->SetUseCCD(Spec.bUseCCD);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetPhysicsLinearVelocity(Velocity);
	Mesh->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * Spec.TumbleDegreesPerSecond);
}

void ACookieLaunchable::EmitCookie()
{
	const FVector Top = GetActorLocation() + FVector::UpVector * Mesh->Bounds.BoxExtent.Z;
	PopCookie(GetWorld(), Top);
}

void ACookieLaunchable::CheckSettled()
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	FlyingSeconds += SettleCheckSeconds;

	// Things wedged against each other can jitter forever, so after a while "slow" counts as still.
	// (Not "freeze regardless": when the frame rate tanks, physics runs in slow motion and things would freeze mid-air.)
	const float Tolerance = FlyingSeconds >= Settings.MaxFlyingSeconds ? 10.f : 1.f;
	const bool bStill = Mesh->GetPhysicsLinearVelocity().Size() < Settings.SettleSpeed * Tolerance
		&& Mesh->GetPhysicsAngularVelocityInDegrees().Size() < Settings.SettleSpinDegrees * Tolerance;
	StillSeconds = bStill ? StillSeconds + SettleCheckSeconds : 0.f;

	if (StillSeconds < Settings.SettleSeconds)
	{
		return;
	}

	ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>();
	if (ACookieSettledManager* Settled = GameMode ? GameMode->GetSettled() : nullptr)
	{
		if (bIsCookie)
		{
			Settled->AddCookie(GetActorTransform());
		}
		else
		{
			Settled->AddItem(Item, GetActorTransform());
		}
	}
	Destroy();
}

void ACookieLaunchable::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsCookie)
	{
		if (ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>())
		{
			GameMode->AdjustFlyingCookieCount(-1);
		}
	}
	Super::EndPlay(EndPlayReason);
}
