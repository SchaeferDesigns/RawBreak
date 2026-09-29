#include "Camera/RbHumanMotion.h"

// Owner: M2-F. Stub of the M2 architect step: a plain ease between the poses and no continuous motion; M2-F replaces it with the
// human trajectories of the header (and folds FRbHeadMotion in).

void FRbHumanMotion::BeginPostureChange(ERbPostureChange InChange, const FTransform& FromEye, const FTransform& ToEye, uint64 Seed)
{
	Change = InChange;
	ChangeFrom = FromEye;
	ChangeSeed = Seed;
	ChangeSeconds = 1.0; // TODO(M2-F): seeded, asymmetric duration incl. overshoot and settle
}

bool FRbHumanMotion::EvaluatePostureChange(double Seconds, const FTransform& ToEye, FTransform& OutEye) const
{
	// TODO(M2-F): hip-hinge arc, weight shift, head lead, overshoot + damped settle, seeded variation.
	const double Alpha = ChangeSeconds > 0.0 ? FMath::Clamp(Seconds / ChangeSeconds, 0.0, 1.0) : 1.0;
	const double Smooth = Alpha * Alpha * (3.0 - 2.0 * Alpha);
	OutEye.SetLocation(FMath::Lerp(ChangeFrom.GetLocation(), ToEye.GetLocation(), Smooth));
	OutEye.SetRotation(FQuat::Slerp(ChangeFrom.GetRotation(), ToEye.GetRotation(), Smooth));
	OutEye.SetScale3D(FVector::OneVector);
	return Alpha < 1.0;
}

FRbHumanMotionSample FRbHumanMotion::Step(const FRbHumanMotionInputs& Inputs, const FRbCameraPresetParams& /*Params*/)
{
	// TODO(M2-F): breathing, postural sway, walking bob + footsteps, tremor share, reactions (follow, flinch), stabilisation.
	Time += FMath::Max(0.0, Inputs.DeltaSeconds);
	PendingFlinch = 0.0;
	return FRbHumanMotionSample();
}

void FRbHumanMotion::NotifyImpact(double Loudness)
{
	PendingFlinch = FMath::Max(PendingFlinch, FMath::Clamp(Loudness, 0.0, 1.0));
}

void FRbHumanMotion::Reset()
{
	*this = FRbHumanMotion();
}
