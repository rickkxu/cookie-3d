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
	constexpr float SpoutTickSeconds = 0.05f;
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
	if (!Thing)
	{
		return nullptr;
	}
	Thing->Item = Item;

	if (!Spec.Spouts.IsEmpty())
	{
		// Big producers spew from their spouts even while they fly: a factory lobbed across the street erupts on the way.
		Thing->SpoutCarry.SetNumZeroed(Spec.Spouts.Num());
		World->GetTimerManager().SetTimer(Thing->EmitTimer, Thing, &ACookieLaunchable::EmitFromSpouts, SpoutTickSeconds, true);
		return Thing;
	}

	const float Production = UCookieGameSettings::Get().GetProduction(Item);
	const float Interval = Production > 0.f ? 1.f / Production : 0.f;
	if (Interval > 0.f)
	{
		// Random first delay so a burst of purchases doesn't emit in lockstep.
		const float FirstDelay = FMath::FRandRange(0.5f, 1.f) * Interval;
		World->GetTimerManager().SetTimer(Thing->EmitTimer, Thing, &ACookieLaunchable::EmitCookie, Interval, true, FirstDelay);
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

	// The bank only ever grows by cookies you can see: this one.
	GameMode->AddCookies(1.0);

	// Pop straight up, scattering a little sideways.
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	const FVector2D Sideways = FMath::RandPointInCircle(Settings.EmitSpreadSpeed);
	const FVector Velocity(Sideways.X, Sideways.Y, Settings.EmitPopSpeed);
	const FVector Start = Top + FVector::UpVector * 15.0;

	// A real physics cookie while there's room in the air; past that, one that flies the same arc
	// without simulating, so a big bakery still visibly pops every cookie without flooding the physics engine.
	if (GameMode->GetFlyingCookieCount() < Settings.MaxFlyingCookies)
	{
		SpawnCookie(World, Start, FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Velocity);
	}
	else
	{
		Settled->LaunchGhostCookie(Start, Velocity);
	}
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

	if (Spec.Bounciness > 0.f || Spec.Friction > 0.f)
	{
		// Max combine so the item bounces (or grips) even against floors that don't.
		UPhysicalMaterial* Surface = NewObject<UPhysicalMaterial>(this);
		if (Spec.Bounciness > 0.f)
		{
			Surface->Restitution = Spec.Bounciness;
			Surface->bOverrideRestitutionCombineMode = true;
			Surface->RestitutionCombineMode = EFrictionCombineMode::Max;
		}
		if (Spec.Friction > 0.f)
		{
			Surface->Friction = Spec.Friction;
			Surface->StaticFriction = Spec.Friction;
			Surface->bOverrideFrictionCombineMode = true;
			Surface->FrictionCombineMode = EFrictionCombineMode::Max;
		}
		Mesh->SetPhysMaterialOverride(Surface);
	}

	Mesh->SetAngularDamping(Spec.AngularDamping);
	Mesh->SetUseCCD(Spec.bUseCCD);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetPhysicsLinearVelocity(Velocity);
	Mesh->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * Spec.TumbleDegreesPerSecond);
}

void ACookieLaunchable::EmitFromSpouts()
{
	ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>();
	if (ACookieSettledManager* Settled = GameMode ? GameMode->GetSettled() : nullptr)
	{
		Settled->EmitSpouts(Item, GetActorTransform(), SpoutTickSeconds, SpoutCarry.GetData());
	}
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
