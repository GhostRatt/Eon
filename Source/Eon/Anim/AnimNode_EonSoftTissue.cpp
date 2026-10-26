#include "AnimNode_EonSoftTissue.h"
#include "Animation/AnimInstanceProxy.h"

FAnimNode_EonSoftTissue::FAnimNode_EonSoftTissue()
{
}

void FAnimNode_EonSoftTissue::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	States.SetNum(Bones.Num());
	bResetPending = true;
}

void FAnimNode_EonSoftTissue::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	DeltaTime = Context.GetDeltaTime();
}

void FAnimNode_EonSoftTissue::ResetDynamics(ETeleportType InTeleportType)
{
	bResetPending = true;
}

void FAnimNode_EonSoftTissue::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	for (FEonSoftBone& B : Bones)
	{
		B.Bone.Initialize(RequiredBones);
	}
	PusherCache.Initialize(ColliderSet, RequiredBones, PusherTags);
	States.SetNum(Bones.Num());
	bResetPending = true;
}

bool FAnimNode_EonSoftTissue::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	for (FEonSoftBone& B : Bones)
	{
		if (B.Bone.IsValidToEvaluate(RequiredBones)) return true;
	}
	return false;
}

float FAnimNode_EonSoftTissue::GetSupport(FName Group) const
{
	for (const FEonSoftSupport& S : Support)
	{
		if (S.Group == Group) return FMath::Clamp(S.Support, 0.f, 1.f);
	}
	return 0.f;
}

static void ClampLocal(FVector& Offset, FVector& Vel, const FQuat& BoneRotW, const FVector& Max)
{
	FVector L = BoneRotW.UnrotateVector(Offset);
	FVector LV = BoneRotW.UnrotateVector(Vel);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double M = FMath::Max<double>(Max[Axis], 0.01);
		if (L[Axis] > M)       { L[Axis] = M;  LV[Axis] = FMath::Min(LV[Axis], 0.0); }
		else if (L[Axis] < -M) { L[Axis] = -M; LV[Axis] = FMath::Max(LV[Axis], 0.0); }
	}
	Offset = BoneRotW.RotateVector(L);
	Vel = BoneRotW.RotateVector(LV);
}

void FAnimNode_EonSoftTissue::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	const FBoneContainer& BC = Output.Pose.GetPose().GetBoneContainer();
	const FTransform CT = Output.AnimInstanceProxy ? Output.AnimInstanceProxy->GetComponentTransform() : FTransform::Identity;

	if (States.Num() != Bones.Num())
	{
		States.SetNum(Bones.Num());
		bResetPending = true;
	}

	// Touch pushers to world space
	PusherCache.Resolve(ColliderSet, Output.Pose, Pushers);
	for (FEonCapsuleCS& P : Pushers)
	{
		P.A = CT.TransformPosition(P.A);
		P.B = CT.TransformPosition(P.B);
	}

	const float Dt = FMath::Clamp(DeltaTime, 0.f, 0.1f);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Dt * SubstepRate), 1, 12);
	const float H = Dt / Steps;
	const FVector Gravity = FVector(0, 0, -1);

	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		const FEonSoftBone& B = Bones[i];
		FState& S = States[i];
		if (!B.Bone.IsValidToEvaluate(BC)) continue;

		const FCompactPoseBoneIndex Idx = B.Bone.GetCompactPoseIndex(BC);
		const FTransform BoneCS = Output.Pose.GetComponentSpaceTransform(Idx);
		const FTransform BoneW = BoneCS * CT;

		const FVector Fwd = B.LocalForward.IsNearlyZero() ? FVector::ForwardVector : B.LocalForward.GetSafeNormal();
		const FVector RestW = BoneW.TransformPosition(Fwd * B.Length);
		const FQuat BoneRotW = BoneW.GetRotation();

		const float Sup = GetSupport(B.Group);
		const float Omega = 2.f * PI * B.Frequency * (1.f + 1.5f * Sup);
		const float Zeta = B.DampingRatio * (1.f + 0.5f * Sup);
		const float Sag = B.GravitySag * (1.f - 0.7f * Sup);
		const FVector MaxOff = B.MaxOffset * (1.f - 0.6f * Sup);
		const FVector SagW = Gravity * Sag;

		const bool bTeleported = S.bValid && FVector::DistSquared(RestW, S.LastRest) > FMath::Square(TeleportDistance);
		if (bResetPending || !S.bValid || bTeleported)
		{
			S.Pos = RestW + SagW;
			S.Vel = FVector::ZeroVector;
			S.LastRest = RestW;
			S.bValid = true;
		}

		if (Dt > 0.f)
		{
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				// Rest point moves smoothly across substeps
				const FVector Rest = FMath::Lerp(S.LastRest, RestW, float(Step + 1) / Steps);
				const FVector Target = Rest + SagW;

				const FVector Accel = (Target - S.Pos) * (Omega * Omega) - S.Vel * (2.f * Zeta * Omega);
				S.Vel += Accel * H;
				S.Pos += S.Vel * H;

				FVector Off = S.Pos - Target;
				ClampLocal(Off, S.Vel, BoneRotW, MaxOff);
				S.Pos = Target + Off;
			}

			// Touch: hands press tissue away, then limit to 1.5x the normal range
			for (const FEonCapsuleCS& P : Pushers)
			{
				FVector Pushed = S.Pos;
				if (EonCollision::PushOut(Pushed, B.TouchRadius, P))
				{
					const FVector N = (Pushed - S.Pos).GetSafeNormal();
					S.Pos = Pushed;
					const double Into = FVector::DotProduct(S.Vel, N);
					if (Into < 0.0) S.Vel -= N * Into;
				}
			}
			FVector Off = S.Pos - (RestW + SagW);
			ClampLocal(Off, S.Vel, BoneRotW, MaxOff * 1.5f);
			S.Pos = RestW + SagW + Off;
		}

		S.LastRest = RestW;

		// Apply: displacement from the animated rest point, in component space
		const FVector DispCS = CT.InverseTransformVector(S.Pos - RestW) * Intensity;
		if (DispCS.IsNearlyZero(0.001)) continue;

		FTransform NewT = BoneCS;
		NewT.AddToTranslation(DispCS * B.TranslationScale);

		const FVector DirCS = BoneCS.TransformVectorNoScale(Fwd) * B.Length;
		const FVector NewDirCS = DirCS + DispCS;
		if (!NewDirCS.IsNearlyZero())
		{
			const FQuat Delta = FQuat::FindBetweenNormals(DirCS.GetSafeNormal(), NewDirCS.GetSafeNormal());
			const FQuat Scaled = FQuat::Slerp(FQuat::Identity, Delta, B.RotationScale);
			NewT.SetRotation((Scaled * BoneCS.GetRotation()).GetNormalized());
		}

		OutBoneTransforms.Add(FBoneTransform(Idx, NewT));

#if ENABLE_ANIM_DEBUG
		if (bDrawDebug && Output.AnimInstanceProxy)
		{
			Output.AnimInstanceProxy->AnimDrawDebugSphere(S.Pos, B.TouchRadius, 10, FColor::Magenta);
			Output.AnimInstanceProxy->AnimDrawDebugLine(BoneW.GetLocation(), S.Pos, FColor::Magenta);
		}
#endif
	}

	bResetPending = false;
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}
