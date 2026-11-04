#include "CookieLaunchable.h"
#include "Components/InstancedStaticMeshComponent.h"
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

	// Pop up and out to the side, always fast enough sideways to clear the helper's top (a cookie popped
	// straight up out of a grandma would land back on her head).
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	const double SidewaysAngle = FMath::FRandRange(0.0, UE_DOUBLE_TWO_PI);
	const double SidewaysSpeed = Settings.EmitSpreadSpeed * FMath::FRandRange(0.6, 1.0);
	const FVector2D Sideways(SidewaysSpeed * FMath::Cos(SidewaysAngle), SidewaysSpeed * FMath::Sin(SidewaysAngle));
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
		if (Spec.bFloats)
		{
			Thing->StartFloating(Spec, Velocity);
		}
		else
		{
			World->GetTimerManager().SetTimer(Thing->SettleTimer, Thing, &ACookieLaunchable::CheckSettled, SettleCheckSeconds, true);
		}
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

	if (Spec.bFloats)
	{
		// No physics at all: it glides (StartFloating) and passes through things on its way up.
		SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	Mesh->SetAngularDamping(Spec.AngularDamping);
	Mesh->SetUseCCD(Spec.bUseCCD);

	FVector Spin = FMath::VRand() * Spec.TumbleDegreesPerSecond;
	if (Spec.bStayUpright)
	{
		// Stand it up and lock pitch and roll, so it can only turn about its trunk, in the air and on the ground.
		SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
		FBodyInstance* Body = Mesh->GetBodyInstance();
		Body->bLockXRotation = true;
		Body->bLockYRotation = true;
		// Buildings thrown to face the player don't turn at all, or a skid on landing swings them round.
		Body->bLockZRotation = Spec.bFaceThrower;
		Body->SetDOFLock(EDOFMode::SixDOF);
		Spin = FVector(0.0, 0.0, FMath::FRandRange(-1.f, 1.f) * Spec.TumbleDegreesPerSecond);
	}

	Mesh->SetSimulatePhysics(true);
	Mesh->SetPhysicsLinearVelocity(Velocity);
	Mesh->SetPhysicsAngularVelocityInDegrees(Spin);
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
	ACookieSettledManager* Settled = GameMode ? GameMode->GetSettled() : nullptr;
	if (bIsCookie && GrandmaShoves < MaxGrandmaShoves && ShoveOffGrandma(Settled))
	{
		++GrandmaShoves;
		StillSeconds = 0.f;
		return;
	}

	if (Settled)
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

void ACookieLaunchable::StartFloating(const FCookieLaunchSpec& Spec, const FVector& Velocity)
{
	// Where it's headed: ahead of the player (the way it was thrown), a little scattered so several don't stack,
	// FloatHeight above whatever is below (ground, roof or mountain).
	FVector Direction = Velocity.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector().GetSafeNormal2D();
	}
	const FVector2D Scatter = FMath::RandPointInCircle(900.f);
	FVector Target = GetActorLocation() + Direction * Spec.FloatDistance + FVector(Scatter.X, Scatter.Y, 0.0);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CookieFloatGround), false, this);
	// Not on top of other clouds (or anything else settled): over the world itself.
	if (const ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>())
	{
		Params.AddIgnoredActor(GameMode->GetSettled());
	}
	FHitResult Hit;
	const FVector Above(Target.X, Target.Y, Target.Z + 100000.0);
	double Ground = GetActorLocation().Z - 200.0;
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Above, Above - FVector(0.0, 0.0, 200000.0), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		Ground = Hit.ImpactPoint.Z;
	}
	Target.Z = Ground + Spec.FloatHeight + FMath::FRandRange(-300.f, 300.f);

	FloatFrom = GetActorLocation();
	FloatTo = Target;
	FloatElapsed = 0.f;
	FloatDuration = Spec.FloatSeconds;
	GetWorldTimerManager().SetTimer(FloatTimer, this, &ACookieLaunchable::TickFloat, 1.f / 60.f, true);
}

void ACookieLaunchable::TickFloat()
{
	FloatElapsed += 1.f / 60.f;
	const float T = FMath::Clamp(FloatElapsed / FloatDuration, 0.f, 1.f);
	// Sideways it eases out; upwards it overshoots a touch and bobs back, like something light catching the air.
	const float Across = 1.f - FMath::Pow(1.f - T, 3.f);
	const float C1 = 1.4f;
	const float Up = 1.f + (C1 + 1.f) * FMath::Pow(T - 1.f, 3.f) + C1 * FMath::Pow(T - 1.f, 2.f);
	FVector Location = FMath::Lerp(FloatFrom, FloatTo, Across);
	Location.Z = FMath::Lerp(FloatFrom.Z, FloatTo.Z, Up);
	SetActorLocation(Location);

	if (T < 1.f)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(FloatTimer);
	ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>();
	if (ACookieSettledManager* Settled = GameMode ? GameMode->GetSettled() : nullptr)
	{
		Settled->AddItem(Item, GetActorTransform());
	}
	Destroy();
}

bool ACookieLaunchable::ShoveOffGrandma(const ACookieSettledManager* Settled)
{
	// What is this cookie resting on? Grandmas are ragdolls (physics bodies) while baking and instances once frozen.
	const FVector Location = GetActorLocation();
	FCollisionObjectQueryParams Types;
	Types.AddObjectTypesToQuery(ECC_WorldStatic);
	Types.AddObjectTypesToQuery(ECC_WorldDynamic);
	Types.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CookieOnGrandma), false, this);
	FHitResult Hit;
	if (!GetWorld()->SweepSingleByObjectType(Hit, Location, Location - FVector(0.0, 0.0, 30.0), FQuat::Identity, Types, FCollisionShape::MakeSphere(4.f), Params))
	{
		return false;
	}

	FVector Axis;
	if (const ACookieGrandma* Grandma = Cast<ACookieGrandma>(Hit.GetActor()))
	{
		Axis = Grandma->GetActorLocation();
	}
	else if (Settled && Hit.GetComponent() && Hit.GetComponent() == Settled->GetItemInstances(ECookieItem::Grandma))
	{
		FTransform Instance;
		Settled->GetItemInstances(ECookieItem::Grandma)->GetInstanceTransform(Hit.Item, Instance, /*bWorldSpace=*/ true);
		Axis = Instance.GetLocation();
	}
	else
	{
		return false;
	}

	// Slide it off her, away from her middle.
	FVector Away = Location - Axis;
	Away.Z = 0.0;
	if (!Away.Normalize())
	{
		Away = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Vector();
	}
	Mesh->SetPhysicsLinearVelocity(Away * 220.0 + FVector(0.0, 0.0, 120.0));
	return true;
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
