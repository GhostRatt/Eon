#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "EonColliderSet.h"
#include "AnimNode_EonLimbCollision.generated.h"

// Upper arm -> forearm -> hand (or thigh -> calf -> foot).
USTRUCT(BlueprintType)
struct EON_API FEonLimbChain
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Limb")
	FBoneReference Root;

	UPROPERTY(EditAnywhere, Category="Limb")
	FBoneReference Joint;

	UPROPERTY(EditAnywhere, Category="Limb")
	FBoneReference End;

	// Sphere around the end bone (palm thickness for hands)
	UPROPERTY(EditAnywhere, Category="Limb", meta=(ClampMin="0"))
	float EndRadius = 3.5f;

	// Sphere around the joint (elbow / knee)
	UPROPERTY(EditAnywhere, Category="Limb", meta=(ClampMin="0"))
	float JointRadius = 4.f;

	// Sphere at the middle of the lower segment (forearm / shin)
	UPROPERTY(EditAnywhere, Category="Limb", meta=(ClampMin="0"))
	float MidRadius = 3.5f;

	// Capsule tags this limb ignores (e.g. its own thigh)
	UPROPERTY(EditAnywhere, Category="Limb")
	TArray<FName> IgnoreTags;
};

// Keeps hands/forearms/elbows on the skin surface instead of inside the body.
// Pushes the end and joint out of body capsules, then re-solves the limb with two-bone IK.
USTRUCT(BlueprintInternalUseOnly)
struct EON_API FAnimNode_EonLimbCollision : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Collision")
	TObjectPtr<UEonColliderSet> ColliderSet = nullptr;

	// Capsule tags that block limbs
	UPROPERTY(EditAnywhere, Category="Collision")
	TArray<FName> BlockingTags = { TEXT("Torso"), TEXT("Pelvis"), TEXT("Thigh"), TEXT("Breast"), TEXT("Glute"), TEXT("Head"), TEXT("Neck") };

	UPROPERTY(EditAnywhere, Category="Collision")
	TArray<FEonLimbChain> Limbs;

	// Gap kept between skin and limb (cm). Small negative = slight press into soft tissue.
	UPROPERTY(EditAnywhere, Category="Collision", meta=(PinHiddenByDefault))
	float SurfaceOffset = 0.2f;

	UPROPERTY(EditAnywhere, Category="Collision", meta=(ClampMin="1", ClampMax="8"))
	int32 Iterations = 3;

	// How fast corrections relax when the limb leaves the body (higher = snappier)
	UPROPERTY(EditAnywhere, Category="Collision", meta=(ClampMin="0"))
	float ReleaseSpeed = 12.f;

	UPROPERTY(EditAnywhere, Category="Debug")
	bool bDrawDebug = false;

	FAnimNode_EonLimbCollision();

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;

protected:
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

private:
	FEonColliderCache Cache;
	TArray<FEonCapsuleCS> Capsules;
	TArray<FVector> SmoothedEndDelta;
	TArray<FVector> SmoothedJointDelta;
	float DeltaTime = 0.f;
};
