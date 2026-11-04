#include "CookieGrandma.h"
#include "Algo/RandomShuffle.h"
#include "Algo/Sort.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "CookieGameMode.h"
#include "CookieGameSettings.h"
#include "CookieLaunchable.h"
#include "CookieSettledManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "StaticMeshAttributes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "TimerManager.h"

namespace
{
	constexpr float GrandmaSettleCheckSeconds = 0.1f;

	// Bones on the grandma's skeleton. Named in the settings, so any humanoid rig works.
	FName PelvisBone() { return UCookieGameSettings::Get().GrandmaPelvisBone; }
	FName HeadBone() { return UCookieGameSettings::Get().GrandmaHeadBone; }
	FName LeftShoulderBone() { return UCookieGameSettings::Get().GrandmaLeftUpperArmBone; }
	FName RightShoulderBone() { return UCookieGameSettings::Get().GrandmaRightUpperArmBone; }

	/**
	 * Copies the skeletal mesh's current pose (at the given detail level) into a new static mesh,
	 * so hundreds of frozen grandmas can be drawn as cheap instances.
	 * Returns null if the mesh's vertex data isn't readable on the CPU.
	 */
	UStaticMesh* BakeStaticPose(USkeletalMeshComponent* Body, int32 LODIndex)
	{
		FSkeletalMeshRenderData* RenderData = Body->GetSkeletalMeshRenderData();
		if (!RenderData || RenderData->LODRenderData.IsEmpty())
		{
			return nullptr;
		}
		LODIndex = FMath::Clamp(LODIndex, 0, RenderData->LODRenderData.Num() - 1);
		const FSkeletalMeshLODRenderData& LOD = RenderData->LODRenderData[LODIndex];
		if (!LOD.StaticVertexBuffers.PositionVertexBuffer.GetVertexData() || !LOD.MultiSizeIndexContainer.IsIndexBufferValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Grandma pose bake: mesh LOD %d isn't CPU-readable; frozen grandmas stay skeletal."), LODIndex);
			return nullptr;
		}

		// Vertex positions in the current pose, in component space.
		TArray<FMatrix44f> RefToLocals;
		Body->CacheRefToLocalMatrices(RefToLocals);
		TArray<FVector3f> Positions;
		USkinnedMeshComponent::ComputeSkinnedPositions(Body, Positions, RefToLocals, LOD, LOD.SkinWeightVertexBuffer);

		const FRawStaticIndexBuffer16or32Interface* Indices = LOD.MultiSizeIndexContainer.GetIndexBuffer();
		const int32 NumTriangles = Indices->Num() / 3;

		// Smooth normals from the posed triangles.
		TArray<FVector3f> Normals;
		Normals.SetNumZeroed(Positions.Num());
		for (int32 Tri = 0; Tri < NumTriangles; ++Tri)
		{
			const uint32 A = Indices->Get(Tri * 3), B = Indices->Get(Tri * 3 + 1), C = Indices->Get(Tri * 3 + 2);
			const FVector3f Face = FVector3f::CrossProduct(Positions[C] - Positions[A], Positions[B] - Positions[A]);
			Normals[A] += Face;
			Normals[B] += Face;
			Normals[C] += Face;
		}

		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		Description.ReserveNewVertices(Positions.Num());
		Description.ReserveNewVertexInstances(Positions.Num());
		Description.ReserveNewTriangles(NumTriangles);

		TArray<FVertexInstanceID> Instances;
		Instances.Reserve(Positions.Num());
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			const FVertexID Vertex = Description.CreateVertex();
			Attributes.GetVertexPositions()[Vertex] = Positions[Index];
			const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
			const FVector3f Normal = Normals[Index].GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
			Attributes.GetVertexInstanceNormals()[Instance] = Normal;
			Attributes.GetVertexInstanceTangents()[Instance] = FVector3f::CrossProduct(Normal, FVector3f::ForwardVector).GetSafeNormal(UE_SMALL_NUMBER, FVector3f::RightVector);
			Attributes.GetVertexInstanceBinormalSigns()[Instance] = 1.f;
			Attributes.GetVertexInstanceUVs().Set(Instance, 0, LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index, 0));
			Instances.Add(Instance);
		}

		// One material for the whole body (the stand-in and the Fab grandma both have a single material slot).
		const FPolygonGroupID Group = Description.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Body");
		for (int32 Tri = 0; Tri < NumTriangles; ++Tri)
		{
			Description.CreateTriangle(Group, { Instances[Indices->Get(Tri * 3)], Instances[Indices->Get(Tri * 3 + 1)], Instances[Indices->Get(Tri * 3 + 2)] });
		}

		UStaticMesh* Mesh = NewObject<UStaticMesh>(Body->GetWorld(), TEXT("FrozenGrandmaPose"));
		const UCookieGameSettings& Settings = UCookieGameSettings::Get();
		UMaterialInterface* Material = Settings.Grandma.Launch.bPlaceholderMaterial
			? Settings.PlaceholderMaterial.LoadSynchronous()
			: Body->GetSkinnedAsset()->GetMaterials()[0].MaterialInterface.Get();
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, TEXT("Body")));
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bBuildSimpleCollision = false;
		Params.bFastBuild = true;
		Mesh->BuildFromMeshDescriptions({ &Description }, Params);

		// Collide as a capsule fitted to her body. A box around the pose is far bigger than she is (cookies
		// would rest on its invisible corners around her head); her exact triangles are too slow to collide with.
		FBox Box(ForceInit);
		for (const FVector3f& Position : Positions)
		{
			Box += FVector(Position);
		}
		FKSphylElem Capsule;
		Capsule.Center = Box.GetCenter();
		Capsule.Radius = 0.2f * static_cast<float>(Box.GetSize().X + Box.GetSize().Y);
		Capsule.Length = FMath::Max(0.f, static_cast<float>(Box.GetSize().Z) - 2.f * Capsule.Radius);
		Mesh->CreateBodySetup();
		UBodySetup* Collision = Mesh->GetBodySetup();
		Collision->AggGeom.SphylElems.Add(Capsule);
		Collision->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		Collision->InvalidatePhysicsData();
		Collision->CreatePhysicsMeshes();
		return Mesh;
	}
}

