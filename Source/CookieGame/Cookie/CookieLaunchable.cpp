#include "CookieLaunchable.h"
#include "Components/StaticMeshComponent.h"
#include "CookieGameMode.h"
#include "CookieGameSettings.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "TimerManager.h"

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
	if (!GameMode || GameMode->GetPhysicalCookieCount() >= UCookieGameSettings::Get().MaxPhysicalCookies)
	{
		return nullptr;
	}

	ACookieLaunchable* Cookie = SpawnLaunched(World, UCookieGameSettings::Get().Cookie, Location, Rotation, Velocity);
	if (Cookie)
	{
		Cookie->bIsCookie = true;
		GameMode->AdjustPhysicalCookieCount(1);
	}
	return Cookie;
}

ACookieLaunchable* ACookieLaunchable::SpawnItem(UWorld* World, ECookieItem Item, const FVector& Location, const FRotator& Rotation, const FVector& Velocity)
{
	const FCookieItemSpec& Spec = UCookieGameSettings::Get().GetItem(Item);
	ACookieLaunchable* Thing = SpawnLaunched(World, Spec.Launch, Location, Rotation, Velocity);
	if (Thing)
	{
		// Random first delay so a burst of purchases doesn't emit in lockstep.
		const float FirstDelay = FMath::FRandRange(0.5f, 1.f) * Spec.EmitIntervalSeconds;
		World->GetTimerManager().SetTimer(Thing->EmitTimer, Thing, &ACookieLaunchable::EmitCookie,
			Spec.EmitIntervalSeconds, true, FirstDelay);
	}
	return Thing;
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
	}
	return Thing;
}

void ACookieLaunchable::Launch(const FCookieLaunchSpec& Spec, const FVector& Velocity)
{
	Mesh->SetStaticMesh(Spec.Mesh.LoadSynchronous());
	Mesh->SetWorldScale3D(Spec.Scale);

	if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), Spec.Color);
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
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();

	// Pop out of the top of whatever this is, scattering a little sideways.
	const FVector Top = GetActorLocation() + FVector::UpVector * (Mesh->Bounds.BoxExtent.Z + 15.0);
	const FVector2D Sideways = FMath::RandPointInCircle(Settings.EmitSpreadSpeed);
	const FVector Velocity(Sideways.X, Sideways.Y, Settings.EmitPopSpeed);
	const FRotator Rotation(0.f, FMath::FRandRange(0.f, 360.f), 0.f);

	SpawnCookie(GetWorld(), Top, Rotation, Velocity);
}

void ACookieLaunchable::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsCookie)
	{
		if (ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>())
		{
			GameMode->AdjustPhysicalCookieCount(-1);
		}
	}
	Super::EndPlay(EndPlayReason);
}
