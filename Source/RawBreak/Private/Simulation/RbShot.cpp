#include "Simulation/RbShot.h"

#include "rb/Physics/ParamTable.h"

#include <vector>

// Owner: UE-6a.

namespace RbShotPrivate
{
	// --- Verbatim copies of Tests/Core/Simulator/SimTestUtil.h (Bits, Mix, MixState): ROB-10 needs the SAME bytes. ---

	uint64 Bits(double X)
	{
		uint64 U = 0;
		FMemory::Memcpy(&U, &X, sizeof(U));
		return U;
	}

	// FNV-1a over 64-bit words.
	void Mix(uint64& H, uint64 V)
	{
		for (int i = 0; i < 8; ++i)
		{
			H ^= (V >> (8 * i)) & 0xFFu;
			H *= 0x100000001B3ull;
		}
	}

	void MixState(uint64& H, const rb::BallState& S)
	{
		Mix(H, Bits(S.Position.x));
		Mix(H, Bits(S.Position.y));
		Mix(H, Bits(S.Position.z));
		Mix(H, Bits(S.Velocity.x));
		Mix(H, Bits(S.Velocity.y));
		Mix(H, Bits(S.Velocity.z));
		Mix(H, Bits(S.Omega.x));
		Mix(H, Bits(S.Omega.y));
		Mix(H, Bits(S.Omega.z));
		Mix(H, static_cast<uint64>(S.State));
	}

	// --- InputHash helpers (same definition in Tools/rbsim/Main.cpp) ---

	void MixReal(uint64& H, double V) { Mix(H, Bits(V)); }
	void MixBool(uint64& H, bool V) { Mix(H, V ? 1u : 0u); }
	void MixBall(uint64& H, rb::BallId Id) { Mix(H, static_cast<uint64>(static_cast<uint8>(Id))); }

	void MixPocketSpec(uint64& H, const rb::PocketSpec& P)
	{
		MixReal(H, P.Mouth);
		MixReal(H, P.CutAngle);
		MixReal(H, P.Shelf);
		MixReal(H, P.JawRadius);
		MixReal(H, P.CaptureRadius);
	}

	void MixTableSpec(uint64& H, const rb::TableSpec& S)
	{
		Mix(H, static_cast<uint64>(S.Preset));
		MixReal(H, S.Length);
		MixReal(H, S.Width);
		MixReal(H, S.BedHeight);
		MixReal(H, S.CushionNoseHeight);
		MixReal(H, S.CushionWidth);
		MixReal(H, S.CushionNoseProfileRadius);
		MixReal(H, S.RailWidthTotal);
		MixReal(H, S.RailTopZ);
		MixReal(H, S.SlateThickness);
		MixReal(H, S.SightInset);
		MixReal(H, S.SightDiameter);
		MixPocketSpec(H, S.Corner);
		MixPocketSpec(H, S.Side);
		MixReal(H, S.Backdraft);
		MixReal(H, S.DropPointRadius);
		MixReal(H, S.FacingThickness);
		MixReal(H, S.LinerUndercut);
		MixBool(H, S.HasPockets);
		Mix(H, static_cast<uint64>(S.Cloth));
		MixReal(H, S.FacingRestitutionScale);
		MixReal(H, S.LinerRestitution);
		MixReal(H, S.LinerFriction);
	}

	void MixCueSpec(uint64& H, const rb::CueSpec& C)
	{
		MixReal(H, C.Mass);
		MixReal(H, C.EndMass);
		MixReal(H, C.TipRestitution);
		MixReal(H, C.TipFriction);
		MixReal(H, C.TipFrictionKinetic);
		MixReal(H, C.TipDomeRadius);
		MixReal(H, C.TipDiameter);
		MixReal(H, C.Length);
		MixReal(H, C.ContactTime);
		MixReal(H, C.FollowThroughDistance);
		MixBool(H, C.JumpCue);
	}

