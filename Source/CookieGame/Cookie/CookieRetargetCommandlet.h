#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CookieRetargetCommandlet.generated.h"

/**
 * Editor-only setup step: retargets the template mannequin's idle and fall animations onto the grandma model.
 * Her Mixamo-style bones have numbered suffixes ("mixamorig_Hips_01") that the editor's auto-retargeter
 * doesn't recognise, so this maps them explicitly.
 *
 * Creates, under /Game/Cookie/Grandma: PA_Grandma (a ragdoll body sized for her), IK_Quinn, IK_Grandma,
 * RTG_QuinnToGrandma, and Anims/Grandma_MM_*.
 * Run with the editor closed:
 *   UnrealEditor-Cmd.exe CookieGame.uproject -run=CookieRetarget [-physicsonly]
 */
UCLASS()
class UCookieRetargetCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCookieRetargetCommandlet();

	virtual int32 Main(const FString& Params) override;
};
