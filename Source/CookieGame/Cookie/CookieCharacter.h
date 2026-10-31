#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "CookieTypes.h"
#include "CookieCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;

/**
 * First-person player. Walks, looks, shoots cookies (left-click) and rapid-fire buys
 * the selected item (hold right-click).
 *
 * Input actions and key mappings are built in code instead of as editor assets, so the
 * whole control scheme is readable in one file and survives engine/template changes.
 */
UCLASS()
class COOKIEGAME_API ACookieCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACookieCharacter();

	virtual void Tick(float DeltaSeconds) override;

	ECookieItem GetSelectedItem() const { return SelectedItem; }

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

private:
	void CreateInputObjects();

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void ShootCookie();
	void StartBuying();
	void StopBuying();

	// Buys and launches one of the selected item. Returns false if the player can't afford it.
	bool BuyOnce();

	void LaunchFromCamera(const FCookieLaunchSpec& Spec);

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> InputMapping;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ShootAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BuyAction;

	// Hotbar selection arrives in build step 3. Until then right-click always buys grandmas.
	ECookieItem SelectedItem = ECookieItem::Grandma;

	bool bBuying = false;
	float BuyHeldSeconds = 0.f;
	float BuyAccumulator = 0.f;
};
