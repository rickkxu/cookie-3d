#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CookieTypes.h"
#include "CookieIconStudio.generated.h"

class UPrimitiveComponent;
class USceneCaptureComponent2D;
class UTexture2D;
class UTextureRenderTarget2D;

// One rendered picture, in full colour and greyed out (for items you can't afford).
USTRUCT()
struct FCookieIcon
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Color;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Grey;
};

/**
 * Renders the HUD's pictures from the game's real models once, when play starts: each item (the grandma posed in
 * her idle) and the cookie, on a transparent background. It works high above the neighbourhood, showing only one
 * model at a time, exposes each picture by hand, then hands the pictures over and destroys itself.
 */
UCLASS()
class COOKIEGAME_API ACookieIconStudio : public AActor
{
	GENERATED_BODY()

public:
	ACookieIconStudio();

	DECLARE_DELEGATE_TwoParams(FOnIconsReady, const TArray<FCookieIcon>& /*Items, by ECookieItem*/, const FCookieIcon& /*Cookie*/);
	FOnIconsReady OnIconsReady;

	virtual void BeginPlay() override;

private:
	void RenderAll();

	// Frames Model from ViewDirection (pointing from the model to the camera) and renders it on its own.
	FCookieIcon Render(UPrimitiveComponent* Model, const FVector& ViewDirection, const FString& Name);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneCaptureComponent2D> Capture;

	// One per item (by ECookieItem), then the cookie.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> Models;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ColorTarget;

	FTimerHandle RenderTimer;

	// Rendered at twice the icon size and averaged down, for smooth edges.
	static constexpr int32 IconSize = 256;
	static constexpr int32 RenderSize = IconSize * 2;
};
