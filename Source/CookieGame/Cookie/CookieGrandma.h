#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CookieGrandma.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;

/**
 * A purchased grandma. Flies as a ragdoll, flops where she lands, gets up (a death animation played
 * backwards, lined up with how she fell), then stands there baking: idling and popping out cookies.
 * Only the nearest few baking grandmas stay live and animated; the rest become frozen instances in
 * ACookieSettledManager (a static copy of the first baking grandma's pose) and thaw back when the player comes near.
 */
UCLASS()
class COOKIEGAME_API ACookieGrandma : public AActor
{
	GENERATED_BODY()

public:
	ACookieGrandma();

	static ACookieGrandma* Spawn(UWorld* World, const FVector& Location, const FRotator& Rotation, const FVector& Velocity);

	// A grandma already standing and baking, e.g. a frozen one the player walked up to.
	static ACookieGrandma* SpawnBaking(UWorld* World, const FTransform& Transform);

	bool IsBaking() const { return State == EState::Baking; }
	bool IsAnimating() const;

	// Lets only the nearest few baking grandmas animate. Called a couple of times a second.
	static void UpdateAnimationBudget(UWorld* World);

private:
	enum class EState : uint8
	{
		Flying,
		GettingUp,
		Baking,
	};

	void ApplyLook();
	void Launch(const FVector& Velocity);
	void StartEmitting();
	void EmitCookie();

	// Runs a few times a second while she flies. Starts her getting up once the ragdoll has been still long enough.
	void CheckSettled();
	void StartGettingUp();
	void StartBaking();
	void SetAnimated(bool bAnimate);

	// Which way her chest faces, from bone positions, so it works for ragdolls and animations alike.
	FVector GetChestDirection() const;
	FVector GetHeadDirection() const;

	// Poses the body at the last frame of Anim, right now, without waiting for the next animation tick.
	void PoseAtEndOf(UAnimSequence* Anim);

	UPROPERTY(VisibleAnywhere, Category = "Grandma")
	TObjectPtr<USkeletalMeshComponent> Body;

	EState State = EState::Flying;
	FTimerHandle SettleTimer;
	FTimerHandle EmitTimer;
	FTimerHandle GetUpTimer;
	float StillSeconds = 0.f;
	float FlyingSeconds = 0.f;
};