	// capacity() == size() afterwards (copy assignment keeps a larger buffer of the target; shrink_to_fit is non-binding).
	template <typename T>
	void ShrinkExact(std::vector<T>& V)
	{
		if (V.capacity() != V.size())
		{
			std::vector<T> Exact;
			Exact.reserve(V.size());
			Exact.insert(Exact.end(), V.begin(), V.end());
			V.swap(Exact);
		}
	}

	template <typename T>
	SIZE_T VectorBytes(const std::vector<T>& V)
	{
		return static_cast<SIZE_T>(V.capacity()) * sizeof(T);
	}
}

namespace RbShot
{
	uint64 ResultHash(const rb::ShotResult& R)
	{
		using namespace RbShotPrivate;
		// Verbatim SimTestUtil.h ResultHash (ROB-10).
		uint64 H = 0xCBF29CE484222325ull;
		Mix(H, static_cast<uint64>(R.Status));
		Mix(H, Bits(R.StopTime));
		for (const rb::ShotEvent& E : R.Events)
		{
			Mix(H, Bits(E.Time));
			Mix(H, static_cast<uint64>(E.Type));
			Mix(H, static_cast<uint64>(static_cast<uint8>(E.A)) | (static_cast<uint64>(static_cast<uint8>(E.B)) << 8) |
				(static_cast<uint64>(E.Feature) << 16) | (static_cast<uint64>(E.SubFeature) << 24) | (static_cast<uint64>(E.Flags) << 32));
			Mix(H, Bits(E.Normal.x));
			Mix(H, Bits(E.Normal.y));
			Mix(H, Bits(E.Normal.z));
			Mix(H, Bits(E.NormalSpeed));
			Mix(H, Bits(E.NormalImpulse));
			Mix(H, Bits(E.TangentImpulse));
			Mix(H, Bits(E.CutAngle));
			Mix(H, Bits(E.Value));
			MixState(H, E.Pre[0]);
			MixState(H, E.Pre[1]);
			MixState(H, E.Post[0]);
			MixState(H, E.Post[1]);
		}
		for (const rb::BallFinal& F : R.Finals)
		{
			Mix(H, static_cast<uint64>(F.Status));
			MixState(H, F.State);
			Mix(H, Bits(F.Time));
		}
		Mix(H, static_cast<uint64>(R.Diagnostics.EventsProcessed));
		Mix(H, static_cast<uint64>(R.Diagnostics.Predictions));
		return H;
	}

