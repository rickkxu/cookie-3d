#include "CookieGameMode.h"
#include "CookieCharacter.h"
#include "CookieGameSettings.h"
#include "CookieHUD.h"

ACookieGameMode::ACookieGameMode()
{
	DefaultPawnClass = ACookieCharacter::StaticClass();
	HUDClass = ACookieHUD::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

void ACookieGameMode::BeginPlay()
{
	Super::BeginPlay();
	Cookies = UCookieGameSettings::Get().StartingCookies;
}

void ACookieGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Cookies += GetCookiesPerSecond() * DeltaSeconds;
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