ACookieGrandma::ACookieGrandma()
{
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetCollisionProfileName(TEXT("Ragdoll"));
	// Off-screen grandmas don't evaluate animation.
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	RootComponent = Body;
}

ACookieGrandma* ACookieGrandma::Spawn(UWorld* World, const FVector& Location, const FRotator& Rotation, const FVector& Velocity)
{
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACookieGrandma* Grandma = World->SpawnActor<ACookieGrandma>(Location, Rotation, Params);
	if (Grandma)
	{
		Grandma->Launch(Velocity);
	}
	return Grandma;
}

ACookieGrandma* ACookieGrandma::SpawnBaking(UWorld* World, const FTransform& Transform)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACookieGrandma* Grandma = World ? World->SpawnActor<ACookieGrandma>(Transform.GetLocation(), Transform.Rotator(), Params) : nullptr;
	if (Grandma)
	{
		Grandma->ApplyLook();
		Grandma->StartEmitting();
		// Already standing where she was frozen: no turning round.
		Grandma->State = EState::Baking;
		Grandma->StartBaking();
	}
	return Grandma;
}

bool ACookieGrandma::IsAnimating() const
{
	return State != EState::Baking || !Body->bPauseAnims;
}

void ACookieGrandma::ApplyLook()
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();

	Body->SetSkeletalMeshAsset(Settings.GrandmaMesh.LoadSynchronous());
	if (UPhysicsAsset* Physics = Settings.GrandmaPhysicsAsset.LoadSynchronous())
	{
		Body->SetPhysicsAsset(Physics, /*bForceReInit=*/ true);
	}
	Body->SetWorldScale3D(FVector(Settings.GrandmaScale));

	if (!Settings.Grandma.Launch.bPlaceholderMaterial)
	{
		return;
	}

	// Placeholder look: every material slot gets the flat color material in grandma lavender.
	UMaterialInterface* Placeholder = Settings.PlaceholderMaterial.LoadSynchronous();
	for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
	{
		Body->SetMaterial(Slot, Placeholder);
		if (UMaterialInstanceDynamic* Material = Body->CreateDynamicMaterialInstance(Slot))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Settings.Grandma.Launch.Color);
		}
	}
}

