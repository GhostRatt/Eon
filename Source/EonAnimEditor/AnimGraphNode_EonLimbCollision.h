#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Anim/AnimNode_EonLimbCollision.h"
#include "AnimGraphNode_EonLimbCollision.generated.h"

UCLASS()
class EONANIMEDITOR_API UAnimGraphNode_EonLimbCollision : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Settings")
	FAnimNode_EonLimbCollision Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override { return FText::FromString(TEXT("Eon Limb Collision")); }
	virtual FText GetTooltipText() const override { return FText::FromString(TEXT("Keeps hands, forearms and elbows on the skin surface via two-bone IK")); }
	virtual FString GetNodeCategory() const override { return TEXT("Eon"); }

protected:
	virtual FText GetControllerDescription() const override { return FText::FromString(TEXT("Eon Limb Collision")); }
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
