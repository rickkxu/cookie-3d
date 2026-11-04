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
	// Each picture takes three passes (coverage, paint, normals); shaded, outlined and anti-aliased by hand below.
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
			// Her mesh faces +Y; turn her round to face the camera on the -Y side.
			Grandma->SetRelativeLocationAndRotation(FVector(ModelSpacing * Spot++, 0.0, 0.0), FRotator(0.0, 180.0, 0.0));
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

bool ACookieIconStudio::CapturePass(ESceneCaptureSource Source, TArray<FLinearColor>& OutPixels)
{
	Capture->CaptureSource = Source;
	Capture->CaptureScene();
	return ColorTarget->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(OutPixels) && OutPixels.Num() == RenderSize * RenderSize;
}

FCookieIcon ACookieIconStudio::Render(UPrimitiveComponent* Model, const FVector& ViewDirection, const FString& Name)
{
	// Frame the model's bounding sphere with an orthographic camera, leaving room round the edge for the outline.
	const FBoxSphereBounds Bounds = Model->Bounds;
	const double Radius = FMath::Max(Bounds.SphereRadius, 1.0);
	Capture->ShowOnlyComponents.Reset();
	Capture->ShowOnlyComponent(Model);
	Capture->OrthoWidth = static_cast<float>(Radius * 2.0 * 0.98);
	Capture->SetWorldLocationAndRotation(Bounds.Origin + ViewDirection * Radius * 4.0, (-ViewDirection).Rotation());

	// Three passes: coverage (scene colour alpha), the model's own paint (base colour), and which way each surface faces.
	TArray<FLinearColor> Scene, Paint, Normals;
	if (!CapturePass(SCS_SceneColorHDR, Scene) || !CapturePass(SCS_BaseColor, Paint) || !CapturePass(SCS_Normal, Normals))
	{
		return FCookieIcon();
	}

	// Scene colour captures carry 1 - coverage in alpha; check the corner (always empty) to be sure which way round.
	const bool bInverted = Scene[0].A > 0.5f;
	auto Coverage = [bInverted](const FLinearColor& P) { return FMath::Clamp(bInverted ? 1.f - P.A : P.A, 0.f, 1.f); };

	// Normals may come back signed (-1..1) or packed (0..1).
	bool bSignedNormals = false;
	for (int32 Index = 0; Index < Normals.Num() && !bSignedNormals; ++Index)
	{
		const FLinearColor& N = Normals[Index];
		bSignedNormals = Coverage(Scene[Index]) > 0.5f && (N.R < -0.01f || N.G < -0.01f || N.B < -0.01f);
	}

	// Our own light, fixed relative to the camera: from the upper left and a little in front, whatever the level's sun.
	const FVector Toward = ViewDirection.GetSafeNormal();
	const FVector Right = Capture->GetRightVector();
	const FVector Up = Capture->GetUpVector();
	const FVector Light = (-0.55 * Right + 0.8 * Up + 0.55 * Toward).GetSafeNormal();

	// Cartoon shading of one render pixel, in linear colour: three hard steps and a thin rim on the shadow side.
	auto Shade = [&](int32 Index) -> FLinearColor
	{
		const FLinearColor& P = Paint[Index];
		const FLinearColor& NC = Normals[Index];
		FVector N = bSignedNormals ? FVector(NC.R, NC.G, NC.B) : FVector(NC.R, NC.G, NC.B) * 2.0 - FVector(1.0);
		N = N.GetSafeNormal();
		const double Lit = FVector::DotProduct(N, Light);
		const float Step = static_cast<float>(FMath::Lerp(0.62, 0.8, FMath::SmoothStep(-0.25, -0.15, Lit))
			+ FMath::Lerp(0.0, 0.22, FMath::SmoothStep(0.3, 0.4, Lit)));
		const float Rim = Lit < 0.35
			? 0.35f * FMath::Pow(1.f - FMath::Clamp(static_cast<float>(FVector::DotProduct(N, Toward)), 0.f, 1.f), 3.f)
			: 0.f;
		return FLinearColor(P.R * Step + Rim * (P.R + 0.35f), P.G * Step + Rim * (P.G + 0.35f), P.B * Step + Rim * (P.B + 0.35f), 1.f);
	};

	// Down to the icon size (2x2 averaging for smooth edges), then punchier colour in sRGB: saturation and contrast up.
	const int32 Count = IconSize * IconSize;
	TArray<FLinearColor> Art;
	TArray<float> Alpha;
	Art.SetNumZeroed(Count);
	Alpha.SetNumZeroed(Count);
	for (int32 Y = 0; Y < IconSize; ++Y)
	{
		for (int32 X = 0; X < IconSize; ++X)
		{
			FLinearColor Sum(0.f, 0.f, 0.f, 0.f);
			float Weight = 0.f;
			for (int32 Sub = 0; Sub < 4; ++Sub)
			{
				const int32 Index = (Y * 2 + Sub / 2) * RenderSize + X * 2 + Sub % 2;
				const float A = Coverage(Scene[Index]);
				if (A > 0.f)
				{
					Sum += Shade(Index) * A;
					Weight += A;
				}
			}
			const int32 Out = Y * IconSize + X;
			Alpha[Out] = Weight / 4.f;
			if (Weight > 0.f)
			{
				const FLinearColor Linear = Sum / Weight;
				FLinearColor C(FMath::Pow(FMath::Max(Linear.R, 0.f), 1.f / 2.2f), FMath::Pow(FMath::Max(Linear.G, 0.f), 1.f / 2.2f),
					FMath::Pow(FMath::Max(Linear.B, 0.f), 1.f / 2.2f), 1.f);
				const float Luma = 0.299f * C.R + 0.587f * C.G + 0.114f * C.B;
				const float Saturation = 1.35f;
				const float Contrast = 1.1f;
				auto Punch = [&](float V) { return FMath::Clamp(((Luma + (V - Luma) * Saturation) - 0.5f) * Contrast + 0.5f, 0.f, 1.f); };
				Art[Out] = FLinearColor(Punch(C.R), Punch(C.G), Punch(C.B), 1.f);
			}
		}
	}

	// Sticker outline: the silhouette grown by OutlinePixels (drawn about 40% size on the HUD, so ~3 px on screen),
	// with a soft last pixel so its edge is anti-aliased.
	constexpr int32 OutlinePixels = 7;
	TArray<float> Outline;
	Outline.SetNumZeroed(Count);
	for (int32 Y = 0; Y < IconSize; ++Y)
	{
		for (int32 X = 0; X < IconSize; ++X)
		{
			float Grown = 0.f;
			for (int32 DY = -OutlinePixels; DY <= OutlinePixels && Grown < 1.f; ++DY)
			{
				for (int32 DX = -OutlinePixels; DX <= OutlinePixels; ++DX)
				{
					const int32 SX = X + DX;
					const int32 SY = Y + DY;
					const float Distance = FMath::Sqrt(static_cast<float>(DX * DX + DY * DY));
					if (Distance > OutlinePixels + 0.5f || SX < 0 || SY < 0 || SX >= IconSize || SY >= IconSize)
					{
						continue;
					}
					Grown = FMath::Max(Grown, Alpha[SY * IconSize + SX] * FMath::Clamp(OutlinePixels + 0.5f - Distance, 0.f, 1.f));
				}
			}
			Outline[Y * IconSize + X] = Grown;
		}
	}

	// Soft drop shadow: the outlined shape, box-blurred and shifted down.
	constexpr int32 ShadowDrop = 8;
	constexpr int32 ShadowBlur = 6;
	TArray<float> Blurred, Shadow;
	Blurred.SetNumZeroed(Count);
	Shadow.SetNumZeroed(Count);
	for (int32 Y = 0; Y < IconSize; ++Y)
	{
		for (int32 X = 0; X < IconSize; ++X)
		{
			float Sum = 0.f;
			for (int32 D = -ShadowBlur; D <= ShadowBlur; ++D)
			{
				Sum += Outline[Y * IconSize + FMath::Clamp(X + D, 0, IconSize - 1)];
			}
			Blurred[Y * IconSize + X] = Sum / (2 * ShadowBlur + 1);
		}
	}
	for (int32 Y = 0; Y < IconSize; ++Y)
	{
		for (int32 X = 0; X < IconSize; ++X)
		{
			float Sum = 0.f;
			for (int32 D = -ShadowBlur; D <= ShadowBlur; ++D)
			{
				const int32 SY = Y - ShadowDrop + D;
				Sum += (SY >= 0 && SY < IconSize) ? Blurred[SY * IconSize + X] : 0.f;
			}
			Shadow[Y * IconSize + X] = 0.35f * Sum / (2 * ShadowBlur + 1);
		}
	}

	// Stack them: shadow, then the dark-brown outline, then the picture (sRGB values, straight alpha).
	const FLinearColor OutlineColor(0.275f, 0.13f, 0.04f, 1.f);
	const FLinearColor ShadowColor(0.08f, 0.03f, 0.01f, 1.f);
	auto Over = [](const FLinearColor& Top, float TopA, const FLinearColor& Bottom, float BottomA, float& OutA)
	{
		OutA = TopA + BottomA * (1.f - TopA);
		return OutA > 0.f ? (Top * TopA + Bottom * (BottomA * (1.f - TopA))) / OutA : FLinearColor::Black;
	};
	auto ToByte = [](float V) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(V * 255.f), 0, 255)); };
	TArray<FColor> Color, Grey;
	Color.SetNumUninitialized(Count);
	Grey.SetNumUninitialized(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		float UnderAlpha = 0.f;
		float FinalAlpha = 0.f;
		const FLinearColor Under = Over(OutlineColor, Outline[Index], ShadowColor, Shadow[Index], UnderAlpha);
		const FLinearColor Final = Over(Art[Index], Alpha[Index], Under, UnderAlpha, FinalAlpha);
		const uint8 A = ToByte(FinalAlpha);
		Color[Index] = FColor(ToByte(Final.R), ToByte(Final.G), ToByte(Final.B), A);
		const uint8 Luma = ToByte((0.299f * Final.R + 0.587f * Final.G + 0.114f * Final.B) * 0.85f);
		Grey[Index] = FColor(Luma, Luma, Luma, A);
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
