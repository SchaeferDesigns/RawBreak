#include "Simulation/RbShotDebugDraw.h"

#include "RawBreak.h"
#include "Core/RbCoords.h"
#include "Game/RbTableSubsystem.h"
#include "Simulation/RbSimScenarios.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

#include "rb/Physics/Playback.h"

#include <vector>

// Owner: UE-6a.

namespace RbShotDebugDrawPrivate
{
	constexpr uint8 kDepthPriority = SDPG_Foreground; // on top of the scene: a diagnostic view, not a look
	constexpr float kTableThickness = 0.35f;          // line thickness [world cm]
	constexpr float kPathThickness = 0.45f;
	constexpr double kSampleDt = 0.005;               // path sampling [s] (plus every segment boundary)

	struct FDrawContext
	{
		const UWorld* World = nullptr;
		FTransform TableToWorld;
		float LifeTime = -1.0f;

		bool Persistent() const { return LifeTime < 0.0f; }

		FVector ToWorld(const rb::Vec3& P) const { return TableToWorld.TransformPosition(FRbCoords::PositionToUE(P)); }

		void Line(const rb::Vec3& A, const rb::Vec3& B, const FColor& Color, float Thickness) const
		{
			DrawDebugLine(World, ToWorld(A), ToWorld(B), Color, Persistent(), LifeTime, kDepthPriority, Thickness);
		}

		// Circle of Radius [m] about C in the plane z = C.z of the table.
		void Circle(const rb::Vec3& C, double Radius, const FColor& Color, float Thickness, int32 Segments = 24) const
		{
			rb::Vec3 Prev = {C.x + Radius, C.y, C.z};
			for (int32 i = 1; i <= Segments; ++i)
			{
				const double A = 2.0 * UE_DOUBLE_PI * static_cast<double>(i) / static_cast<double>(Segments);
				const rb::Vec3 Next = {C.x + Radius * FMath::Cos(A), C.y + Radius * FMath::Sin(A), C.z};
				Line(Prev, Next, Color, Thickness);
				Prev = Next;
			}
		}

		void Cross(const rb::Vec3& C, double Half, const FColor& Color, float Thickness) const
		{
			Line({C.x - Half, C.y - Half, C.z}, {C.x + Half, C.y + Half, C.z}, Color, Thickness);
			Line({C.x - Half, C.y + Half, C.z}, {C.x + Half, C.y - Half, C.z}, Color, Thickness);
		}
	};

	bool IsRemoved(rb::BallFinalStatus Status) { return Status == rb::BallFinalStatus::Pocketed || Status == rb::BallFinalStatus::OffTable; }
}

namespace RbShotDebugDraw
{
	FColor BallColor(int32 Id)
	{
		static const FColor Colors[16] = {
			FColor(255, 255, 255), // cue ball
			FColor(255, 200, 0),   // 1 yellow
			FColor(40, 80, 255),   // 2 blue
			FColor(230, 30, 30),   // 3 red
			FColor(150, 60, 200),  // 4 purple
			FColor(255, 120, 0),   // 5 orange
			FColor(20, 170, 70),   // 6 green
			FColor(150, 30, 40),   // 7 maroon
			FColor(90, 90, 90),    // 8 black (dark grey on black)
			FColor(255, 235, 130), // 9 yellow stripe
			FColor(120, 160, 255), // 10 blue stripe
			FColor(255, 120, 120), // 11 red stripe
			FColor(200, 140, 240), // 12 purple stripe
			FColor(255, 180, 100), // 13 orange stripe
			FColor(110, 220, 140), // 14 green stripe
			FColor(200, 100, 110), // 15 maroon stripe
		};
		return Id >= 0 && Id < 16 ? Colors[Id] : FColor(0, 255, 255);
	}

	void DrawTable(const UWorld* World, const FTransform& TableToWorld, const rb::TableGeometry& G, float LifeTime)
	{
		using namespace RbShotDebugDrawPrivate;
		if (World == nullptr)
		{
			return;
		}
		const FDrawContext D{World, TableToWorld, LifeTime};
		const FColor Cushion(0, 200, 120);
		const FColor Rail(110, 80, 50);
		const FColor Marks(160, 160, 160);

		// Cushion nose outline incl. jaws and facings (closed polyline, the last point joins the first).
		std::vector<rb::Vec2> Outline(4096);
		const int32 N = rb::BuildNoseOutline(G, 16, Outline.data(), static_cast<int>(Outline.size()));
		for (int32 i = 0; i < N; ++i)
		{
			const rb::Vec2& A = Outline[static_cast<size_t>(i)];
			const rb::Vec2& B = Outline[static_cast<size_t>((i + 1) % N)];
			D.Line({A.x, A.y, 0.0}, {B.x, B.y, 0.0}, Cushion, kTableThickness);
		}

		// Outer rail boundary.
		const rb::Aabb2& O = G.OuterBoundary;
		const rb::Vec3 Corners[4] = {{O.Lo.x, O.Lo.y, 0.0}, {O.Hi.x, O.Lo.y, 0.0}, {O.Hi.x, O.Hi.y, 0.0}, {O.Lo.x, O.Hi.y, 0.0}};
		for (int32 i = 0; i < 4; ++i)
		{
			D.Line(Corners[i], Corners[(i + 1) % 4], Rail, kTableThickness);
		}

		// Pocket holes (liner cylinder r_p about the capture centre).
		for (const rb::PocketGeometry& P : G.Pockets)
		{
			D.Circle({P.CaptureCenter.x, P.CaptureCenter.y, 0.0}, P.CaptureRadius, Cushion, kTableThickness, 32);
		}

		// Sights, head string, head / foot spots.
		for (const rb::Sight& S : G.Sights)
		{
			D.Circle({S.Position.x, S.Position.y, 0.0}, 0.006, Marks, kTableThickness, 8);
		}
		const rb::TableLandmarks& L = G.Landmarks;
		const double HalfW = 0.5 * G.Spec.Width;
		D.Line({L.HeadStringX, -HalfW, 0.0}, {L.HeadStringX, HalfW, 0.0}, Marks, 0.15f);
		D.Cross({L.HeadSpot.x, L.HeadSpot.y, 0.0}, 0.01, Marks, kTableThickness);
		D.Cross({L.FootSpot.x, L.FootSpot.y, 0.0}, 0.01, Marks, kTableThickness);
	}

