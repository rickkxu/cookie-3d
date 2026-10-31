#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CookieRetargetCommandlet.generated.h"

/**
 * Editor-only setup step: retargets the template mannequin's idle and fall animations onto the grandma model.
 * Maps her Mixamo-rig bones to the mannequin's explicitly instead of relying on the editor's auto-detection,
 * so it's repeatable and easy to adjust for a different model.
 *
 * Creates, under /Game/Cookie/Grandma: IK_Quinn, IK_Grandma, RTG_QuinnToGrandma, and Anims/Grandma_MM_*.
 * Run with the editor closed:
 *   UnrealEditor-Cmd.exe CookieGame.uproject -run=CookieRetarget
 */
UCLASS()
class UCookieRetargetCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCookieRetargetCommandlet();

	virtual int32 Main(const FString& Params) override;
};
