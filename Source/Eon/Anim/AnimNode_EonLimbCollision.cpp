#include "AnimNode_EonLimbCollision.h"
#include "Animation/AnimInstanceProxy.h"
#include "TwoBoneIK.h"

FAnimNode_EonLimbCollision::FAnimNode_EonLimbCollision()
{
}

void FAnimNode_EonLimbCollision::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	SmoothedEndDelta.Init(FVector::ZeroVector, Limbs.Num());
	SmoothedJointDelta.Init(FVector::ZeroVector, Limbs.Num());
}

void FAnimNode_EonLimbCollision::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	DeltaTime = Context.GetDeltaTime();
}

void FAnimNode_EonLimbCollision::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	for (FEonLimbChain& L : Limbs)
	{
		L.Root.Initialize(RequiredBones);
		L.Joint.Initialize(RequiredBones);
		L.End.Initialize(RequiredBones);
	}
	Cache.Initialize(ColliderSet, RequiredBones, BlockingTags);
}

bool FAnimNode_EonLimbCollision::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	if (!ColliderSet || Limbs.Num() == 0) return false;
	for (FEonLimbChain& L : Limbs)
	{
		if (L.Root.IsValidToEvaluate(RequiredBones) && L.Joint.IsValidToEvaluate(RequiredBones) && L.End.IsValidToEvaluate(RequiredBones))
		{
			return true;
		}
	}
	return false;
}

static FVector SmoothCorrection(const FVector& Prev, const FVector& Target, float Dt, float ReleaseSpeed)
{
	// Entering the body snaps instantly (never allow penetration); leaving relaxes smoothly.
	if (Target.SizeSquared() >= Prev.SizeSquared()) return Target;
	return FMath::VInterpTo(Prev, Target, Dt, ReleaseSpeed);
}

void FAnimNode_EonLimbCollision::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	const FBoneContainer& BC = Output.Pose.GetPose().GetBoneContainer();

	if (SmoothedEndDelta.Num() != Limbs.Num())
	{
		SmoothedEndDelta.Init(FVector::ZeroVector, Limbs.Num());
		SmoothedJointDelta.Init(FVector::ZeroVector, Limbs.Num());
	}

	Cache.Resolve(ColliderSet, Output.Pose, Capsules);
	if (Capsules.Num() == 0) return;

	for (int32 LimbIdx = 0; LimbIdx < Limbs.Num(); ++LimbIdx)
	{
		const FEonLimbChain& L = Limbs[LimbIdx];
		if (!L.Root.IsValidToEvaluate(BC) || !L.Joint.IsValidToEvaluate(BC) || !L.End.IsValidToEvaluate(BC)) continue;

		const FCompactPoseBoneIndex RootIdx = L.Root.GetCompactPoseIndex(BC);
		const FCompactPoseBoneIndex JointIdx = L.Joint.GetCompactPoseIndex(BC);
		const FCompactPoseBoneIndex EndIdx = L.End.GetCompactPoseIndex(BC);

		FTransform RootT = Output.Pose.GetComponentSpaceTransform(RootIdx);
		FTransform JointT = Output.Pose.GetComponentSpaceTransform(JointIdx);
		FTransform EndT = Output.Pose.GetComponentSpaceTransform(EndIdx);

		const FVector OrigEnd = EndT.GetLocation();
		const FVector OrigJoint = JointT.GetLocation();

		FVector End = OrigEnd;
		FVector Joint = OrigJoint;

		for (int32 It = 0; It < Iterations; ++It)
		{
			bool bAny = false;
			for (const FEonCapsuleCS& C : Capsules)
			{
				if (L.IgnoreTags.Contains(C.Tag)) continue;

				bAny |= EonCollision::PushOut(End, L.EndRadius + SurfaceOffset, C);
				bAny |= EonCollision::PushOut(Joint, L.JointRadius + SurfaceOffset, C);

				FVector Mid = (Joint + End) * 0.5;
				const FVector MidBefore = Mid;
				if (EonCollision::PushOut(Mid, L.MidRadius + SurfaceOffset, C))
				{
					const FVector Push = Mid - MidBefore;
					Joint += Push;
					End += Push;
					bAny = true;
				}
			}
			if (!bAny) break;
		}

		SmoothedEndDelta[LimbIdx] = SmoothCorrection(SmoothedEndDelta[LimbIdx], End - OrigEnd, DeltaTime, ReleaseSpeed);
		SmoothedJointDelta[LimbIdx] = SmoothCorrection(SmoothedJointDelta[LimbIdx], Joint - OrigJoint, DeltaTime, ReleaseSpeed);

		if (SmoothedEndDelta[LimbIdx].IsNearlyZero(0.01) && SmoothedJointDelta[LimbIdx].IsNearlyZero(0.01)) continue;

		const FVector Effector = OrigEnd + SmoothedEndDelta[LimbIdx];

		// Pole: original bend direction, shifted by the elbow correction.
		const FVector Mid = (RootT.GetLocation() + OrigEnd) * 0.5;
		FVector BendDir = OrigJoint - Mid;
		if (BendDir.IsNearlyZero()) BendDir = FVector::ForwardVector;
		const FVector JointTarget = OrigJoint + BendDir.GetSafeNormal() * 20.0 + SmoothedJointDelta[LimbIdx];

		const FQuat EndRot = EndT.GetRotation();
		AnimationCore::SolveTwoBoneIK(RootT, JointT, EndT, JointTarget, Effector, false, 1.0, 1.0);
		EndT.SetRotation(EndRot);

		OutBoneTransforms.Add(FBoneTransform(RootIdx, RootT));
		OutBoneTransforms.Add(FBoneTransform(JointIdx, JointT));
		OutBoneTransforms.Add(FBoneTransform(EndIdx, EndT));
	}

	OutBoneTransforms.Sort(FCompareBoneTransformIndex());

#if ENABLE_ANIM_DEBUG
	if (bDrawDebug && Output.AnimInstanceProxy)
	{
		const FTransform CT = Output.AnimInstanceProxy->GetComponentTransform();
		for (const FEonCapsuleCS& C : Capsules)
		{
			const FVector A = CT.TransformPosition(C.A);
			const FVector B = CT.TransformPosition(C.B);
			Output.AnimInstanceProxy->AnimDrawDebugSphere(A, C.Radius, 12, FColor::Cyan);
			if (!A.Equals(B, 0.1))
			{
				Output.AnimInstanceProxy->AnimDrawDebugSphere(B, C.Radius, 12, FColor::Cyan);
				Output.AnimInstanceProxy->AnimDrawDebugLine(A, B, FColor::Cyan);
			}
		}
	}
#endif
}
