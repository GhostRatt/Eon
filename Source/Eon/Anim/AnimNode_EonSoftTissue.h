#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "EonColliderSet.h"
#include "AnimNode_EonSoftTissue.generated.h"

// One soft-tissue bone (breast, glute, belly, thigh...). Simulated as a damped spring mass in world space,
// so it reacts to every movement of the body: steps, jumps, turns, breathing, impacts and touch.
USTRUCT(BlueprintType)
struct EON_API FEonSoftBone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Soft")
	FBoneReference Bone;

	// Group used for runtime support scaling (Breast, Glute, Belly...)
	UPROPERTY(EditAnywhere, Category="Soft")
	FName Group = TEXT("Breast");

	// Natural frequency in Hz (lower = heavier, slower sway)
	UPROPERTY(EditAnywhere, Category="Soft", meta=(ClampMin="0.3", ClampMax="20"))
	float Frequency = 3.0f;

	// 0 = bounces forever, 1 = no overshoot. Real tissue ~0.2-0.4
	UPROPERTY(EditAnywhere, Category="Soft", meta=(ClampMin="0", ClampMax="2"))
	float DampingRatio = 0.3f;

	// Rest sag under gravity (cm)
	UPROPERTY(EditAnywhere, Category="Soft", meta=(ClampMin="0"))
	float GravitySag = 0.6f;

	// Max displacement per local axis of the bone (cm). Lets you allow more vertical than sideways motion.
	UPROPERTY(EditAnywhere, Category="Soft")
	FVector MaxOffset = FVector(3.f, 3.f, 4.f);

	// Distance from the bone to the simulated mass point (tissue length, cm)
	UPROPERTY(EditAnywhere, Category="Soft", meta=(ClampMin="0.5"))
	float Length = 8.f;

	// Local bone axis pointing from the bone into the tissue
	UPROPERTY(EditAnywhere, Category="Soft")
	FVector LocalForward = FVector(1.f, 0.f, 0.f);

	// 0..1: how much of the motion moves the bone (translation)
	UPROPERTY(EditAnywhere, Category="Soft", meta=(ClampMin="0", ClampMax="1"))
	float TranslationScale = 0.5f;

	// 0..1: how much of the motion rotates the bone
	UPROPERTY(EditAnywhere, Category="Soft", meta=(ClampMin="0", ClampMax="1"))
	float RotationScale = 0.6f;

	// Radius of the tissue for touch pushers (cm)
	UPROPERTY(EditAnywhere, Category="Soft", meta=(ClampMin="0"))
	float TouchRadius = 6.f;
};

// Per-group runtime support: 0 = unsupported, 1 = fully supported (bra, tight shorts...).
USTRUCT(BlueprintType)
struct EON_API FEonSoftSupport
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Soft")
	FName Group = TEXT("Breast");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Soft", meta=(ClampMin="0", ClampMax="1"))
	float Support = 0.f;
};

USTRUCT(BlueprintInternalUseOnly)
struct EON_API FAnimNode_EonSoftTissue : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Soft")
	TArray<FEonSoftBone> Bones;

	// Set from the Wardrobe via AnimBP variable: bra/underwear firmness per group.
	UPROPERTY(EditAnywhere, Category="Soft", meta=(PinShownByDefault))
	TArray<FEonSoftSupport> Support;

	// Hands/forearms that press tissue away on touch
	UPROPERTY(EditAnywhere, Category="Touch")
	TObjectPtr<UEonColliderSet> ColliderSet = nullptr;

	UPROPERTY(EditAnywhere, Category="Touch")
	TArray<FName> PusherTags = { TEXT("Hand"), TEXT("Forearm") };

	// Scales all motion (e.g. lower when asleep)
	UPROPERTY(EditAnywhere, Category="Soft", meta=(PinHiddenByDefault, ClampMin="0", ClampMax="2"))
	float Intensity = 1.f;

	UPROPERTY(EditAnywhere, Category="Soft", meta=(ClampMin="30", ClampMax="480"))
	float SubstepRate = 120.f;

	// World jump larger than this (cm per frame) resets the simulation (teleports)
	UPROPERTY(EditAnywhere, Category="Soft")
	float TeleportDistance = 60.f;

	UPROPERTY(EditAnywhere, Category="Debug")
	bool bDrawDebug = false;

	FAnimNode_EonSoftTissue();

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void ResetDynamics(ETeleportType InTeleportType) override;
	virtual bool NeedsDynamicReset() const override { return true; }

protected:
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

private:
	struct FState
	{
		FVector Pos = FVector::ZeroVector;   // world-space mass point
		FVector Vel = FVector::ZeroVector;
		FVector LastRest = FVector::ZeroVector;
		bool bValid = false;
	};

	float GetSupport(FName Group) const;

	TArray<FState> States;
	FEonColliderCache PusherCache;
	TArray<FEonCapsuleCS> Pushers;
	float DeltaTime = 0.f;
	float Accumulator = 0.f;
	bool bResetPending = true;
};