void ACookieGrandma::StartEmitting()
{
	// Random first delay so a burst of purchases doesn't emit in lockstep.
	const float Production = UCookieGameSettings::Get().GetProduction(ECookieItem::Grandma);
	const float Interval = Production > 0.f ? 1.f / Production : 0.f;
	if (Interval <= 0.f)
	{
		return;
	}
	const float FirstDelay = FMath::FRandRange(0.5f, 1.f) * Interval;
	GetWorldTimerManager().SetTimer(EmitTimer, this, &ACookieGrandma::EmitCookie, Interval, true, FirstDelay);
}

void ACookieGrandma::Launch(const FVector& Velocity)
{
	ApplyLook();

	// Full ragdoll from the moment she leaves the player.
	Body->SetSimulatePhysics(true);
	Body->SetAllPhysicsLinearVelocity(Velocity);
	Body->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * UCookieGameSettings::Get().Grandma.Launch.TumbleDegreesPerSecond, false, PelvisBone());

	GetWorldTimerManager().SetTimer(SettleTimer, this, &ACookieGrandma::CheckSettled, GrandmaSettleCheckSeconds, true);
	StartEmitting();
}

void ACookieGrandma::EmitCookie()
{
	const FBoxSphereBounds& Bounds = Body->Bounds;
	ACookieLaunchable::PopCookie(GetWorld(), Bounds.Origin + FVector::UpVector * Bounds.BoxExtent.Z);
}

void ACookieGrandma::CheckSettled()
{
	// Same rule as other thrown things (see ACookieLaunchable::CheckSettled), judged by her pelvis.
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	FlyingSeconds += GrandmaSettleCheckSeconds;
	const float Tolerance = FlyingSeconds >= Settings.MaxFlyingSeconds ? 10.f : 1.f;
	const bool bStill = Body->GetPhysicsLinearVelocity(PelvisBone()).Size() < Settings.SettleSpeed * Tolerance;
	StillSeconds = bStill ? StillSeconds + GrandmaSettleCheckSeconds : 0.f;
	if (StillSeconds >= Settings.SettleSeconds)
	{
		GetWorldTimerManager().ClearTimer(SettleTimer);
		StartGettingUp();
	}
}

void ACookieGrandma::UpdateAnimationBudget(UWorld* World)
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	const FVector Viewer = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	const ACookieGameMode* GameMode = World ? World->GetAuthGameMode<ACookieGameMode>() : nullptr;
	ACookieSettledManager* Settled = GameMode ? GameMode->GetSettled() : nullptr;
	const bool bCanFreeze = Settled && Settled->HasGrandmaPose();

	// Every baking grandma, whether she's a live actor or a frozen instance, nearest first.
	struct FCandidate
	{
		double DistSq;
		ACookieGrandma* Actor;
		int32 Instance;
	};
	TArray<FCandidate> Candidates;
	for (TActorIterator<ACookieGrandma> It(World); It; ++It)
	{
		if (It->IsBaking())
		{
			Candidates.Add({ FVector::DistSquared(Viewer, It->GetActorLocation()), *It, INDEX_NONE });
		}
	}
	if (bCanFreeze)
	{
		const UInstancedStaticMeshComponent* Frozen = Settled->GetItemInstances(ECookieItem::Grandma);
		for (int32 Index = 0; Index < Frozen->GetInstanceCount(); ++Index)
		{
			FTransform Transform;
			Frozen->GetInstanceTransform(Index, Transform, /*bWorldSpace=*/ true);
			Candidates.Add({ FVector::DistSquared(Viewer, Transform.GetLocation()), nullptr, Index });
		}
	}
	Algo::SortBy(Candidates, &FCandidate::DistSq);

	// The nearest few are live, animated actors. Everyone else is a frozen instance (or, if her pose
	// couldn't be baked, a paused actor). Swapping is limited per update so walking around stays smooth.
	const double MaxDistSq = FMath::Square(Settings.GrandmaAnimateRadius);
	TArray<int32> ToThaw;
	for (int32 Rank = 0; Rank < Candidates.Num(); ++Rank)
	{
		const FCandidate& Candidate = Candidates[Rank];
		const bool bAnimate = Pawn && Rank < Settings.MaxAnimatedGrandmas && Candidate.DistSq < MaxDistSq;
		if (Candidate.Actor)
		{
			if (bAnimate || !bCanFreeze)
			{
				Candidate.Actor->SetAnimated(bAnimate);
			}
			else
			{
				Settled->AddItem(ECookieItem::Grandma, Candidate.Actor->GetActorTransform());
				Candidate.Actor->Destroy();
			}
		}
		else if (bAnimate && ToThaw.Num() < Settings.MaxGrandmaThawsPerUpdate)
		{
			ToThaw.Add(Candidate.Instance);
		}
	}

	// Highest index first, so removing one doesn't shift the others.
	ToThaw.Sort(TGreater<int32>());
	for (int32 Instance : ToThaw)
	{
		SpawnBaking(World, Settled->RemoveItemInstance(ECookieItem::Grandma, Instance));
	}
}

