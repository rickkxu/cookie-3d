#include "CookieCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "CookieGameMode.h"
#include "CookieGameSettings.h"
#include "CookieLaunchable.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

ACookieCharacter::ACookieCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(40.f, 96.f);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	Camera->bUsePawnControlRotation = true;
}

void ACookieCharacter::CreateInputObjects()
{
	if (InputMapping)
	{
		return;
	}

	auto MakeAction = [this](EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this);
		Action->ValueType = Type;
		return Action;
	};

	MoveAction = MakeAction(EInputActionValueType::Axis2D);
	LookAction = MakeAction(EInputActionValueType::Axis2D);
	JumpAction = MakeAction(EInputActionValueType::Boolean);
	ShootAction = MakeAction(EInputActionValueType::Boolean);
	BuyAction = MakeAction(EInputActionValueType::Boolean);

	InputMapping = NewObject<UInputMappingContext>(this);

	// WASD -> 2D move vector (X = right, Y = forward). Swizzle moves a key's value from X to Y.
	InputMapping->MapKey(MoveAction, EKeys::W).Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(InputMapping));
	{
		FEnhancedActionKeyMapping& Back = InputMapping->MapKey(MoveAction, EKeys::S);
		Back.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(InputMapping));
		Back.Modifiers.Add(NewObject<UInputModifierNegate>(InputMapping));
	}
	InputMapping->MapKey(MoveAction, EKeys::A).Modifiers.Add(NewObject<UInputModifierNegate>(InputMapping));
	InputMapping->MapKey(MoveAction, EKeys::D);

	InputMapping->MapKey(LookAction, EKeys::Mouse2D);
	InputMapping->MapKey(JumpAction, EKeys::SpaceBar);
	InputMapping->MapKey(ShootAction, EKeys::LeftMouseButton);
	InputMapping->MapKey(BuyAction, EKeys::RightMouseButton);
}

void ACookieCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	CreateInputObjects();
	if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(InputMapping, 0);
		}
	}
}

void ACookieCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	CreateInputObjects();
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogTemp, Error, TEXT("CookieCharacter needs Enhanced Input (Project Settings > Input > Default Input Component Class)."));
		return;
	}

	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ACookieCharacter::Move);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ACookieCharacter::Look);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindAction(ShootAction, ETriggerEvent::Started, this, &ACookieCharacter::ShootCookie);
	Input->BindAction(BuyAction, ETriggerEvent::Started, this, &ACookieCharacter::StartBuying);
	Input->BindAction(BuyAction, ETriggerEvent::Completed, this, &ACookieCharacter::StopBuying);
}

void ACookieCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddMovementInput(GetActorForwardVector(), Axis.Y);
	AddMovementInput(GetActorRightVector(), Axis.X);
}

void ACookieCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
}

void ACookieCharacter::ShootCookie()
{
	if (ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>())
	{
		GameMode->AddCookies(1.0);
	}
	LaunchFromCamera(UCookieGameSettings::Get().Cookie);
}

void ACookieCharacter::StartBuying()
{
	bBuying = true;
	BuyHeldSeconds = 0.f;
	BuyAccumulator = 0.f;
	BuyOnce();
}

void ACookieCharacter::StopBuying()
{
	bBuying = false;
}

void ACookieCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bBuying)
	{
		return;
	}

	// The buy rate ramps from BuyStartRate to BuyMaxRate the longer right-click is held.
	const UCookieGameSettings& Settings = UCookieGameSettings::Get();
	BuyHeldSeconds += DeltaSeconds;
	const float Ramp = FMath::Clamp(BuyHeldSeconds / Settings.BuyRampSeconds, 0.f, 1.f);
	const float Rate = FMath::Lerp(Settings.BuyStartRate, Settings.BuyMaxRate, Ramp);

	BuyAccumulator += Rate * DeltaSeconds;
	int32 BoughtThisFrame = 0;
	while (BuyAccumulator >= 1.f && BoughtThisFrame < Settings.MaxBuysPerFrame)
	{
		BuyAccumulator -= 1.f;
		if (!BuyOnce())
		{
			// Out of cookies: keep holding, but don't bank up a burst for later.
			BuyAccumulator = 0.f;
			break;
		}
		++BoughtThisFrame;
	}
	BuyAccumulator = FMath::Min(BuyAccumulator, 1.f);
}

bool ACookieCharacter::BuyOnce()
{
	ACookieGameMode* GameMode = GetWorld()->GetAuthGameMode<ACookieGameMode>();
	if (!GameMode || !GameMode->TryBuy(SelectedItem))
	{
		return false;
	}
	LaunchFromCamera(UCookieGameSettings::Get().GetItem(SelectedItem).Launch);
	return true;
}

void ACookieCharacter::LaunchFromCamera(const FCookieLaunchSpec& Spec)
{
	const FVector Direction = Camera->GetForwardVector();
	const FVector Location = Camera->GetComponentLocation() + Direction * Spec.SpawnDistance;
	const FRotator Rotation(0.f, GetControlRotation().Yaw, 0.f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ACookieLaunchable* Thing = GetWorld()->SpawnActor<ACookieLaunchable>(Location, Rotation, Params))
	{
		// Look direction, plus a lift for a visible arc, plus the player's own motion.
		const FVector Velocity = Direction * Spec.LaunchSpeed
			+ FVector::UpVector * UCookieGameSettings::Get().LaunchUpBoost
			+ GetVelocity();
		Thing->Launch(Spec, Velocity);
	}
}
