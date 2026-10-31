#include "CookieRetargetCommandlet.h"

#if WITH_EDITOR
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetSettings.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "RetargetEditor/IKRetargeterPoseGenerator.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* QuinnMeshPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple");
	// The Fab importer named the folder after the listing, en dash included.
	const TCHAR* GrandmaMeshPath = TEXT("/Game/Fab/Grandma_3D_Character_Stylized_Cartoon_Elder_Women_–_Rigged___Animated_/fbx/SkeletalMeshes/fbx.fbx");
	const TCHAR* OutputFolder = TEXT("/Game/Cookie/Grandma");

	const TCHAR* AnimsToRetarget[] = {
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"),
		TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Back_01.MM_Death_Back_01"),
		TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_01.MM_Death_Front_01"),
		TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_02.MM_Death_Front_02"),
		TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_03.MM_Death_Front_03"),
		TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Left_01.MM_Death_Left_01"),
		TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Right_01.MM_Death_Right_01"),
	};

	// One retarget chain on both skeletons: same chain name, mannequin bones -> grandma bones.
	struct FChain
	{
		const TCHAR* Name;
		const TCHAR* QuinnStart;
		const TCHAR* QuinnEnd;
		const TCHAR* GrandmaStart;
		const TCHAR* GrandmaEnd;
	};

	const FChain Chains[] = {
		{ TEXT("Spine"),         TEXT("spine_01"),   TEXT("spine_05"),  TEXT("mixamorig_Spine_02"),         TEXT("mixamorig_Spine2_04") },
		{ TEXT("Neck"),          TEXT("neck_01"),    TEXT("neck_02"),   TEXT("mixamorig_Neck_05"),          TEXT("mixamorig_Neck_05") },
		{ TEXT("Head"),          TEXT("head"),       TEXT("head"),      TEXT("mixamorig_Head_06"),          TEXT("mixamorig_Head_06") },
		{ TEXT("LeftClavicle"),  TEXT("clavicle_l"), TEXT("clavicle_l"), TEXT("mixamorig_LeftShoulder_08"), TEXT("mixamorig_LeftShoulder_08") },
		{ TEXT("LeftArm"),       TEXT("upperarm_l"), TEXT("hand_l"),    TEXT("mixamorig_LeftArm_09"),       TEXT("mixamorig_LeftHand_011") },
		{ TEXT("RightClavicle"), TEXT("clavicle_r"), TEXT("clavicle_r"), TEXT("mixamorig_RightShoulder_032"), TEXT("mixamorig_RightShoulder_032") },
		{ TEXT("RightArm"),      TEXT("upperarm_r"), TEXT("hand_r"),    TEXT("mixamorig_RightArm_033"),     TEXT("mixamorig_RightHand_035") },
		{ TEXT("LeftLeg"),       TEXT("thigh_l"),    TEXT("ball_l"),    TEXT("mixamorig_LeftUpLeg_055"),    TEXT("mixamorig_LeftToeBase_058") },
		{ TEXT("RightLeg"),      TEXT("thigh_r"),    TEXT("ball_r"),    TEXT("mixamorig_RightUpLeg_060"),   TEXT("mixamorig_RightToeBase_063") },
	};

	template <typename T>
	T* CreateAsset(const FString& Name)
	{
		const FString PackageName = FString::Printf(TEXT("%s/%s"), OutputFolder, *Name);
		UPackage* Package = CreatePackage(*PackageName);
		T* Asset = NewObject<T>(Package, *Name, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Asset);
		return Asset;
	}

	bool SavePackageOf(UObject* Asset)
	{
		UPackage* Package = Asset->GetPackage();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, nullptr, *Filename, Args);
		UE_LOG(LogTemp, Display, TEXT("CookieRetarget: %s %s"), bSaved ? TEXT("saved") : TEXT("FAILED to save"), *Filename);
		return bSaved;
	}

	UIKRigDefinition* MakeRig(const FString& Name, USkeletalMesh* Mesh, const TCHAR* Root, bool bQuinnSide)
	{
		UIKRigDefinition* Rig = CreateAsset<UIKRigDefinition>(Name);
		UIKRigController* Controller = UIKRigController::GetController(Rig);
		Controller->SetSkeletalMesh(Mesh);
		Controller->SetRetargetRoot(Root);
		for (const FChain& Chain : Chains)
		{
			const FName Added = Controller->AddRetargetChain(Chain.Name,
				bQuinnSide ? Chain.QuinnStart : Chain.GrandmaStart, bQuinnSide ? Chain.QuinnEnd : Chain.GrandmaEnd, NAME_None);
			if (Added.IsNone())
			{
				UE_LOG(LogTemp, Error, TEXT("CookieRetarget: %s couldn't add chain %s"), *Name, Chain.Name);
			}
		}
		return Rig;
	}
}
#endif