void ACookieGrandma::SetAnimated(bool bAnimate)
{
	if (bAnimate == !Body->bPauseAnims)
	{
		return;
	}

	// A frozen grandma costs no CPU (no animation, no bone updates) and little GPU (her lowest-detail mesh).
	Body->bPauseAnims = !bAnimate;
	Body->bNoSkeletonUpdate = !bAnimate;
	Body->SetComponentTickEnabled(bAnimate);
	Body->SetForcedLOD(bAnimate ? 0 : Body->GetNumLODs());
}

FVector ACookieGrandma::GetChestDirection() const
{
	// Shoulder line crossed with the spine points out of her chest, whatever her bones' axes are.
	const FVector Spine = Body->GetBoneLocation(HeadBone()) - Body->GetBoneLocation(PelvisBone());
	const FVector Shoulders = Body->GetBoneLocation(RightShoulderBone()) - Body->GetBoneLocation(LeftShoulderBone());
	return FVector::CrossProduct(Shoulders, Spine).GetSafeNormal();
}

FVector ACookieGrandma::GetHeadDirection() const
{
	return (Body->GetBoneLocation(HeadBone()) - Body->GetBoneLocation(PelvisBone())).GetSafeNormal2D();
}

void ACookieGrandma::PoseAtEndOf(UAnimSequence* Anim)
{
	Body->PlayAnimation(Anim, /*bLooping=*/ false);
	Body->SetPosition(Anim->GetPlayLength(), /*bFireNotifies=*/ false);
	Body->TickAnimation(0.f, false);
	Body->RefreshBoneTransforms();
}

void ACookieGrandma::StartGettingUp()
{
	State = EState::GettingUp;
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();

	// Remember how she landed before the ragdoll is switched off.
	const FVector LyingPelvis = Body->GetBoneLocation(PelvisBone());
	const FVector LyingHeadDirection = GetHeadDirection();
	const bool bFaceUp = GetChestDirection().Z > 0.0;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrandmaGround), false, this);
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, LyingPelvis, LyingPelvis - FVector(0.0, 0.0, 300.0), ECC_Visibility, Params);
	const double GroundZ = bHit ? Hit.ImpactPoint.Z : LyingPelvis.Z - 15.0;

	Body->SetSimulatePhysics(false);
	SetActorLocationAndRotation(FVector(LyingPelvis.X, LyingPelvis.Y, GroundZ), FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);

	// Pick a fall whose final pose lies the same way up as she does.
	TArray<TSoftObjectPtr<UAnimSequence>> Candidates = Settings.GrandmaGetUpAnims;
	Algo::RandomShuffle(Candidates);
	UAnimSequence* GetUp = nullptr;
	for (const TSoftObjectPtr<UAnimSequence>& Candidate : Candidates)
	{
		UAnimSequence* Anim = Candidate.LoadSynchronous();
		if (!Anim)
		{
			continue;
		}
		PoseAtEndOf(Anim);
		if ((GetChestDirection().Z > 0.0) == bFaceUp)
		{
			GetUp = Anim;
			break;
		}
	}
	if (!GetUp)
	{
		StartBaking();
		return;
	}

	// Turn and shift the animation's lying pose onto the ragdoll's: head the same way, pelvis in the same spot.
	const FVector AnimHeadDirection = GetHeadDirection();
	const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(LyingHeadDirection.Y, LyingHeadDirection.X)
		- FMath::Atan2(AnimHeadDirection.Y, AnimHeadDirection.X));
	const FVector PelvisOffset = (Body->GetBoneLocation(PelvisBone()) - GetActorLocation()) * FVector(1.0, 1.0, 0.0);
	const FVector RotatedOffset = PelvisOffset.RotateAngleAxis(Yaw, FVector::UpVector);
	SetActorLocationAndRotation(FVector(LyingPelvis.X - RotatedOffset.X, LyingPelvis.Y - RotatedOffset.Y, GroundZ),
		FRotator(0.0, Yaw, 0.0), false, nullptr, ETeleportType::TeleportPhysics);

	// Play the fall backwards: she gets up.
	Body->SetPlayRate(-Settings.GrandmaGetUpPlayRate);
	Body->Play(/*bLooping=*/ false);
	const float Duration = GetUp->GetPlayLength() / Settings.GrandmaGetUpPlayRate;
	GetWorldTimerManager().SetTimer(GetUpTimer, this, &ACookieGrandma::StartBaking, Duration, false);
}

