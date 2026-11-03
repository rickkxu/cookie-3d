#include "CookieGameSettings.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"

namespace
{
	// Stylized meshes from the Low Poly Interior kit (Fab), with their own palette material.
	const TCHAR* CookieMeshPath = TEXT("/Game/LowPolyInterior/Meshes/KitchenObjects/Mesh_KitchenObjects_Cookie_01.Mesh_KitchenObjects_Cookie_01");
	const TCHAR* ToasterMeshPath = TEXT("/Game/LowPolyInterior/Meshes/KitchenObjects/Mesh_KitchenObjects_Toaster.Mesh_KitchenObjects_Toaster");
	const TCHAR* PlaceholderMaterialPath = TEXT("/Game/Cookie/M_CookiePlaceholder.M_CookiePlaceholder");
	const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
}

UCookieGameSettings::UCookieGameSettings()
{
	PlaceholderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(PlaceholderMaterialPath));

	// Cookie: the kit's 13 cm cookie, doubled so it reads from across the room.
	Cookie.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(CookieMeshPath));
	Cookie.Scale = FVector(2.0);
	Cookie.bPlaceholderMaterial = false;
	Cookie.Color = FLinearColor(0.8f, 0.6f, 0.32f);  // The kit cookie's dough, for pile cores and the HUD.
	Cookie.LaunchSpeed = 750.f;  // Lands across a ~4 m room instead of slamming into the far wall.
	Cookie.SpawnDistance = 60.f;
	Cookie.TumbleDegreesPerSecond = 360.f;
	Cookie.bUseCCD = true;
	Cookie.Friction = 0.9f;
	Cookie.AngularDamping = 0.3f;

	// Toaster oven: the kit's toaster, scaled up to a chunky 50 cm.
	ToasterOven.DisplayName = NSLOCTEXT("CookieGame", "ToasterOven", "Toaster Oven");
	ToasterOven.BaseCost = 15.0;
	ToasterOven.CookiesPerSecond = 0.5f;
	ToasterOven.Launch.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(ToasterMeshPath));
	ToasterOven.Launch.Scale = FVector(1.6);
	ToasterOven.Launch.bPlaceholderMaterial = false;
	ToasterOven.Launch.Color = FLinearColor(0.6f, 0.6f, 0.65f);
	ToasterOven.Launch.LaunchSpeed = 650.f;
	ToasterOven.Launch.SpawnDistance = 110.f;
	ToasterOven.Launch.Bounciness = 0.45f;

	// Grandma: the stylized grandma from Fab (FBX download, Mixamo rig), with the template's idle and falls
	// retargeted onto her by CookieRetargetCommandlet. She imports ~4.9 m tall, so she's scaled to ~155 cm.
	// Launch.Mesh/Scale are unused (she's a ragdoll).
	GrandmaMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Cookie/Grandma/Model/SK_Grandma.SK_Grandma")));
	GrandmaScale = 0.32f;
	GrandmaPelvisBone = TEXT("Hips");
	GrandmaHeadBone = TEXT("Head");
	GrandmaLeftUpperArmBone = TEXT("LeftArm");
	GrandmaRightUpperArmBone = TEXT("RightArm");
	GrandmaIdleAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Cookie/Grandma/Anims/Grandma_MM_Idle.Grandma_MM_Idle")));
	for (const TCHAR* Fall : { TEXT("MM_Death_Back_01"), TEXT("MM_Death_Front_01"), TEXT("MM_Death_Front_02"),
		TEXT("MM_Death_Front_03"), TEXT("MM_Death_Left_01"), TEXT("MM_Death_Right_01") })
	{
		GrandmaGetUpAnims.Emplace(FSoftObjectPath(FString::Printf(TEXT("/Game/Cookie/Grandma/Anims/Grandma_%s.Grandma_%s"), Fall, Fall)));
	}

	Grandma.DisplayName = NSLOCTEXT("CookieGame", "Grandma", "Grandma");
	Grandma.BaseCost = 100.0;
	Grandma.CookiesPerSecond = 2.f;
	Grandma.Launch.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(CylinderPath));
	Grandma.Launch.Scale = FVector(0.4, 0.4, 1.5);
	Grandma.Launch.Color = FLinearColor(0.7f, 0.55f, 0.85f);
	Grandma.Launch.bPlaceholderMaterial = false;
	Grandma.Launch.LaunchSpeed = 650.f;
	Grandma.Launch.SpawnDistance = 130.f;
	Grandma.Launch.TumbleDegreesPerSecond = 120.f;

	// The big producers. Each tier costs ~15x the last and makes ~8x as much. Models are merged meshes in
	// Content/Cookie/Items, built from the kit and the faceted foliage pieces; spout positions are in mesh space
	// (bakery and factory pivots sit 9 cm under their ground, the tree's at its base).
	auto Spout = [](FVector Start, FVector Jitter, FVector Target, float Spread, float Flight, float Share, float Visible)
	{
		FCookieSpout S;
		S.Start = Start;
		S.StartJitter = Jitter;
		S.Target = Target;
		S.TargetSpread = Spread;
		S.FlightSeconds = Flight;
		S.Share = Share;
		S.VisibleCookiesPerSecond = Visible;
		return S;
	};

	// Cookie tree: about the size of the yard trees. Cookies drop out of the whole canopy onto a heap round the trunk.
	CookieTree.DisplayName = NSLOCTEXT("CookieGame", "CookieTree", "Cookie Tree");
	CookieTree.BaseCost = 1500.0;
	CookieTree.CookiesPerSecond = 15.f;
	CookieTree.Launch.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Cookie/Items/SM_CookieTree.SM_CookieTree")));
	CookieTree.Launch.bPlaceholderMaterial = false;
	CookieTree.Launch.Color = FLinearColor(0.25f, 0.55f, 0.12f);
	CookieTree.Launch.LaunchSpeed = 650.f;
	CookieTree.Launch.SpawnDistance = 400.f;
	CookieTree.Launch.TumbleDegreesPerSecond = 60.f;
	CookieTree.Spouts.Add(Spout(FVector(20, -10, 300), FVector(150, 150, 80), FVector(0, 0, 0), 260.f, 0.55f, 1.f, 20.f));

	// Bakery: a little smaller than the house. Cookies arc out of the oven chimney behind it and tumble out the door.
	Bakery.DisplayName = NSLOCTEXT("CookieGame", "Bakery", "Bakery");
	Bakery.BaseCost = 25000.0;
	Bakery.CookiesPerSecond = 120.f;
	Bakery.Launch.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Cookie/Items/SM_Bakery.SM_Bakery")));
	Bakery.Launch.bPlaceholderMaterial = false;
	Bakery.Launch.Color = FLinearColor(0.86f, 0.56f, 0.52f);
	Bakery.Launch.LaunchSpeed = 550.f;
	Bakery.Launch.SpawnDistance = 1000.f;
	Bakery.Launch.TumbleDegreesPerSecond = 25.f;
	Bakery.Spouts.Add(Spout(FVector(250, 160, 690), FVector(15, 15, 5), FVector(700, 950, 0), 380.f, 2.2f, 0.6f, 30.f));
	Bakery.Spouts.Add(Spout(FVector(62, -350, 110), FVector(35, 5, 60), FVector(60, -1000, 0), 260.f, 1.1f, 0.4f, 20.f));

	// Cookie factory: bigger than the house. Geysers out of both smokestacks build a mountain behind it, and the
	// conveyor chute pours a heap beside it.
	Factory.DisplayName = NSLOCTEXT("CookieGame", "Factory", "Cookie Factory");
	Factory.BaseCost = 400000.0;
	Factory.CookiesPerSecond = 1000.f;
	Factory.Launch.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Cookie/Items/SM_Factory.SM_Factory")));
	Factory.Launch.bPlaceholderMaterial = false;
	Factory.Launch.Color = FLinearColor(0.42f, 0.13f, 0.07f);
	Factory.Launch.LaunchSpeed = 450.f;
	Factory.Launch.SpawnDistance = 1900.f;
	Factory.Launch.TumbleDegreesPerSecond = 10.f;
	Factory.Spouts.Add(Spout(FVector(700, 300, 1728), FVector(30, 30, 5), FVector(500, 2600, 0), 700.f, 3.2f, 0.38f, 70.f));
	Factory.Spouts.Add(Spout(FVector(330, 300, 1728), FVector(30, 30, 5), FVector(500, 2600, 0), 700.f, 3.2f, 0.38f, 70.f));
	Factory.Spouts.Add(Spout(FVector(1590, -200, 150), FVector(10, 40, 5), FVector(2150, -200, 0), 350.f, 0.8f, 0.24f, 40.f));

	PileCoreMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Cookie/Items/MI_Item_Dough.MI_Item_Dough")));
}

const FCookieItemSpec& UCookieGameSettings::GetItem(ECookieItem Item) const
{
	switch (Item)
	{
	case ECookieItem::ToasterOven: return ToasterOven;
	case ECookieItem::Grandma:     return Grandma;
	case ECookieItem::CookieTree:  return CookieTree;
	case ECookieItem::Bakery:      return Bakery;
	case ECookieItem::Factory:     return Factory;
	default:                       return Grandma;
	}
}
