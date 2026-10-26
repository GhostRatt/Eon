#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "EonColliderSet.generated.h"

// Capsule attached to a bone. Axis runs along the bone's local axis chosen by Rotation (default local Z).
// HalfHeight 0 = sphere.
USTRUCT(BlueprintType)
struct EON_API FEonCapsuleDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capsule")
	FName Bone;

	// Group used by nodes to filter: Torso, Pelvis, Thigh, Breast, Head, Hand, Forearm...
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capsule")
	FName Tag = TEXT("Torso");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capsule")
	FVector Offset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capsule")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capsule", meta=(ClampMin="0.1"))
	float Radius = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capsule", meta=(ClampMin="0"))
	float HalfHeight = 0.f;
};

UCLASS(BlueprintType)
class EON_API UEonColliderSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colliders")
	TArray<FEonCapsuleDef> Capsules;
};

// Capsule resolved to component space for one evaluation.
struct FEonCapsuleCS
{
	FVector A = FVector::ZeroVector;
	FVector B = FVector::ZeroVector;
	float Radius = 0.f;
	FName Tag;
};

// Runtime cache of bone indices for a collider set, owned by an anim node.
struct EON_API FEonColliderCache
{
	struct FEntry
	{
		FBoneReference Bone;
		int32 DefIndex = INDEX_NONE;
	};

	TArray<FEntry> Entries;

	void Initialize(const UEonColliderSet* Set, const FBoneContainer& RequiredBones, const TArray<FName>& IncludeTags);
	void Resolve(const UEonColliderSet* Set, FCSPose<FCompactPose>& Pose, TArray<FEonCapsuleCS>& Out) const;
};

namespace EonCollision
{
	inline FVector ClosestPointOnSegment(const FVector& P, const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const double LenSq = AB.SizeSquared();
		if (LenSq < KINDA_SMALL_NUMBER) return A;
		const double T = FMath::Clamp(FVector::DotProduct(P - A, AB) / LenSq, 0.0, 1.0);
		return A + AB * T;
	}

	// Pushes a sphere (P, R) out of a capsule. Returns true if it was penetrating.
	inline bool PushOut(FVector& P, float R, const FEonCapsuleCS& C)
	{
		const FVector Closest = ClosestPointOnSegment(P, C.A, C.B);
		FVector Delta = P - Closest;
		const double MinDist = R + C.Radius;
		const double DistSq = Delta.SizeSquared();
		if (DistSq >= MinDist * MinDist) return false;

		double Dist = FMath::Sqrt(DistSq);
		if (Dist < KINDA_SMALL_NUMBER)
		{
			Delta = FVector::UpVector;
			Dist = 1.0;
		}
		P = Closest + Delta / Dist * MinDist;
		return true;
	}
}
