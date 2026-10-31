#include "CookieGameMode.h"
#include "CookieCharacter.h"
#include "CookieGameSettings.h"
#include "CookieHUD.h"

ACookieGameMode::ACookieGameMode()
{
	DefaultPawnClass = ACookieCharacter::StaticClass();
	HUDClass = ACookieHUD::StaticClass();
}

void ACookieGameMode::BeginPlay()
{
	Super::BeginPlay();
	Cookies = UCookieGameSettings::Get().StartingCookies;
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
