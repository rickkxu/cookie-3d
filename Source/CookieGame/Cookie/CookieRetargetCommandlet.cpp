#include "CookieRetargetCommandlet.h"

#if WITH_EDITOR
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
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
	// The Fab grandma, imported from the listing's FBX download (Mixamo rig; the importer drops the "mixamorig:" prefix).
	const TCHAR* GrandmaMeshPath = TEXT("/Game/Cookie/Grandma/Model/SK_Grandma.SK_Grandma");
	const TCHAR* GrandmaRoot = TEXT("Hips");
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
		{ TEXT("Spine"),         TEXT("spine_01"),   TEXT("spine_05"),   TEXT("Spine"),         TEXT("Spine2") },
		{ TEXT("Neck"),          TEXT("neck_01"),    TEXT("neck_02"),    TEXT("Neck"),          TEXT("Neck") },
		{ TEXT("Head"),          TEXT("head"),       TEXT("head"),       TEXT("Head"),          TEXT("Head") },
		{ TEXT("LeftClavicle"),  TEXT("clavicle_l"), TEXT("clavicle_l"), TEXT("LeftShoulder"),  TEXT("LeftShoulder") },
		{ TEXT("LeftArm"),       TEXT("upperarm_l"), TEXT("hand_l"),     TEXT("LeftArm"),       TEXT("LeftHand") },
		{ TEXT("RightClavicle"), TEXT("clavicle_r"), TEXT("clavicle_r"), TEXT("RightShoulder"), TEXT("RightShoulder") },
		{ TEXT("RightArm"),      TEXT("upperarm_r"), TEXT("hand_r"),     TEXT("RightArm"),      TEXT("RightHand") },
		{ TEXT("LeftLeg"),       TEXT("thigh_l"),    TEXT("ball_l"),     TEXT("LeftUpLeg"),     TEXT("LeftToeBase") },
		{ TEXT("RightLeg"),      TEXT("thigh_r"),    TEXT("ball_r"),     TEXT("RightUpLeg"),    TEXT("RightToeBase") },
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

	UIKRigDefinition* QuinnRig = MakeRig(TEXT("IK_Quinn"), Quinn, TEXT("pelvis"), true);
	UIKRigDefinition* GrandmaRig = MakeRig(TEXT("IK_Grandma"), Grandma, GrandmaRoot, false);

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
