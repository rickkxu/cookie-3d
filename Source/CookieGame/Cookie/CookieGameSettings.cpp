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
	Cookie.LaunchSpeed = 1100.f;
	Cookie.SpawnDistance = 60.f;
	Cookie.TumbleDegreesPerSecond = 360.f;
	Cookie.bUseCCD = true;

	// Toaster oven: the kit's toaster, scaled up to a chunky 50 cm.
	ToasterOven.DisplayName = NSLOCTEXT("CookieGame", "ToasterOven", "Toaster Oven");
	ToasterOven.BaseCost = 15.0;
	ToasterOven.CookiesPerSecond = 0.5f;
	ToasterOven.EmitIntervalSeconds = 4.f;
	ToasterOven.Launch.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(ToasterMeshPath));
	ToasterOven.Launch.Scale = FVector(1.6);
	ToasterOven.Launch.bPlaceholderMaterial = false;
	ToasterOven.Launch.Color = FLinearColor(0.6f, 0.6f, 0.65f);
	ToasterOven.Launch.LaunchSpeed = 900.f;
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
	Grandma.EmitIntervalSeconds = 2.5f;
	Grandma.Launch.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(CylinderPath));
	Grandma.Launch.Scale = FVector(0.4, 0.4, 1.5);
	Grandma.Launch.Color = FLinearColor(0.7f, 0.55f, 0.85f);
	Grandma.Launch.bPlaceholderMaterial = false;
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
