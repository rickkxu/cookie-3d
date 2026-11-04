#include "CookieGameMode.h"
#include "CookieCharacter.h"
#include "CookieGameSettings.h"
#include "CookieGrandma.h"
#include "CookieHUD.h"
#include "CookieLaunchable.h"
#include "CookieSettledManager.h"
#include "EngineUtils.h"
#include "RHI.h"
#include "RenderCore.h"
#include "GameFramework/PlayerStart.h"
#include "TimerManager.h"

ACookieGameMode::ACookieGameMode()
{
	DefaultPawnClass = ACookieCharacter::StaticClass();
	HUDClass = ACookieHUD::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

void ACookieGameMode::BeginPlay()
{
	Super::BeginPlay();

	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	Cookies = Settings.StartingCookies;
	Settled = GetWorld()->SpawnActor<ACookieSettledManager>();

	StressPending[static_cast<int32>(ECookieItem::CookieTree)] += Settings.StressTestCookieTrees;
	StressPending[static_cast<int32>(ECookieItem::Bakery)] += Settings.StressTestBakeries;
	StressPending[static_cast<int32>(ECookieItem::Factory)] += Settings.StressTestFactories;
	StartStressTest(Settings.StressTestToasterOvens, Settings.StressTestGrandmas);

	PlaceDemoItems();
}

void ACookieGameMode::PlaceDemoItems()
{
	if (!UCookieGameSettings::Get().bDemoMode)
	{
		return;
	}

	// Items standing from the start: dropped straight onto the ground under each spot, already settled.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CookieDemoPlacement), false);
	Params.AddIgnoredActor(Settled);
	for (const FCookieDemoPlacement& Placement : UCookieGameSettings::Get().DemoPlacements)
	{
		FVector Location = Placement.Location;
		FHitResult Hit;
		const FVector Top = Location + FVector(0.0, 0.0, 5000.0);
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Top, Top - FVector(0.0, 0.0, 10000.0), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			Location.Z = Hit.ImpactPoint.Z;
		}
		// Bakery and factory pivots sit 9 cm under their foundations.
		if (Placement.Item == ECookieItem::Bakery || Placement.Item == ECookieItem::Factory)
		{
			Location.Z -= 9.0;
		}
		// Clouds hover up in the sky over the spot.
		const FCookieLaunchSpec& Launch = UCookieGameSettings::Get().GetItem(Placement.Item).Launch;
		if (Launch.bFloats)
		{
			Location.Z += Launch.FloatHeight;
		}
		++Owned[static_cast<int32>(Placement.Item)];
		Settled->AddItem(Placement.Item, FTransform(FRotator(0.f, Placement.Yaw, 0.f), Location));
	}

	// Heaps already there: real cookies, so they're in the bank too.
	for (const FCookieDemoHeap& Heap : UCookieGameSettings::Get().DemoHeaps)
	{
		Settled->AddToPileNear(Heap.Location, Heap.Cookies);
		Cookies += static_cast<double>(Heap.Cookies);
	}
}

void ACookieGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// No passive income here: helpers bank each cookie as they pop it out (ACookieLaunchable::PopCookie).

	GrandmaBudgetSeconds += DeltaSeconds;
	if (GrandmaBudgetSeconds >= 0.5f)
	{
		GrandmaBudgetSeconds = 0.f;
		ACookieGrandma::UpdateAnimationBudget(GetWorld());
	}

	if (UCookieGameSettings::Get().bLogStats)
	{
		LogStats(DeltaSeconds);
	}
}

double ACookieGameMode::GetCookiesPerSecond() const
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	double Total = 0.0;
	for (ECookieItem Item : TEnumRange<ECookieItem>())
	{
		Total += GetOwned(Item) * static_cast<double>(Settings.GetProduction(Item));
	}
	return Total;
}

double ACookieGameMode::GetCost(ECookieItem Item) const
{
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	const double Base = Settings.GetItem(Item).BaseCost;
	return FMath::CeilToDouble(Base * FMath::Pow(Settings.CostGrowth, static_cast<double>(GetOwned(Item))));
}

bool ACookieGameMode::TryBuy(ECookieItem Item)
{
	const double Cost = GetCost(Item);
	if (Cookies < Cost)
	{
		return false;
	}
	Cookies -= Cost;
	++Owned[static_cast<int32>(Item)];

	// What you spend leaves the world too, thinning every heap and scattered cookie a little.
	if (Settled)
	{
		Settled->RemoveCookies(static_cast<int64>(Cost));
	}
	return true;
}

