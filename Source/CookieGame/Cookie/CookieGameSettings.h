#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CookieTypes.h"
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

	// The cookie shot by left-click.
	UPROPERTY(Config, EditAnywhere, Category = "Launch")
	FCookieLaunchSpec Cookie;

	// Extra upward velocity (cm/s) added to every launch so things fly in a visible arc.
	UPROPERTY(Config, EditAnywhere, Category = "Launch")
	float LaunchUpBoost = 300.f;

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
};
