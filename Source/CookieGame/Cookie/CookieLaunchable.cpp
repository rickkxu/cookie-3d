#include "CookieLaunchable.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

ACookieLaunchable::ACookieLaunchable()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	RootComponent = Mesh;
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
