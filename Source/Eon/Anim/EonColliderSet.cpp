#include "EonColliderSet.h"

void FEonColliderCache::Initialize(const UEonColliderSet* Set, const FBoneContainer& RequiredBones, const TArray<FName>& IncludeTags)
{
	Entries.Reset();
	if (!Set) return;

	for (int32 i = 0; i < Set->Capsules.Num(); ++i)
	{
		const FEonCapsuleDef& Def = Set->Capsules[i];
		if (IncludeTags.Num() > 0 && !IncludeTags.Contains(Def.Tag)) continue;

		FEntry E;
		E.Bone = FBoneReference(Def.Bone);
		E.Bone.Initialize(RequiredBones);
		E.DefIndex = i;
		if (E.Bone.IsValidToEvaluate(RequiredBones))
		{
			Entries.Add(E);
		}
	}
}

void FEonColliderCache::Resolve(const UEonColliderSet* Set, FCSPose<FCompactPose>& Pose, TArray<FEonCapsuleCS>& Out) const
{
	Out.Reset();
	if (!Set) return;

	const FBoneContainer& BC = Pose.GetPose().GetBoneContainer();
	for (const FEntry& E : Entries)
	{
		if (!Set->Capsules.IsValidIndex(E.DefIndex)) continue;
		const FEonCapsuleDef& Def = Set->Capsules[E.DefIndex];

		const FCompactPoseBoneIndex Idx = E.Bone.GetCompactPoseIndex(BC);
		if (!Idx.IsValid()) continue;

		const FTransform BoneCS = Pose.GetComponentSpaceTransform(Idx);
		const FVector Axis = Def.Rotation.RotateVector(FVector::UpVector) * Def.HalfHeight;

		FEonCapsuleCS C;
		C.A = BoneCS.TransformPosition(Def.Offset - Axis);
		C.B = BoneCS.TransformPosition(Def.Offset + Axis);
		C.Radius = Def.Radius * BoneCS.GetScale3D().GetAbsMax();
		C.Tag = Def.Tag;
		Out.Add(C);
	}
}