	void DrawShot(const UWorld* World, const FTransform& TableToWorld, const FRbShot& Shot, double UntilTime, float LifeTime)
	{
		using namespace RbShotDebugDrawPrivate;
		if (World == nullptr)
		{
			return;
		}
		const FDrawContext D{World, TableToWorld, LifeTime};
		const rb::ShotResult& R = Shot.Result;
		const rb::SimInput& In = Shot.Request.Input;
		const double End = FMath::Min(UntilTime, R.StopTime);
		std::vector<rb::TrajectorySample> Samples(1 << 15);

		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			const rb::SimBall& Ball = In.Balls[Id];
			if (!Ball.InPlay)
			{
				continue;
			}
			const FColor Color = BallColor(Id);
			const double Radius = Ball.Spec.Radius;
			D.Circle({Ball.State.Position.x, Ball.State.Position.y, 0.0}, Radius, FColor(110, 110, 110), 0.2f);

			const int32 Count = rb::SampleTrajectory(R, Id, kSampleDt, Samples.data(), static_cast<int>(Samples.size()));
			for (int32 k = 1; k < Count && Samples[static_cast<size_t>(k - 1)].Time < End; ++k)
			{
				const rb::TrajectorySample& A = Samples[static_cast<size_t>(k - 1)];
				rb::Vec3 B = Samples[static_cast<size_t>(k)].Position;
				if (Samples[static_cast<size_t>(k)].Time > End)
				{
					rb::BallState AtEnd;
					rb::StateAt(R, Id, End, AtEnd);
					B = AtEnd.Position;
				}
				D.Line(A.Position, B, Color, kPathThickness);
			}

			// Where the ball is at the end time: a circle on the table, a cross where a removed ball left.
			const rb::BallFinal& Final = R.Finals[Id];
			if (IsRemoved(Final.Status) && Final.Time <= End)
			{
				D.Cross(Final.State.Position, Radius, Color, kPathThickness);
			}
			else
			{
				rb::BallState AtEnd;
				if (rb::StateAt(R, Id, End, AtEnd))
				{
					D.Circle(AtEnd.Position, Radius, Color, kPathThickness);
				}
			}
		}

		// The cue of each strike at contact: tip dome centre and the stick behind it along the stroke direction.
		for (int32 s = 0; s < In.Strikes.Size(); ++s)
		{
			rb::Vec3 Tip;
			rb::Vec3 Dir;
			if (rb::CueTipAt(R, s, 0.0, Tip, Dir))
			{
				const double Length = In.Strikes[s].Input.Cue.Length;
				D.Line(Tip, Tip - Dir * Length, FColor(200, 170, 120), 0.8f);
				D.Circle(Tip, In.Strikes[s].Input.Cue.TipDomeRadius, FColor(80, 160, 255), 0.3f, 12);
			}
		}
	}
}

#if !UE_BUILD_SHIPPING
namespace RbShotDebugDrawPrivate
{
	void RunSimDemo(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr)
		{
			UE_LOG(LogRawBreak, Error, TEXT("rb.SimDemo: no world"));
			return;
		}
		URbSimulationSubsystem* Service = World->GetSubsystem<URbSimulationSubsystem>();
		if (Service == nullptr)
		{
			UE_LOG(LogRawBreak, Error, TEXT("rb.SimDemo: no URbSimulationSubsystem in %s"), *World->GetName());
			return;
		}
		const FString Name = Args.Num() > 0 ? Args[0] : FString(TEXT("break9"));
		const double Until = Args.Num() > 1 ? FCString::Atod(*Args[1]) : TNumericLimits<double>::Max();

		FRbShotRequest Request;
		FString Error;
		if (!RbSimScenarios::MakeByName(Name, Request, Error))
		{
			UE_LOG(LogRawBreak, Error, TEXT("rb.SimDemo: %s"), *Error);
			return;
		}

