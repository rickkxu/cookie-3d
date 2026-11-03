#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "UObject/SoftObjectPtr.h"
#include "CookieTypes.generated.h"

// Things the player can buy. Add new house items here and in UCookieGameSettings::GetItem.
UENUM(BlueprintType)
enum class ECookieItem : uint8
{
	ToasterOven,
	Grandma,
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(ECookieItem, ECookieItem::Count);

// How a physical thing looks and flies when it is launched from the player.
USTRUCT(BlueprintType)
struct FCookieLaunchSpec
{
	GENERATED_BODY()

	// Placeholder shape. Swap for a stylized mesh in build step 8.
	UPROPERTY(EditAnywhere, Category = "Launch")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, Category = "Launch")
	FVector Scale = FVector(1.0);

	// Tints the mesh with the flat PlaceholderMaterial in Color. Turn off for real meshes that bring their own materials.
	UPROPERTY(EditAnywhere, Category = "Launch")
	bool bPlaceholderMaterial = true;

	// Placeholder tint, and the item's hotbar swatch.
	UPROPERTY(EditAnywhere, Category = "Launch")
	FLinearColor Color = FLinearColor::White;

	// Forward speed in cm/s along the look direction.
	UPROPERTY(EditAnywhere, Category = "Launch")
	float LaunchSpeed = 1000.f;

	// How far in front of the camera it appears, so it doesn't spawn inside the player.
	UPROPERTY(EditAnywhere, Category = "Launch")
	float SpawnDistance = 80.f;

	// Random spin on launch, in degrees/sec.
	UPROPERTY(EditAnywhere, Category = "Launch")
	float TumbleDegreesPerSecond = 180.f;

	// 0 = engine default (barely bounces), 1 = rubber ball.
	UPROPERTY(EditAnywhere, Category = "Launch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Bounciness = 0.f;

	// 0 = engine default. Higher makes things grip each other and heap up instead of sliding apart.
	UPROPERTY(EditAnywhere, Category = "Launch", meta = (ClampMin = "0.0"))
	float Friction = 0.f;

	// Slows spinning and rolling, so discs don't roll away on their edge.
	UPROPERTY(EditAnywhere, Category = "Launch", meta = (ClampMin = "0.0"))
	float AngularDamping = 0.f;

	// Continuous collision detection. Needed for thin, fast things like cookies.
	UPROPERTY(EditAnywhere, Category = "Launch")
	bool bUseCCD = false;
};

USTRUCT(BlueprintType)
struct FCookieItemSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Item")
	FText DisplayName;

	UPROPERTY(EditAnywhere, Category = "Item")
	double BaseCost = 10.0;

	// Production. Each one pops a physical cookie out of itself this many times a second,
	// and every popped cookie is what goes into the bank.
	UPROPERTY(EditAnywhere, Category = "Item", meta = (ClampMin = "0.0"))
	float CookiesPerSecond = 0.f;

	// Seconds between one helper's cookies, or 0 if it doesn't produce.
	float GetEmitIntervalSeconds() const { return CookiesPerSecond > 0.f ? 1.f / CookiesPerSecond : 0.f; }

	UPROPERTY(EditAnywhere, Category = "Item")
	FCookieLaunchSpec Launch;
};
