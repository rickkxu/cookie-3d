#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CookieTypes.h"
#include "Materials/MaterialInterface.h"

class UAnimSequence;
class UPhysicsAsset;
class USkeletalMesh;

#include "CookieGameSettings.generated.h"

/**
 * Every tunable number in one place.
 * Edit in the editor under Project Settings > Game > Cookie Game (saved to Config/DefaultGame.ini).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Cookie Game"))
class COOKIEGAME_API UCookieGameSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCookieGameSettings();

	static const UCookieGameSettings& Get() { return *GetDefault<UCookieGameSettings>(); }

	const FCookieItemSpec& GetItem(ECookieItem Item) const;

	// Flat material with a "Color" vector parameter, used on every placeholder shape.
	UPROPERTY(Config, EditAnywhere, Category = "Launch")
	TSoftObjectPtr<UMaterialInterface> PlaceholderMaterial;

	// Each purchase multiplies that item's price by this: cost = base * growth^owned.
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (ClampMin = "1.0"))
	double CostGrowth = 1.15;

	// Cookies in the bank when play starts. Raise it to test buying without clicking first.
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (ClampMin = "0.0"))
	double StartingCookies = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Items")
	FCookieItemSpec ToasterOven;

	UPROPERTY(Config, EditAnywhere, Category = "Items")
	FCookieItemSpec Grandma;

	UPROPERTY(Config, EditAnywhere, Category = "Items")
	FCookieItemSpec CookieTree;

	UPROPERTY(Config, EditAnywhere, Category = "Items")
	FCookieItemSpec Bakery;

	UPROPERTY(Config, EditAnywhere, Category = "Items")
	FCookieItemSpec Factory;

	// Grandmas are ragdolls, so they use a skeletal mesh instead of Grandma.Launch.Mesh.
	// Must use the UE5 mannequin skeleton: the bone names and animations below depend on it.
	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body")
	TSoftObjectPtr<USkeletalMesh> GrandmaMesh;

	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body", meta = (ClampMin = "0.01"))
	float GrandmaScale = 1.f;

	// Ragdoll body to use instead of the mesh's own. Leave empty to use the mesh's.
	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body")
	TSoftObjectPtr<UPhysicsAsset> GrandmaPhysicsAsset;

	// Bones the get-up logic reads to tell how she landed: which way her head points and which way her chest faces.
	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body")
	FName GrandmaPelvisBone = TEXT("pelvis");

	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body")
	FName GrandmaHeadBone = TEXT("head");

	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body")
	FName GrandmaLeftUpperArmBone = TEXT("upperarm_l");

	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body")
	FName GrandmaRightUpperArmBone = TEXT("upperarm_r");

	// Falls that end lying down. Played backwards to get up, picking one that lies the same way up as she landed.
	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body")
	TArray<TSoftObjectPtr<UAnimSequence>> GrandmaGetUpAnims;

	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body", meta = (ClampMin = "0.1"))
	float GrandmaGetUpPlayRate = 1.5f;

	// Looped while she bakes.
	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body")
	TSoftObjectPtr<UAnimSequence> GrandmaIdleAnim;

	// Only this many baking grandmas animate, the ones nearest the player...
	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body", meta = (ClampMin = "0"))
	int32 MaxAnimatedGrandmas = 30;

	// ...and only within this distance (cm). The rest freeze mid-bake into cheap instances.
	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body", meta = (ClampMin = "0.0"))
	float GrandmaAnimateRadius = 2500.f;

	// Detail level of the mesh frozen grandmas use (0 = full detail). Quinn has 0-2.
	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body", meta = (ClampMin = "0"))
	int32 GrandmaFrozenLOD = 2;

	// How many frozen grandmas can come back to life per budget update (2 per second), so walking into a crowd doesn't hitch.
	UPROPERTY(Config, EditAnywhere, Category = "Grandma Body", meta = (ClampMin = "1"))
	int32 MaxGrandmaThawsPerUpdate = 4;

	// The cookie shot by left-click.
	UPROPERTY(Config, EditAnywhere, Category = "Launch")
	FCookieLaunchSpec Cookie;

	// Extra upward velocity (cm/s) added to every launch so things fly in a visible arc.
	UPROPERTY(Config, EditAnywhere, Category = "Launch")
	float LaunchUpBoost = 300.f;

	// Produced cookies pop straight up out of their helper at this speed (cm/s)...
	UPROPERTY(Config, EditAnywhere, Category = "Production")
	float EmitPopSpeed = 350.f;

	// ...plus up to this much random sideways speed, so they scatter around it.
	UPROPERTY(Config, EditAnywhere, Category = "Production")
	float EmitSpreadSpeed = 150.f;

	// Past this many loose cookies at rest, newly landing cookies grow pile meshes instead.
	UPROPERTY(Config, EditAnywhere, Category = "Production", meta = (ClampMin = "0"))
	int32 MaxPhysicalCookies = 10000;

	// Produced cookies only fly as physics objects while fewer than this many cookies are in the air; the rest fly the
	// same arc without simulating (ghost cookies) and land the same way.
	// Keeps a big bakery from flooding the physics engine. The player's own shots always fly.
	UPROPERTY(Config, EditAnywhere, Category = "Production", meta = (ClampMin = "0"))
	int32 MaxFlyingCookies = 150;

	// Cookies landing within this distance (cm) of an existing pile join it instead of starting a new one.
	UPROPERTY(Config, EditAnywhere, Category = "Piles", meta = (ClampMin = "1.0"))
	float PileMergeRadius = 250.f;

	// Indoors, a pile this full stops growing and the next cookie nearby starts a new pile, so heaps don't fill a room.
	// Outdoors piles have no limit: they grow and merge into mountains.
	UPROPERTY(Config, EditAnywhere, Category = "Piles", meta = (ClampMin = "1"))
	int32 MaxCookiesPerPile = 3000;

	// The player's house. Heaps that start inside are indoor heaps; outdoor heaps never show cookies inside it.
	UPROPERTY(Config, EditAnywhere, Category = "Piles")
	FBox HouseInterior = FBox(FVector(-512.0, -387.0, -20.0), FVector(512.0, 387.0, 262.0));

	// Height (cm) of the open ground outside. Mountains stand on it.
	UPROPERTY(Config, EditAnywhere, Category = "Piles")
	float GroundZ = -18.f;

	// Outdoor heaps wider than this (cm) count as mountains: their cookies skip the furniture checks, they stand on the
	// ground, and their shadow comes from the core alone.
	UPROPERTY(Config, EditAnywhere, Category = "Piles", meta = (ClampMin = "50.0"))
	float PileLargeRadius = 300.f;

	// Time per frame (ms) for laying out heaps' cookies as they grow. Mountains take a few frames to re-lay.
	UPROPERTY(Config, EditAnywhere, Category = "Piles", meta = (ClampMin = "0.1"))
	float PileBuildBudgetMs = 3.f;

	// The heap core under the surface cookies (seen only through gaps between them).
	UPROPERTY(Config, EditAnywhere, Category = "Piles")
	TSoftObjectPtr<UMaterialInterface> PileCoreMaterial;

	// Most produced cookies drawn in the air at once. Past this, streams go straight onto their heaps.
	UPROPERTY(Config, EditAnywhere, Category = "Production", meta = (ClampMin = "0"))
	int32 MaxGhostCookies = 15000;

	// Space each piled cookie takes up (cm^3), which sets how fast piles grow. ~4000 = loosely packed cookies.
	UPROPERTY(Config, EditAnywhere, Category = "Piles", meta = (ClampMin = "1.0"))
	float PileCookieVolume = 4000.f;

	// How many times over cookies cover a heap's footprint. Around 2 they overlap like scales with no gaps.
	UPROPERTY(Config, EditAnywhere, Category = "Piles", meta = (ClampMin = "0.0"))
	float PileSurfaceCoverage = 3.2f;

	// Extra loose cookies scattered around a heap's base, as a fraction of its surface cookies.
	UPROPERTY(Config, EditAnywhere, Category = "Piles", meta = (ClampMin = "0.0"))
	float PileSkirtFraction = 0.12f;

	// How messily surface cookies lie: 0 = flat against the slope.
	UPROPERTY(Config, EditAnywhere, Category = "Piles", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PileSurfaceJitter = 0.3f;

	// Growing piles are resized at most this often (resizing rebuilds collision).
	UPROPERTY(Config, EditAnywhere, Category = "Piles", meta = (ClampMin = "0.0"))
	float PileRefreshSeconds = 0.25f;

	// A thrown thing counts as still below this speed (cm/s)...
	UPROPERTY(Config, EditAnywhere, Category = "Settling", meta = (ClampMin = "0.0"))
	float SettleSpeed = 10.f;

	// ...and this spin (degrees/s)...
	UPROPERTY(Config, EditAnywhere, Category = "Settling", meta = (ClampMin = "0.0"))
	float SettleSpinDegrees = 20.f;

	// ...and freezes into a cheap static instance after staying still this long.
	UPROPERTY(Config, EditAnywhere, Category = "Settling", meta = (ClampMin = "0.0"))
	float SettleSeconds = 0.5f;

	// After flying this long, anything merely slow (under 10x SettleSpeed) also freezes, so things wedged
	// together and jittering don't simulate forever.
	UPROPERTY(Config, EditAnywhere, Category = "Settling", meta = (ClampMin = "0.5"))
	float MaxFlyingSeconds = 10.f;

	// Purchases per second when right-click is first held...
	UPROPERTY(Config, EditAnywhere, Category = "Hold To Buy", meta = (ClampMin = "0.1"))
	float BuyStartRate = 2.f;

	// ...ramping up to this rate...
	UPROPERTY(Config, EditAnywhere, Category = "Hold To Buy", meta = (ClampMin = "0.1"))
	float BuyMaxRate = 12.f;

	// ...over this many seconds of holding.
	UPROPERTY(Config, EditAnywhere, Category = "Hold To Buy", meta = (ClampMin = "0.01"))
	float BuyRampSeconds = 3.f;

	// Safety cap so a frame hitch can't dump dozens of items at once.
	UPROPERTY(Config, EditAnywhere, Category = "Hold To Buy", meta = (ClampMin = "1"))
	int32 MaxBuysPerFrame = 4;

	// Free items rained down around the player when play starts. Also: console command "CookieStress <ovens> <grandmas>".
	UPROPERTY(Config, EditAnywhere, Category = "Stress Test", meta = (ClampMin = "0"))
	int32 StressTestToasterOvens = 0;

	UPROPERTY(Config, EditAnywhere, Category = "Stress Test", meta = (ClampMin = "0"))
	int32 StressTestGrandmas = 0;

	UPROPERTY(Config, EditAnywhere, Category = "Stress Test", meta = (ClampMin = "0"))
	int32 StressTestCookieTrees = 0;

	UPROPERTY(Config, EditAnywhere, Category = "Stress Test", meta = (ClampMin = "0"))
	int32 StressTestBakeries = 0;

	UPROPERTY(Config, EditAnywhere, Category = "Stress Test", meta = (ClampMin = "0"))
	int32 StressTestFactories = 0;

	// Items already standing (and producing) when play starts, for filming and testing. Each counts as owned.
	UPROPERTY(Config, EditAnywhere, Category = "Stress Test")
	TArray<FCookieDemoPlacement> DemoPlacements;

	// Heaps already there when play starts, for filming and testing. Their cookies are in the bank.
	UPROPERTY(Config, EditAnywhere, Category = "Stress Test")
	TArray<FCookieDemoHeap> DemoHeaps;

	// Speeds up everything's production (for filming mountains growing). The bank still only counts cookies
	// that physically came out, and the HUD's per-second figure includes it.
	UPROPERTY(Config, EditAnywhere, Category = "Stress Test", meta = (ClampMin = "0.0"))
	float DemoProductionMultiplier = 1.f;

	// Cookies per second one of this item makes, including the demo multiplier.
	float GetProduction(ECookieItem Item) const { return GetItem(Item).CookiesPerSecond * DemoProductionMultiplier; }

	// How many stress test items drop every 0.05 s.
	UPROPERTY(Config, EditAnywhere, Category = "Stress Test", meta = (ClampMin = "1"))
	int32 StressSpawnsPerBatch = 1;

	UPROPERTY(Config, EditAnywhere, Category = "Stress Test", meta = (ClampMin = "0.0"))
	float StressDropRadius = 800.f;

	// Prints fps and object counts to the output log every 2 seconds.
	UPROPERTY(Config, EditAnywhere, Category = "Stress Test")
	bool bLogStats = false;
};
