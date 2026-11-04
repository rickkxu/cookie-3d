#include "CookieIconStudio.h"
#include "Animation/AnimSequence.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CookieGameSettings.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "TimerManager.h"

namespace
{
	// High above everything, so nothing else is in the pictures or casts shadows on them.
	const FVector StudioLocation(0.0, 0.0, 150000.0);

	// Models stand this far apart, each in its own spot.
	constexpr double ModelSpacing = 8000.0;

	// The cookie sits on the counter as the game's own Mesh_KitchenObjects_Cookie_01.
	const TCHAR* CookieMeshPath = TEXT("/Game/LowPolyInterior/Meshes/KitchenObjects/Mesh_KitchenObjects_Cookie_01.Mesh_KitchenObjects_Cookie_01");

	// Simple filmic curve (Hable-free Reinhard variant) and sRGB encoding for the hand-exposed HDR capture.
	float ToneMap(float Linear)
	{
		const float X = Linear * 1.15f;
		return X * (1.f + X / 6.f) / (1.f + X);
	}
}

ACookieIconStudio::ACookieIconStudio()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	// Linear colour with coverage in alpha; exposed, tone-mapped and anti-aliased by hand below.
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetCloud(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetTemporalAA(false);
	Capture->ShowFlags.SetAntiAliasing(false);
}

void ACookieIconStudio::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(StudioLocation);

	ColorTarget = NewObject<UTextureRenderTarget2D>(this);
	ColorTarget->RenderTargetFormat = RTF_RGBA16f;
	ColorTarget->ClearColor = FLinearColor::Transparent;
	ColorTarget->InitAutoFormat(RenderSize, RenderSize);
	ColorTarget->UpdateResourceImmediate(true);
	Capture->TextureTarget = ColorTarget;

	// Every model gets its own component and spot, set up now so they've all been drawn once before rendering.
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	int32 Spot = 0;
	auto AddStatic = [this, &Spot](UStaticMesh* Mesh, const FVector& Scale, const FRotator& Rotation) -> UPrimitiveComponent*
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetStaticMesh(Mesh);
		Component->SetupAttachment(RootComponent);
		Component->SetRelativeLocationAndRotation(FVector(ModelSpacing * Spot++, 0.0, 0.0), Rotation);
		Component->SetRelativeScale3D(Scale);
		Component->RegisterComponent();
		// Full-resolution textures: the kit colours its models from a small palette texture, which blurs to grey at low mips.
		Component->SetTextureForceResidentFlag(true);
		return Component;
	};

	for (ECookieItem Item : TEnumRange<ECookieItem>())
	{
		const FCookieItemSpec& Spec = Settings.GetItem(Item);
		UPrimitiveComponent* Component = nullptr;
		if (Item == ECookieItem::Grandma && !Settings.GrandmaMesh.IsNull())
		{
			USkeletalMeshComponent* Grandma = NewObject<USkeletalMeshComponent>(this);
			Grandma->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Grandma->SetSkeletalMesh(Settings.GrandmaMesh.LoadSynchronous());
			Grandma->SetupAttachment(RootComponent);
			Grandma->SetRelativeLocation(FVector(ModelSpacing * Spot++, 0.0, 0.0));
			Grandma->SetRelativeScale3D(FVector(Settings.GrandmaScale));
			Grandma->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Grandma->RegisterComponent();
			Grandma->SetTextureForceResidentFlag(true);
			if (UAnimSequence* Idle = Settings.GrandmaIdleAnim.LoadSynchronous())
			{
				Grandma->PlayAnimation(Idle, true);
			}
			Component = Grandma;
		}
		else
		{
			Component = AddStatic(Spec.Launch.Mesh.LoadSynchronous(), Spec.Launch.Scale, FRotator::ZeroRotator);
		}
		Models.Add(Component);
	}
	Models.Add(AddStatic(LoadObject<UStaticMesh>(nullptr, CookieMeshPath), FVector(1.0), FRotator::ZeroRotator));

	// A moment for the grandma to strike her pose, every model to reach the renderer and its textures to stream in.
	GetWorldTimerManager().SetTimer(RenderTimer, this, &ACookieIconStudio::RenderAll, 1.5f, false);
}

