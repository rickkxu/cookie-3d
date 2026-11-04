#include "CookieHUD.h"
#include "CookieHUDWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

namespace
{
	// Purchases closer together than this add up into one pop-up (holding right-click).
	constexpr float CombineSeconds = 0.6f;
}

ACookieHUD::ACookieHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	for (float& Time : LastBought)
	{
		Time = -1000.f;
	}
}

void ACookieHUD::BeginPlay()
{
	Super::BeginPlay();

	if (GEngine && GEngine->GameViewport && GetOwningPlayerController() && GetOwningPlayerController()->IsLocalController())
	{
		Widget = SNew(SCookieHUDWidget).HUD(this);
		GEngine->GameViewport->AddViewportWidgetContent(Widget.ToSharedRef(), 10);
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	if (ACookieIconStudio* Studio = GetWorld()->SpawnActor<ACookieIconStudio>(Params))
	{
		Studio->OnIconsReady.BindUObject(this, &ACookieHUD::OnIconsReady);
	}
}

void ACookieHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Widget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
	}
	Widget.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACookieHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Now += DeltaSeconds;
	for (FPopup& Popup : Popups)
	{
		Popup.Age += DeltaSeconds;
	}
	Popups.RemoveAll([](const FPopup& Popup) { return Popup.Age >= GetPopupLife(Popup.Kind); });
}

float ACookieHUD::GetPopupLife(EPopupKind Kind)
{
	switch (Kind)
	{
	case EPopupKind::Gain:  return 0.9f;
	case EPopupKind::Spend: return 1.2f;
	default:                return 0.7f;
	}
}

void ACookieHUD::NotifyPurchase(ECookieItem Item, double Cost)
{
	LastBought[static_cast<int32>(Item)] = Now;

	// Add to a pop-up that's still young (holding right-click), restarting its flight; otherwise start a new one.
	auto AddTo = [this](EPopupKind Kind, ECookieItem PopupItem, double Amount)
	{
		for (FPopup& Popup : Popups)
		{
			if (Popup.Kind == Kind && Popup.Item == PopupItem && Popup.Age < CombineSeconds)
			{
				Popup.Amount += Amount;
				Popup.Age = 0.f;
				return;
			}
		}
		FPopup& Popup = Popups.AddDefaulted_GetRef();
		Popup.Kind = Kind;
		Popup.Item = PopupItem;
		Popup.Amount = Amount;
	};
	AddTo(EPopupKind::Gain, Item, 1.0);
	// Spending pop-ups combine across items: the count only has one.
	AddTo(EPopupKind::Spend, ECookieItem::ToasterOven, Cost);
}

void ACookieHUD::NotifyShot()
{
	FPopup& Popup = Popups.AddDefaulted_GetRef();
	Popup.Kind = EPopupKind::Click;
	Popup.Amount = 1.0;
	Popup.Jitter = FMath::FRandRange(-1.f, 1.f);
}

const FCookieIcon* ACookieHUD::GetItemIcon(ECookieItem Item) const
{
	const int32 Index = static_cast<int32>(Item);
	return ItemIcons.IsValidIndex(Index) && ItemIcons[Index].Color ? &ItemIcons[Index] : nullptr;
}

void ACookieHUD::OnIconsReady(const TArray<FCookieIcon>& Items, const FCookieIcon& Cookie)
{
	ItemIcons = Items;
	CookieIcon = Cookie;
}
