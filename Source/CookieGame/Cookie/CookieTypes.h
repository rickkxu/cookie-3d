#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "UObject/SoftObjectPtr.h"
#include "CookieTypes.generated.h"

// Things the player can buy. Add new items here and in UCookieGameSettings::GetItem.
UENUM(BlueprintType)
enum class ECookieItem : uint8
{
	ToasterOven,
	Grandma,
	CookieTree,
	Bakery,
	Factory,
	CookieCloud,
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

	// Never tips over: flies and lands standing up, only turning about its vertical axis (trees).
	UPROPERTY(EditAnywhere, Category = "Launch")
	bool bStayUpright = false;

	// Launched with its front toward the player who threw it (buildings). Use with bStayUpright and no tumble so
	// it still faces them when it lands.
	UPROPERTY(EditAnywhere, Category = "Launch")
	bool bFaceThrower = false;

	// Which way the model's front points in its own space, as a yaw in degrees (-90 = -Y, the kit's front).
	UPROPERTY(EditAnywhere, Category = "Launch", meta = (EditCondition = "bFaceThrower"))
	float FrontYaw = -90.f;

	// Flies up into the sky and hovers there instead of falling (clouds): no physics, it glides to a spot
	// FloatDistance ahead of the player, FloatHeight above whatever is below, over FloatSeconds.
	UPROPERTY(EditAnywhere, Category = "Launch")
	bool bFloats = false;

	UPROPERTY(EditAnywhere, Category = "Launch", meta = (EditCondition = "bFloats"))
	float FloatHeight = 3000.f;

	UPROPERTY(EditAnywhere, Category = "Launch", meta = (EditCondition = "bFloats"))
	float FloatDistance = 3500.f;

	UPROPERTY(EditAnywhere, Category = "Launch", meta = (EditCondition = "bFloats", ClampMin = "0.1"))
	float FloatSeconds = 2.5f;
};

// Where a big producer spews its cookies: out of Start (a chimney, a smokestack, a canopy), arcing onto a heap
// around Target. Both are in the item's own space, so they turn with it however it landed.
USTRUCT(BlueprintType)
struct FCookieSpout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Spout")
	FVector Start = FVector::ZeroVector;

	// Each cookie leaves from a random point up to this far from Start on each axis.
	UPROPERTY(EditAnywhere, Category = "Spout")
	FVector StartJitter = FVector::ZeroVector;

	// Centre of the heap this spout feeds. Only X and Y count; the cookies land on whatever is there.
	UPROPERTY(EditAnywhere, Category = "Spout")
	FVector Target = FVector::ZeroVector;

	// Cookies land anywhere within this radius (cm) of Target, so the heap grows round.
	UPROPERTY(EditAnywhere, Category = "Spout", meta = (ClampMin = "0.0"))
	float TargetSpread = 200.f;

	// Time in the air. Longer flights arc higher.
	UPROPERTY(EditAnywhere, Category = "Spout", meta = (ClampMin = "0.2"))
	float FlightSeconds = 1.f;

	// Fraction of the item's production that comes out of this spout.
	UPROPERTY(EditAnywhere, Category = "Spout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Share = 1.f;

	// How many cookies a second are drawn flying. Past this, each drawn cookie stands for several.
	UPROPERTY(EditAnywhere, Category = "Spout", meta = (ClampMin = "1.0"))
	float VisibleCookiesPerSecond = 30.f;
};

// An item standing in the world from the start (dropped onto whatever is under Location).
USTRUCT(BlueprintType)
struct FCookieDemoPlacement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Demo")
	ECookieItem Item = ECookieItem::Factory;

	UPROPERTY(EditAnywhere, Category = "Demo")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Demo")
	float Yaw = 0.f;
};

// A heap of cookies already there when play starts (they count toward the bank, like any cookie in the world).
USTRUCT(BlueprintType)
struct FCookieDemoHeap
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Demo")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Demo", meta = (ClampMin = "1"))
	int64 Cookies = 100000;
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

	// Big producers spew streams of cookies from these instead of popping single cookies out of their top.
	UPROPERTY(EditAnywhere, Category = "Item")
	TArray<FCookieSpout> Spouts;
};