void ACookieIconStudio::RenderAll()
{
	// Which way the sun shines (the brightest directional light), so pictures are taken from the sunny side.
	FVector SunTravel(0.0, 0.0, -1.0);
	float Brightest = -1.f;
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		const ULightComponent* Light = It->GetLightComponent();
		if (Light && Light->IsVisible() && Light->Intensity > Brightest)
		{
			Brightest = Light->Intensity;
			SunTravel = Light->GetDirection();
		}
	}
	// Front-right or front-left (fronts face -Y), whichever the sun lights, and a little above.
	const FVector FrontRight(0.55, -1.0, 0.42);
	const FVector FrontLeft(-0.55, -1.0, 0.42);
	const FVector ItemView = FVector::DotProduct(FrontRight, -SunTravel) >= FVector::DotProduct(FrontLeft, -SunTravel) ? FrontRight : FrontLeft;

	TArray<FCookieIcon> Items;
	FCookieIcon CookieIcon;
	for (int32 Index = 0; Index < Models.Num(); ++Index)
	{
		UPrimitiveComponent* Model = Models[Index];
		const bool bCookie = Index == Models.Num() - 1;
		// The cookie from almost straight above, chips up.
		const FVector View = bCookie ? FVector(0.15, -0.45, 1.0) : ItemView;
		const FCookieIcon Icon = Model ? Render(Model, View.GetSafeNormal(), bCookie ? TEXT("Cookie") : *FString::Printf(TEXT("Item%d"), Index)) : FCookieIcon();
		if (bCookie)
		{
			CookieIcon = Icon;
		}
		else
		{
			Items.Add(Icon);
		}
	}

	OnIconsReady.ExecuteIfBound(Items, CookieIcon);
	Destroy();
}

FCookieIcon ACookieIconStudio::Render(UPrimitiveComponent* Model, const FVector& ViewDirection, const FString& Name)
{
	// Frame the model's bounding sphere with an orthographic camera.
	const FBoxSphereBounds Bounds = Model->Bounds;
	const double Radius = FMath::Max(Bounds.SphereRadius, 1.0);
	Capture->ShowOnlyComponents.Reset();
	Capture->ShowOnlyComponent(Model);
	Capture->OrthoWidth = static_cast<float>(Radius * 2.0 * 0.86);
	Capture->SetWorldLocationAndRotation(Bounds.Origin + ViewDirection * Radius * 4.0, (-ViewDirection).Rotation());
	Capture->CaptureScene();

	TArray<FLinearColor> Pixels;
	if (!ColorTarget->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels) || Pixels.Num() != RenderSize * RenderSize)
	{
		return FCookieIcon();
	}

	// Scene colour captures carry 1 - coverage in alpha; check the corner (always empty) to be sure which way round.
	const bool bInverted = Pixels[0].A > 0.5f;
	auto Coverage = [bInverted](const FLinearColor& P) { return FMath::Clamp(bInverted ? 1.f - P.A : P.A, 0.f, 1.f); };

	// Expose by hand: put the model's average brightness at a pleasant mid-tone, whatever the level's lighting units.
	double LuminanceSum = 0.0;
	double Covered = 0.0;
	for (const FLinearColor& P : Pixels)
	{
		const float A = Coverage(P);
		LuminanceSum += A * P.GetLuminance();
		Covered += A;
	}
	const double MeanLuminance = Covered > 0.0 ? LuminanceSum / Covered : 1.0;
	const float Exposure = static_cast<float>(0.5 / FMath::Max(MeanLuminance, 1e-6));

	// Average 2x2 blocks down to the icon size (smooth edges), tone-map and encode as sRGB with straight alpha.
	TArray<FColor> Color, Grey;
	Color.SetNumUninitialized(IconSize * IconSize);
	Grey.SetNumUninitialized(IconSize * IconSize);
	for (int32 Y = 0; Y < IconSize; ++Y)
	{
		for (int32 X = 0; X < IconSize; ++X)
		{
			FLinearColor Sum(0.f, 0.f, 0.f, 0.f);
			float AlphaSum = 0.f;
			for (int32 Sub = 0; Sub < 4; ++Sub)
			{
				const FLinearColor& P = Pixels[(Y * 2 + Sub / 2) * RenderSize + X * 2 + Sub % 2];
				const float A = Coverage(P);
				Sum += FLinearColor(P.R, P.G, P.B, 0.f) * A;
				AlphaSum += A;
			}
			const float Alpha = AlphaSum / 4.f;
			const FLinearColor Linear = AlphaSum > 0.f ? Sum / AlphaSum : FLinearColor::Black;
			const FLinearColor Mapped(ToneMap(Linear.R * Exposure), ToneMap(Linear.G * Exposure), ToneMap(Linear.B * Exposure), Alpha);
			Color[Y * IconSize + X] = Mapped.ToFColorSRGB();
			const float Luma = 0.3f * Mapped.R + 0.59f * Mapped.G + 0.11f * Mapped.B;
			Grey[Y * IconSize + X] = FLinearColor(Luma * 0.8f, Luma * 0.8f, Luma * 0.8f, Alpha).ToFColorSRGB();
		}
	}

	auto MakeTexture = [this](const TArray<FColor>& Data, const FString& TextureName) -> UTexture2D*
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(IconSize, IconSize, PF_B8G8R8A8, FName(*TextureName));
		if (!Texture)
		{
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->Filter = TF_Trilinear;
		void* Mip = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Mip, Data.GetData(), Data.Num() * sizeof(FColor));
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	};

	FCookieIcon Icon;
	Icon.Color = MakeTexture(Color, TEXT("HUDIcon_") + Name);
	Icon.Grey = MakeTexture(Grey, TEXT("HUDIconGrey_") + Name);
	return Icon;
}