		// The table frame: the player's table's cloth origin (URbTableSubsystem), else the world origin raised to the bed height.
		FTransform TableToWorld(FVector(0.0, 0.0, FRbCoords::CmPerMeter * Request.Table->BedHeight()));
		if (const URbTableSubsystem* Tables = URbTableSubsystem::Get(World))
		{
			if (const ARbTable* PlayerTable = Tables->GetPlayerTable())
			{
				TableToWorld = PlayerTable->GetTableToWorld();
			}
		}

		const TSharedRef<uint32> ShotId = MakeShared<uint32>(0u);
		const TSharedRef<FDelegateHandle> Handle = MakeShared<FDelegateHandle>();
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		const TWeakObjectPtr<URbSimulationSubsystem> WeakService(Service);
		*Handle = Service->OnShotSimulated.AddLambda(
			[ShotId, Handle, WeakWorld, WeakService, TableToWorld, Until, Name](const TSharedRef<const FRbShot>& Shot)
			{
				if (Shot->Id != *ShotId)
				{
					return;
				}
				if (UWorld* W = WeakWorld.Get())
				{
					RbShotDebugDraw::DrawTable(W, TableToWorld, Shot->Request.Table->Geometry);
					RbShotDebugDraw::DrawShot(W, TableToWorld, *Shot, Until);
				}
				FString Removed;
				for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
				{
					const rb::BallFinal& F = Shot->Result.Finals[Id];
					if (IsRemoved(F.Status))
					{
						Removed += FString::Printf(TEXT(" %d(%s%d, %.2f s)"), Id, F.Status == rb::BallFinalStatus::Pocketed ? TEXT("P") : TEXT("off "),
							F.Status == rb::BallFinalStatus::Pocketed ? static_cast<int32>(F.Pocket) : static_cast<int32>(F.OffReason), F.Time);
					}
				}
				UE_LOG(LogRawBreak, Display, TEXT("rb.SimDemo %s: shot %u drawn - sim %.3f ms %s, hand-off %s, %d events, stop %.3f s, removed:%s, input %016llx, result %016llx"),
					*Name, Shot->Id, Shot->SimMilliseconds, Shot->bSimulatedOnWorker ? TEXT("on a worker") : TEXT("in place"),
					Shot->HandOffFrame == Shot->SubmitFrame ? TEXT("in the submit frame") : TEXT("late"), Shot->Result.Diagnostics.EventsProcessed,
					Shot->Result.StopTime, Removed.IsEmpty() ? TEXT(" none") : *Removed, Shot->InputHash, Shot->ResultHash);
				// Last statement: removing the binding destroys this lambda and its captures.
				if (URbSimulationSubsystem* S = WeakService.Get())
				{
					const FDelegateHandle Self = *Handle;
					S->OnShotSimulated.Remove(Self);
				}
			});
		// Submit at the start of the world's next tick, i.e. inside the world tick before the tick groups - where the pawn
		// submits at the tip contact (TG_PrePhysics) - so the demo exercises the same-frame hand-off of the real engine
		// loop. (A console command runs outside the world tick; submitting here directly would always hand off a frame later.)
		const TSharedRef<FRbShotRequest> Pending = MakeShared<FRbShotRequest>(MoveTemp(Request));
		const TSharedRef<FDelegateHandle> TickHandle = MakeShared<FDelegateHandle>();
		*TickHandle = FWorldDelegates::OnWorldTickStart.AddLambda(
			[Pending, TickHandle, ShotId, Handle, WeakWorld, WeakService, Name](UWorld* TickWorld, ELevelTick, float)
			{
				if (WeakWorld.IsValid() && TickWorld != WeakWorld.Get())
				{
					return;
				}
				// Removing the binding destroys this lambda and its captures: keep what is used afterwards alive first.
				const TSharedRef<FRbShotRequest> Request = Pending;
				const TSharedRef<uint32> Id = ShotId;
				const FDelegateHandle Simulated = *Handle;
				const FString DemoName = Name;
				URbSimulationSubsystem* S = WeakService.Get();
				const bool bWorldAlive = WeakWorld.IsValid();
				const FDelegateHandle Self = *TickHandle;
				FWorldDelegates::OnWorldTickStart.Remove(Self);
				if (S == nullptr || !bWorldAlive)
				{
					return;
				}
				*Id = S->SubmitShot(MoveTemp(*Request));
				if (*Id == 0)
				{
					S->OnShotSimulated.Remove(Simulated);
					UE_LOG(LogRawBreak, Error, TEXT("rb.SimDemo: the simulation service refused the shot (busy?)"));
					return;
				}
				UE_LOG(LogRawBreak, Display, TEXT("rb.SimDemo %s: shot %u submitted at the start of the world tick of frame %llu"), *DemoName, *Id, GFrameCounter);
			});
	}

	FAutoConsoleCommandWithWorldAndArgs GRbSimDemoCommand(TEXT("rb.SimDemo"),
		TEXT("rb.SimDemo [break9|twoball] [UntilSeconds]: simulate a scripted shot through the world's simulation service (worker) and draw it"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSimDemo));
}
#endif