void ACookieGrandma::TurnToFacePlayer()
{
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	const APawn* Player = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Player)
	{
		return;
	}
	TurnPivot = Body->GetBoneLocation(PelvisBone());
	FVector ToPlayer = Player->GetActorLocation() - TurnPivot;
	ToPlayer.Z = 0.0;
	if (!ToPlayer.Normalize())
	{
		return;
	}

	// Her mesh faces +Y in its own space: at yaw Y she faces (-sin Y, cos Y).
	const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(-ToPlayer.X, ToPlayer.Y));
	TurnStartYaw = GetActorRotation().Yaw;
	TurnDeltaYaw = FMath::FindDeltaAngleDegrees(TurnStartYaw, TargetYaw);
	TurnStartLocation = GetActorLocation();
	TurnSeconds = 0.f;
	GetWorldTimerManager().SetTimer(TurnTimer, this, &ACookieGrandma::TickTurn, 1.f / 30.f, true);
}

void ACookieGrandma::TickTurn()
{
	constexpr float TurnDuration = 0.45f;
	TurnSeconds += 1.f / 30.f;
	const float Alpha = FMath::SmoothStep(0.f, 1.f, FMath::Min(TurnSeconds / TurnDuration, 1.f));
	const float Delta = TurnDeltaYaw * Alpha;
	// Pivot about her pelvis so she turns on the spot.
	const FVector Location = TurnPivot + (TurnStartLocation - TurnPivot).RotateAngleAxis(Delta, FVector::UpVector);
	SetActorLocationAndRotation(Location, FRotator(0.f, TurnStartYaw + Delta, 0.f));
	if (TurnSeconds >= TurnDuration)
	{
		GetWorldTimerManager().ClearTimer(TurnTimer);
	}
}

void ACookieGrandma::StartBaking()
{
	// Thrown grandmas (not ones thawed out already baking) turn to face the player as they settle in.
	if (State != EState::Baking)
	{
		TurnToFacePlayer();
	}
	State = EState::Baking;
	if (UAnimSequence* Idle = UCookieGameSettings::Get().GrandmaIdleAnim.LoadSynchronous())
	{
		Body->PlayAnimation(Idle, /*bLooping=*/ true);
		Body->SetPlayRate(1.f);
		Body->SetPosition(FMath::FRandRange(0.f, Idle->GetPlayLength()), false);
	}

	// The first grandma to start baking is copied into a static mesh, the pose every frozen grandma uses.
	const ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>();
	ACookieSettledManager* Settled = GameMode ? GameMode->GetSettled() : nullptr;
	if (Settled && !Settled->HasGrandmaPose())
	{
		Body->TickAnimation(0.f, false);
		Body->RefreshBoneTransforms();
		Settled->SetGrandmaPose(BakeStaticPose(Body, UCookieGameSettings::Get().GrandmaFrozenLOD));
	}
	// From here UpdateAnimationBudget decides whether she animates.
}
