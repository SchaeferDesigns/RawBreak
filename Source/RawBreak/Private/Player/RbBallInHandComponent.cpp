#include "Player/RbBallInHandComponent.h"

#include "Table/RbTable.h"

// Owner: M2-F. Stub of the M2 architect step (state only, nothing shown yet).

URbBallInHandComponent::URbBallInHandComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // TODO(M2-F): tick the hand / ball pose
}

void URbBallInHandComponent::BeginCarry(ARbTable* InTable, int32 InBallId, double InBallRadius, TFunction<bool(const rb::Vec2&)> InIsLegal)
{
	Table = InTable;
	BallId = InBallId;
	BallRadius = InBallRadius;
	IsLegal = MoveTemp(InIsLegal);
	State = ERbBallInHandState::Carrying;
}

void URbBallInHandComponent::SetTargetCore(const rb::Vec2& Plan)
{
	Target = Plan;
	if (State == ERbBallInHandState::Refused)
	{
		State = ERbBallInHandState::Carrying;
	}
}

void URbBallInHandComponent::AddFineAdjustCm(const FVector2D& /*DeltaCm*/)
{
	// TODO(M2-F)
}

bool URbBallInHandComponent::RequestSetDown()
{
	if (State != ERbBallInHandState::Carrying)
	{
		return false;
	}
	if (IsLegal && !IsLegal(Target))
	{
		State = ERbBallInHandState::Refused;
		return false;
	}
	// TODO(M2-F): the lowering animation; OnSetDown when the ball touches the cloth.
	State = ERbBallInHandState::Placed;
	OnSetDown.Broadcast(Target);
	return true;
}

void URbBallInHandComponent::Cancel()
{
	State = ERbBallInHandState::Inactive;
}

FVector URbBallInHandComponent::GetBallWorld() const
{
	const ARbTable* T = Table.Get();
	if (!T || !T->HasContext())
	{
		return GetComponentLocation();
	}
	return T->CoreToWorld(rb::Vec3(Target.x, Target.y, BallRadius + 0.01 * HoverHeightCm));
}