	uint64 InputHash(const rb::SimInput& In)
	{
		using namespace RbShotPrivate;
		uint64 H = 0xCBF29CE484222325ull;

		// 1. table (the geometry is BuildTableGeometry(Spec))
		if (In.Table == nullptr)
		{
			Mix(H, 0);
		}
		else
		{
			Mix(H, 1);
			MixTableSpec(H, In.Table->Spec);
		}

		// 2. environment
		MixReal(H, In.Environment.LampUndersideZ);
		MixReal(H, In.Environment.LampFootprint.Lo.x);
		MixReal(H, In.Environment.LampFootprint.Lo.y);
		MixReal(H, In.Environment.LampFootprint.Hi.x);
		MixReal(H, In.Environment.LampFootprint.Hi.y);

		// 3. params: every ParamTable key in table order
		const int ParamCount = rb::PhysicsParamCount();
		Mix(H, static_cast<uint64>(ParamCount));
		for (int i = 0; i < ParamCount; ++i)
		{
			double Value = 0.0;
			rb::GetPhysicsParam(In.Params, rb::PhysicsParamAt(i).Key, Value);
			MixReal(H, Value);
		}

		// 4. balls in play
		for (int Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			const rb::SimBall& B = In.Balls[Id];
			if (!B.InPlay)
			{
				continue;
			}
			Mix(H, static_cast<uint64>(Id));
			MixReal(H, B.Spec.Radius);
			MixReal(H, B.Spec.Mass);
			MixReal(H, B.Spec.Inertia);
			MixState(H, B.State);
			MixReal(H, B.Orientation.w);
			MixReal(H, B.Orientation.x);
			MixReal(H, B.Orientation.y);
			MixReal(H, B.Orientation.z);
			Mix(H, static_cast<uint64>(B.ChalkMarks.Size()));
			for (const rb::ChalkMark& M : B.ChalkMarks)
			{
				MixReal(H, M.BodyDir.x);
				MixReal(H, M.BodyDir.y);
				MixReal(H, M.BodyDir.z);
				MixReal(H, M.Strength);
				MixReal(H, M.Radius);
			}
		}

		// 5. strikes
		Mix(H, static_cast<uint64>(In.Strikes.Size()));
		for (const rb::StrikeRequest& S : In.Strikes)
		{
			MixBall(H, S.Ball);
			MixReal(H, S.Input.Speed);
			MixReal(H, S.Input.Elevation);
			MixReal(H, S.Input.Azimuth);
			MixReal(H, S.Input.OffsetA);
			MixReal(H, S.Input.OffsetB);
			MixReal(H, S.Input.LambdaOverride);
			MixBool(H, S.Input.SquirtEnabled);
			MixBool(H, S.Input.TipTouchesCloth);
			MixCueSpec(H, S.Input.Cue);
		}

		// 6. context
		const rb::ShotContext& C = In.Context;
		Mix(H, static_cast<uint64>(C.InHand));
		MixReal(H, C.PlacedPosition.x);
		MixReal(H, C.PlacedPosition.y);
		MixBool(H, C.TemplatePresent);
		MixReal(H, C.ShotClockElapsed);
		MixBool(H, C.FootOnFloor);
		MixReal(H, C.FrozenTolerance);
		Mix(H, static_cast<uint64>(C.NonTipContacts.Size()));
		for (const rb::NonTipContact& N : C.NonTipContacts)
		{
			MixBall(H, N.Ball);
			Mix(H, static_cast<uint64>(N.Source));
			MixReal(H, N.Time);
		}

		// 7. record options
		MixBool(H, In.Record.Trajectories);
		MixBool(H, In.Record.EventStates);
		MixBool(H, In.Record.LogTransitions);
		MixBool(H, In.Record.LogObservers);
		MixBool(H, In.Record.ShotRecord);
		return H;
	}

	void CopyCompact(const rb::ShotResult& Source, rb::ShotResult& Out)
	{
		using namespace RbShotPrivate;
		// Everything by value (fixed-size members, and any member the core adds later); copying into empty vectors
		// allocates size() already, a reused Out may keep larger buffers: those are reallocated to the exact size.
		Out = Source;
		ShrinkExact(Out.Events);
		ShrinkExact(Out.CueTips);
		for (rb::BallTrack& Track : Out.Tracks)
		{
			ShrinkExact(Track.Segments);
		}
		ShrinkExact(Out.Record.Events);
	}

	SIZE_T HeapBytes(const rb::ShotResult& Result)
	{
		using namespace RbShotPrivate;
		SIZE_T Bytes = VectorBytes(Result.Events) + VectorBytes(Result.CueTips) + VectorBytes(Result.Record.Events);
		for (const rb::BallTrack& Track : Result.Tracks)
		{
			Bytes += VectorBytes(Track.Segments);
		}
		return Bytes;
	}

	SIZE_T FootprintBytes(const FRbShot& Shot)
	{
		return sizeof(FRbShot) + HeapBytes(Shot.Result) + Shot.Request.Stroke.InputLog.GetAllocatedSize();
	}

	void InitSimInput(const FRbTableContext& Table, rb::SimInput& Out)
	{
		Out = rb::SimInput{};
		Out.Table = &Table.Geometry;
		Out.Environment = Table.Environment;
		Out.Params = Table.Physics;
		Out.Record = rb::RecordOptions{}; // trajectories + event states (playback, audio), transitions, observers, rules record
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			Out.Balls[Id].InPlay = false;
			Out.Balls[Id].Spec = Id < Table.Balls.Count ? Table.Balls.Balls[Id] : rb::BallSpec{};
		}
	}
}
