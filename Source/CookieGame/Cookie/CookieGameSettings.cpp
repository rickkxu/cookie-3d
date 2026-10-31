#include "CookieGameSettings.h"

namespace
{
	// Engine placeholder shapes. Both use BasicShapeMaterial, which has a "Color" parameter.
	const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
}

UCookieGameSettings::UCookieGameSettings()
{
	// Cookie: a flat brown disc, 25 cm across.
	Cookie.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(CylinderPath));
	Cookie.Scale = FVector(0.25, 0.25, 0.05);
	Cookie.Color = FLinearColor(0.55f, 0.33f, 0.12f);
	Cookie.LaunchSpeed = 1100.f;
	Cookie.SpawnDistance = 60.f;
	Cookie.TumbleDegreesPerSecond = 360.f;
	Cookie.bUseCCD = true;

	// Toaster oven: a grey box.
	ToasterOven.DisplayName = NSLOCTEXT("CookieGame", "ToasterOven", "Toaster Oven");
	ToasterOven.BaseCost = 15.0;
	ToasterOven.CookiesPerSecond = 0.5f;
	ToasterOven.EmitIntervalSeconds = 4.f;
	ToasterOven.Launch.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(CubePath));
	ToasterOven.Launch.Scale = FVector(0.5, 0.35, 0.3);
	ToasterOven.Launch.Color = FLinearColor(0.6f, 0.6f, 0.65f);
	ToasterOven.Launch.LaunchSpeed = 900.f;
	ToasterOven.Launch.SpawnDistance = 110.f;
	ToasterOven.Launch.Bounciness = 0.45f;

	// Grandma: a tall lavender cylinder standing in for a capsule.
	Grandma.DisplayName = NSLOCTEXT("CookieGame", "Grandma", "Grandma");
	Grandma.BaseCost = 100.0;
	Grandma.CookiesPerSecond = 2.f;
	Grandma.EmitIntervalSeconds = 2.5f;
	Grandma.Launch.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(CylinderPath));
	Grandma.Launch.Scale = FVector(0.4, 0.4, 1.5);
	Grandma.Launch.Color = FLinearColor(0.7f, 0.55f, 0.85f);
	Grandma.Launch.LaunchSpeed = 900.f;
	Grandma.Launch.SpawnDistance = 130.f;
	Grandma.Launch.TumbleDegreesPerSecond = 120.f;
}

const FCookieItemSpec& UCookieGameSettings::GetItem(ECookieItem Item) const
{
	switch (Item)
	{
	case ECookieItem::ToasterOven: return ToasterOven;
	case ECookieItem::Grandma:     return Grandma;
	default:                       return Grandma;
	}
}
