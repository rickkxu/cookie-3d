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

	if (Settings.StressTestToasterOvens > 0 || Settings.StressTestGrandmas > 0)
	{
		StartStressTest(Settings.StressTestToasterOvens, Settings.StressTestGrandmas);
	}
}

void ACookieGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Cookies += GetCookiesPerSecond() * DeltaSeconds;

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
		Total += GetOwned(Item) * static_cast<double>(Settings.GetItem(Item).CookiesPerSecond);
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
	return true;
}

void ACookieGameMode::StartStressTest(int32 ToasterOvens, int32 Grandmas)
{
	StressPending[static_cast<int32>(ECookieItem::ToasterOven)] += FMath::Max(ToasterOvens, 0);
	StressPending[static_cast<int32>(ECookieItem::Grandma)] += FMath::Max(Grandmas, 0);
	GetWorldTimerManager().SetTimer(StressTimer, this, &ACookieGameMode::SpawnStressBatch, 0.05f, true);
	UE_LOG(LogTemp, Display, TEXT("Cookie stress test: dropping %d toaster ovens and %d grandmas"), ToasterOvens, Grandmas);
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

	// The same thread timings "stat unit" shows: whichever is largest is what limits the frame rate.
	UE_LOG(LogTemp, Display,
		TEXT("Cookie stats: %.0f fps (game %.1f ms, render %.1f ms, gpu %.1f ms; emit %.2f, piles %.2f ms/frame) | flying %d (cookies %d) | settled cookies %d, ovens %d | grandmas %d live (baking %d, animating %d), %d frozen | piles %d holding %lld"),
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
		Settled ? Settled->GetPiledCookieCount() : int64(0));

	StatsSeconds = 0.f;
	StatsFrames = 0;
}
