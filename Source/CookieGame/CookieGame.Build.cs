// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CookieGame : ModuleRules
{
	public CookieGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"DeveloperSettings",
			"PhysicsCore",
			"MeshDescription",
			"StaticMeshDescription",
			"RenderCore",
			"RHI",
			"ProceduralMeshComponent"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// Editor-only setup commandlets (Cookie/CookieRetargetCommandlet).
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AssetRegistry", "IKRig", "IKRigEditor", "SlateCore" });
		}

		PublicIncludePaths.AddRange(new string[] {
			"CookieGame",
			"CookieGame/Cookie",
			"CookieGame/Variant_Horror",
			"CookieGame/Variant_Horror/UI",
			"CookieGame/Variant_Shooter",
			"CookieGame/Variant_Shooter/AI",
			"CookieGame/Variant_Shooter/UI",
			"CookieGame/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