void ACookieGameMode::StartStressTest(int32 ToasterOvens, int32 Grandmas)
{
	StressPending[static_cast<int32>(ECookieItem::ToasterOven)] += FMath::Max(ToasterOvens, 0);
	StressPending[static_cast<int32>(ECookieItem::Grandma)] += FMath::Max(Grandmas, 0);
	int32 Total = 0;
	for (int32 Pending : StressPending)
	{
		Total += Pending;
	}
	if (Total == 0)
	{
		return;
	}
	GetWorldTimerManager().SetTimer(StressTimer, this, &ACookieGameMode::SpawnStressBatch, 0.05f, true);
	UE_LOG(LogTemp, Display, TEXT("Cookie stress test: dropping %d items (%d toaster ovens, %d grandmas)"), Total, ToasterOvens, Grandmas);
}

void ACookieGameMode::SpawnStressBatch()
{
	FVector Center = FVector::ZeroVector;
	if (const APawn* Player = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr)
	{
		Center = Player->GetActorLocation();
	}
	else if (TActorIterator<APlayerStart> Start(GetWorld()); Start)
	{
		Center = Start->GetActorLocation();
	}

	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	int32 Spawned = 0;
	for (ECookieItem Item : TEnumRange<ECookieItem>())
	{
		int32& Pending = StressPending[static_cast<int32>(Item)];
		while (Pending > 0 && Spawned < Settings.StressSpawnsPerBatch)
		{
			--Pending;
			++Spawned;
			++Owned[static_cast<int32>(Item)];

			const FVector2D Offset = FMath::RandPointInCircle(Settings.StressDropRadius);
			const FVector Location = Center + FVector(Offset.X, Offset.Y, 400.0);
			const FVector2D Drift = FMath::RandPointInCircle(200.f);
			const FRotator Rotation(0.f, FMath::FRandRange(0.f, 360.f), 0.f);
			ACookieLaunchable::SpawnItem(GetWorld(), Item, Location, Rotation, FVector(Drift.X, Drift.Y, 0.0));
		}
	}

	if (Spawned == 0)
	{
		GetWorldTimerManager().ClearTimer(StressTimer);
	}
}

void ACookieGameMode::LogStats(float DeltaSeconds)
{
	StatsSeconds += DeltaSeconds;
	++StatsFrames;
	if (StatsSeconds < 2.f)
	{
		return;
	}

	int32 FlyingThings = 0;
	for (TActorIterator<ACookieLaunchable> It(GetWorld()); It; ++It)
	{
		++FlyingThings;
	}

	int32 Grandmas = 0;
	int32 BakingGrandmas = 0;
	int32 AnimatingGrandmas = 0;
	for (TActorIterator<ACookieGrandma> It(GetWorld()); It; ++It)
	{
		++Grandmas;
		BakingGrandmas += It->IsBaking() ? 1 : 0;
		AnimatingGrandmas += It->IsAnimating() ? 1 : 0;
	}

	double EmitMs = 0.0;
	double PileMs = 0.0;
	if (Settled)
	{
		Settled->ConsumeTimings(EmitMs, PileMs);
	}

	FString PlayerLocation = TEXT("-");
	if (const APawn* Player = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr)
	{
		const FVector L = Player->GetActorLocation();
		PlayerLocation = FString::Printf(TEXT("(%.0f, %.0f, %.0f)"), L.X, L.Y, L.Z);
	}

	// The same thread timings "stat unit" shows: whichever is largest is what limits the frame rate.
	UE_LOG(LogTemp, Display,
		TEXT("Cookie stats: %.0f fps (game %.1f ms, render %.1f ms, gpu %.1f ms; emit %.2f, piles %.2f ms/frame) | flying %d (cookies %d) | settled cookies %d, ovens %d | grandmas %d live (baking %d, animating %d), %d frozen | piles %d holding %lld (largest %lld, %d surface cookies) | ghosts %d | bank %.0f | producers %d trees, %d bakeries, %d factories (%d in the air) | player at %s"),
		StatsFrames / StatsSeconds,
		FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
		FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()),
		EmitMs / StatsFrames, PileMs / StatsFrames,
		FlyingThings, FlyingCookies,
		Settled ? Settled->GetSettledCookieCount() : 0,
		Settled ? Settled->GetSettledItemCount(ECookieItem::ToasterOven) : 0,
		Grandmas, BakingGrandmas, AnimatingGrandmas,
		Settled ? Settled->GetSettledItemCount(ECookieItem::Grandma) : 0,
		Settled ? Settled->GetPileCount() : 0,
		Settled ? Settled->GetPiledCookieCount() : int64(0),
		Settled ? Settled->GetLargestPileCount() : int64(0),
		Settled ? Settled->GetPileSurfaceInstanceCount() : 0,
		Settled ? Settled->GetGhostCookieCount() : 0,
		Cookies,
		Settled ? Settled->GetSettledItemCount(ECookieItem::CookieTree) : 0,
		Settled ? Settled->GetSettledItemCount(ECookieItem::Bakery) : 0,
		Settled ? Settled->GetSettledItemCount(ECookieItem::Factory) : 0,
		FlyingThings - FlyingCookies,
		*PlayerLocation);

	StatsSeconds = 0.f;
	StatsFrames = 0;
}