UCookieRetargetCommandlet::UCookieRetargetCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UCookieRetargetCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	USkeletalMesh* Quinn = LoadObject<USkeletalMesh>(nullptr, QuinnMeshPath);
	USkeletalMesh* Grandma = LoadObject<USkeletalMesh>(nullptr, GrandmaMeshPath);
	if (!Quinn || !Grandma)
	{
		UE_LOG(LogTemp, Error, TEXT("CookieRetarget: couldn't load meshes (Quinn %d, Grandma %d)"), Quinn != nullptr, Grandma != nullptr);
		return 1;
	}

	// Her imported physics body was generated at her 1 cm import size, where every bone is under the
	// generator's 20 cm minimum. Build one with the minimum scaled down to match (she's scaled up ~155x in game).
	{
		UPhysicsAsset* Physics = CreateAsset<UPhysicsAsset>(TEXT("PA_Grandma"));
		FPhysAssetCreateParams PhysicsParams;
		PhysicsParams.MinBoneSize = 20.f / 155.f;
		FText Error;
		if (FPhysicsAssetUtils::CreateFromSkeletalMesh(Physics, Grandma, PhysicsParams, Error, /*bSetToMesh=*/ false, /*bShowProgress=*/ false))
		{
			UE_LOG(LogTemp, Display, TEXT("CookieRetarget: PA_Grandma has %d bodies"), Physics->SkeletalBodySetups.Num());
			SavePackageOf(Physics);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("CookieRetarget: physics asset failed: %s"), *Error.ToString());
		}
	}
	if (Params.Contains(TEXT("-physicsonly")))
	{
		return 0;
	}

	UIKRigDefinition* QuinnRig = MakeRig(TEXT("IK_Quinn"), Quinn, TEXT("pelvis"), true);
	UIKRigDefinition* GrandmaRig = MakeRig(TEXT("IK_Grandma"), Grandma, TEXT("mixamorig_Hips_01"), false);

	UIKRetargeter* Retargeter = CreateAsset<UIKRetargeter>(TEXT("RTG_QuinnToGrandma"));
	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	Controller->SetIKRig(ERetargetSourceOrTarget::Source, QuinnRig);
	Controller->SetIKRig(ERetargetSourceOrTarget::Target, GrandmaRig);
	Controller->SetPreviewMesh(ERetargetSourceOrTarget::Source, Quinn);
	Controller->SetPreviewMesh(ERetargetSourceOrTarget::Target, Grandma);
	Controller->AddDefaultOps();
	Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, QuinnRig);
	Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, GrandmaRig);
	Controller->AutoMapChains(EAutoMapChainType::Exact, true);

	// She's modelled in a T-pose and Quinn in an A-pose: bend her chains to match before copying motion.
	const FName AlignedPose = Controller->CreateRetargetPose(TEXT("AlignedToQuinn"), ERetargetSourceOrTarget::Target);
	Controller->SetCurrentRetargetPose(AlignedPose, ERetargetSourceOrTarget::Target);
	Controller->AutoAlignAllBones(ERetargetSourceOrTarget::Target);

	SavePackageOf(QuinnRig);
	SavePackageOf(GrandmaRig);
	SavePackageOf(Retargeter);

	FIKRetargetBatchOperationInputs Inputs;
	for (const TCHAR* Path : AnimsToRetarget)
	{
		if (UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, Path))
		{
			Inputs.AssetsToRetarget.Add(FAssetData(Anim));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("CookieRetarget: couldn't load %s"), Path);
		}
	}
	Inputs.SourceMesh = Quinn;
	Inputs.TargetMesh = Grandma;
	Inputs.IKRetargetAsset = Retargeter;
	Inputs.Prefix = TEXT("Grandma_");
	Inputs.TargetPath = FString(OutputFolder) + TEXT("/Anims");
	Inputs.bIncludeReferencedAssets = false;
	Inputs.bOverwriteExistingFiles = true;

	const TArray<FAssetData> Results = UIKRetargetBatchOperation::RunBatchRetarget(Inputs);
	int32 Saved = 0;
	for (const FAssetData& Result : Results)
	{
		if (UObject* Asset = Result.GetAsset())
		{
			Saved += SavePackageOf(Asset) ? 1 : 0;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("CookieRetarget: retargeted %d animations, saved %d"), Results.Num(), Saved);
	return Saved > 0 ? 0 : 1;
#else
	return 1;
#endif
}
