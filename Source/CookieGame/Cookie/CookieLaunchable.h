#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CookieTypes.h"
#include "CookieLaunchable.generated.h"

class UStaticMeshComponent;

/**
 * A physical thing thrown from the player: a cookie, a toaster oven, a grandma.
 * For now it just simulates physics forever. Freeze-when-settled + instancing is build step 5.
 */
UCLASS()
class COOKIEGAME_API ACookieLaunchable : public AActor
{
	GENERATED_BODY()

public:
	ACookieLaunchable();

	// Applies the look from Spec and throws it with Velocity.
	void Launch(const FCookieLaunchSpec& Spec, const FVector& Velocity);

private:
	UPROPERTY(VisibleAnywhere, Category = "Cookie")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
