#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Anim/AnimNode_EonSoftTissue.h"
#include "AnimGraphNode_EonSoftTissue.generated.h"

UCLASS()
class EONANIMEDITOR_API UAnimGraphNode_EonSoftTissue : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Settings")
	FAnimNode_EonSoftTissue Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override { return FText::FromString(TEXT("Eon Soft Tissue")); }
	virtual FText GetTooltipText() const override { return FText::FromString(TEXT("Spring-damper soft tissue (breast, glute, belly) with support and touch")); }
	virtual FString GetNodeCategory() const override { return TEXT("Eon"); }

protected:
	virtual FText GetControllerDescription() const override { return FText::FromString(TEXT("Eon Soft Tissue")); }
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
