#include "Audio/RbAudioPlan.h"

#include "Audio/RbAudioLive.h"
#include "Simulation/RbTableContext.h"

#include "RbAudio/RbImpactSynth.h"
#include "RbAudio/RbNoiseSynth.h"

#include "Async/ParallelFor.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

#include "rb/Physics/Playback.h"

#include <cmath>

// Owner: M2-C. The event filters and contact presets follow Tools/audio/click_synth.py events_to_impacts (ESTIMATE values of
// audio.md 3.2 / 3.5); change them there and here together.

// No using-directive at file scope (unity builds: RbAudio::C0 / Pi would hide or clash with engine names).
namespace RbAudioPlanPrivate
{
	using namespace RbAudio;

	// ESTIMATE contact presets (click_synth.py CONTACTS): contact time at 1 m/s.
	constexpr double CushionT1 = 2.5e-3;
	constexpr double FacingT1 = 1.2e-3;
	constexpr double RailTopT1 = 0.35e-3;
	constexpr double SlateT1 = 0.45e-3;
	constexpr double LinerT1 = 1.0e-3;
	constexpr double RailTopRestitution = 0.5;
	constexpr double LinerRestitution = 0.3;
	constexpr double MinNormalSpeed = 0.003;   // [m/s] quieter contacts are silent (audio.md 1.1)
	constexpr double MinSlateSpeed = 0.01;
	constexpr double DefaultTipTime = 1.0e-3;
	constexpr double RecontactTipTime = 0.8e-3;

	rb::Vec3 SafeNormal(const rb::Vec3& V, const rb::Vec3& Fallback)
	{
		const double L = rb::Length(V);
		return L > 1e-12 ? V / L : Fallback;
	}

	double ClothFactor(const FRbTableContext& Context)
	{
		switch (Context.Spec.Cloth)
		{
		case rb::ClothPreset::NappedBar: return DbToGain(3.0);   // worn bar cloth +3 dB (audio.md 2.2 AU-22)
		case rb::ClothPreset::WorstedFast: return DbToGain(-2.0); // fresh worsted -2 dB
		default: return 1.0;
		}
	}

	struct FBuildState
	{
		const rb::ShotResult& Result;
		const FRbTableContext& Context;
		const rb::Vec3 Listener;
		const FRbAudioPlanOptions& Options;
		FRbShotAudioPlan& Out;
		FReflection Cloth;
		double Ref = 0.25;
		double Floor = 0.0;
		bool bReduced = false;    // T1 / T2: order 1 only, no image
		int32 NextImpactId = 0;
		FBallKernelsPtr Kernels[rb::kMaxBalls];
		FBallKernelsPtr RigidKernels[rb::kMaxBalls];
		FBallKernelsPtr StdRigid;

		FBuildState(const rb::ShotResult& InResult, const FRbTableContext& InContext, const rb::Vec3& InListener, const FRbAudioPlanOptions& InOptions,
			FRbShotAudioPlan& InOut)
			: Result(InResult), Context(InContext), Listener(InListener), Options(InOptions), Out(InOut)
		{
		}

		double PanGain(const rb::Vec3& Point) const
		{
			return RbLouderEarPanGain(Point, Listener, Options.ListenerRightCore);
		}

		FBallKernelsPtr KernelsFor(int32 Ball, bool bRigid)
		{
			FBallKernelsPtr* Slot = bRigid ? &RigidKernels[Ball] : &Kernels[Ball];
			if (!Slot->IsValid())
			{
				*Slot = GetBallKernels(FRbAudioPlanBuilder::BallAcoustics(Context, Ball), Options.SampleRate, bRigid);
			}
			return *Slot;
		}

		rb::Vec3 BallPos(int32 Ball, double T) const
		{
			rb::BallState S;
			if (rb::StateAt(Result, Ball, T, S))
			{
				return S.Position;
			}
			return Result.Finals[Ball].State.Position;
		}

		// Voice of an emitter for the tier (T0: the named voice; T1: quadrant of the point; T2: the single voice).
		int32 MapVoice(int32 T0Voice, const rb::Vec3& Point) const
		{
			switch (Options.Tier)
			{
			case ERbTableAudioTier::T1: return (Point.x >= 0.0 ? 1 : 0) + (Point.y >= 0.0 ? 2 : 0);
			case ERbTableAudioTier::T2: return 0;
			default: return T0Voice;
			}
		}

		double Weight(int32 N, double C) const
		{
			double W = Legendre(N, C);
			if (N == 1 && Floor > 0.0 && FMath::Abs(W) < Floor)
			{
				W = W < 0.0 ? -Floor : Floor;
			}
			if (bReduced && N != 1)
			{
				W = 0.0;
			}
			return W;
		}

		// Ball radiation of one body on one voice event (direct + cloth image).
		void SetBall(FImpactEvent& E, int32 Ball, const rb::Vec3& Center, const rb::Vec3& Axis, bool bRigid)
		{
			E.Kernels = KernelsFor(Ball, bRigid);
			const double A = E.Kernels->Ball.Radius;
			const rb::Vec3 D = Listener - Center;
			const double R = FMath::Max(rb::Length(D), A + 1e-3);
			const double C = rb::Dot(D / R, Axis);
			FRadiationPath& P0 = E.Paths[0];
			for (int32 N = 0; N <= 3; ++N)
			{
				P0.Weights[N] = Weight(N, C);
			}
			P0.NearField = C0 / (R * Options.SampleRate);
			P0.Gain = 1.0 / Ref;
			P0.DelaySeconds = (R - A) / C0;
			P0.bReflected = false;
			E.NumPaths = 1;
			E.ListenerDistance = R;
			E.LouderEarGain = PanGain(Center);
			if (Center.z > 0.0 && !bReduced)
			{
				const rb::Vec3 Ci(Center.x, Center.y, -Center.z);
				const rb::Vec3 Ai(Axis.x, Axis.y, -Axis.z);
				const rb::Vec3 Di = Listener - Ci;
				const double Ri = FMath::Max(rb::Length(Di), A + 1e-3);
				const double Cc = rb::Dot(Di / Ri, Ai);
				FRadiationPath& P1 = E.Paths[1];
				for (int32 N = 0; N <= 3; ++N)
				{
					P1.Weights[N] = Weight(N, Cc);
				}
				P1.NearField = C0 / (Ri * Options.SampleRate);
				P1.Gain = R / (Ri * Ref);
				P1.DelaySeconds = (Ri - A) / C0;
				P1.bReflected = true;
				E.Reflection = Cloth;
				E.NumPaths = 2;
			}
		}

		void SetBank(FImpactEvent& E, EModalBank Bank, const rb::Vec3& Point)
		{
			E.Bank = Bank;
			const double R = FMath::Max(rb::Length(Listener - Point), 0.05);
			E.BankGain = 1.0 / Ref;
			E.BankDelaySeconds = R / C0;
			if (!E.Kernels.IsValid())
			{
				E.ListenerDistance = R;
				E.LouderEarGain = PanGain(Point);
			}
		}

		void Push(int32 Voice, FImpactEvent&& E)
		{
			if (Options.Tier == ERbTableAudioTier::T2)
			{
				E.LowPassHz = E.LowPassHz > 0.0 ? FMath::Min(E.LowPassHz, 6000.0) : 6000.0;
			}
			if (Out.Voices.IsValidIndex(Voice))
			{
				Out.Voices[Voice].Impacts.Add(MoveTemp(E));
			}
		}

		FRbAudioPlanImpact& Record(int32 SourceEvent, EImpactKind Kind, double T, double Vn, int32 A, int32 B, const rb::Vec3& Where)
		{
			FRbAudioPlanImpact& I = Out.Impacts.AddDefaulted_GetRef();
			I.ImpactId = NextImpactId++;
			I.SourceEvent = SourceEvent;
			I.Kind = Kind;
			I.ShotTime = T;
			I.ArrivalTime = T + rb::Length(Listener - Where) / C0;
			I.NormalSpeed = Vn;
			I.BallA = A;
			I.BallB = B;
			return I;
		}

		static FImpactEvent HertzEvent(EImpactKind Kind, double T, double V, double MStar, double K, double E, int32 Source, int32 Id)
		{
			FImpactEvent Ev;
			Ev.Kind = Kind;
			Ev.ShotTime = T;
			Ev.SourceEvent = Source;
			Ev.ImpactId = Id;
			Ev.Pulse = EPulseShape::Hertz;
			Ev.Shape = GetContactShape(E);
			const FContactPulse P = HertzPulse(*Ev.Shape, V, MStar, K);
			Ev.ContactTime = P.Duration;
			Ev.PeakForce = P.PeakForce;
			Ev.Impulse = P.Impulse;
			Ev.NormalSpeed = V;
			return Ev;
		}

		static FImpactEvent SineEvent(EImpactKind Kind, double T, double J, double Duration, int32 Source, int32 Id)
		{
			FImpactEvent Ev;
			Ev.Kind = Kind;
			Ev.ShotTime = T;
			Ev.SourceEvent = Source;
			Ev.ImpactId = Id;
			Ev.Pulse = EPulseShape::Sine15;
			Ev.ContactTime = Duration;
			Ev.Impulse = J;
			return Ev;
		}
	};
}

double RbLouderEarPanGain(const rb::Vec3& SourceCore, const rb::Vec3& ListenerCore, const rb::Vec3& RightCore)
{
	const double RightLength = rb::Length(RightCore);
	const rb::Vec3 D = SourceCore - ListenerCore;
	const double Length = rb::Length(D);
	if (RightLength < 1e-6 || Length < 1e-6)
	{
		return 1.0;
	}
	const double Lateral = FMath::Clamp(rb::Dot(D, RightCore) / (RightLength * Length), -1.0, 1.0); // sin of the azimuth
	const double Fraction = 0.5 + std::asin(Lateral) / UE_DOUBLE_PI;                                   // 0 hard left ... 1 hard right
	const double Angle = 0.5 * UE_DOUBLE_PI * Fraction;
	return UE_DOUBLE_SQRT_2 * FMath::Max(std::cos(Angle), std::sin(Angle));
}

int32 FRbAudioPlanBuilder::NumVoices(ERbTableAudioTier Tier)
{
	switch (Tier)
	{
	case ERbTableAudioTier::T0: return NumT0Voices;
	case ERbTableAudioTier::T1: return 4;
	case ERbTableAudioTier::T2: return 1;
	default: return 0;
	}
}

const TCHAR* FRbAudioPlanBuilder::VoiceName(ERbTableAudioTier Tier, int32 Voice)
{
	using namespace RbAudio;
	using namespace RbAudioPlanPrivate;
	static const TCHAR* T0Names[NumT0Voices] = {TEXT("Ball00"), TEXT("Ball01"), TEXT("Ball02"), TEXT("Ball03"), TEXT("Ball04"), TEXT("Ball05"),
		TEXT("Ball06"), TEXT("Ball07"), TEXT("Ball08"), TEXT("Ball09"), TEXT("Ball10"), TEXT("Ball11"), TEXT("Ball12"), TEXT("Ball13"), TEXT("Ball14"),
		TEXT("Ball15"), TEXT("RailC0"), TEXT("RailC1"), TEXT("RailC2"), TEXT("RailC3"), TEXT("RailC4"), TEXT("RailC5"), TEXT("PocketP0"), TEXT("PocketP1"),
		TEXT("PocketP2"), TEXT("PocketP3"), TEXT("PocketP4"), TEXT("PocketP5"), TEXT("Body"), TEXT("Cue")};
	static const TCHAR* T1Names[4] = {TEXT("QuadHeadRight"), TEXT("QuadFootRight"), TEXT("QuadHeadLeft"), TEXT("QuadFootLeft")};
	switch (Tier)
	{
	case ERbTableAudioTier::T0: return (Voice >= 0 && Voice < NumT0Voices) ? T0Names[Voice] : TEXT("?");
	case ERbTableAudioTier::T1: return (Voice >= 0 && Voice < 4) ? T1Names[Voice] : TEXT("?");
	case ERbTableAudioTier::T2: return TEXT("TableCentre");
	default: return TEXT("?");
	}
}

RbAudio::FBallAcoustics FRbAudioPlanBuilder::BallAcoustics(const FRbTableContext& Context, int32 BallId)
{
	using namespace RbAudio;
	using namespace RbAudioPlanPrivate;
	FBallAcoustics B;
	const bool bValid = BallId >= 0 && BallId < FMath::Min(Context.Balls.Count, rb::kMaxBalls);
	B.Radius = bValid ? Context.Balls.Balls[BallId].Radius : StdBallRadius;
	B.Mass = bValid ? Context.Balls.Balls[BallId].Mass : StdBallMass;
	B.Material = PhenolicMaterial();
	return B;
}

bool FRbAudioPlanBuilder::IsCoinOp(const FRbTableContext& Context)
{
	using namespace RbAudio;
	using namespace RbAudioPlanPrivate;
	return Context.Spec.Preset == rb::TablePreset::SevenFootBar || Context.Spec.Preset == rb::TablePreset::SevenFootTrue;
}

void FRbAudioPlanBuilder::Build(const rb::ShotResult& Result, const FRbTableContext& Context, const rb::Vec3& ListenerCore,
	const FRbAudioPlanOptions& Options, FRbShotAudioPlan& Out, bool bKeepMonoStem)
{
	using namespace RbAudio;
	using namespace RbAudioPlanPrivate;
	const uint64 StartCycles = FPlatformTime::Cycles64();
	Out = FRbShotAudioPlan();
	const int32 NumVoicesOfTier = NumVoices(Options.Tier);
	Out.Voices.SetNum(NumVoicesOfTier);
	Out.VoicePositionsCore.SetNum(NumVoicesOfTier);
	const FPresentationMode Mode = GetPresentationMode(Options.Mode);
	for (int32 V = 0; V < NumVoicesOfTier; ++V)
	{
		FVoicePlan& P = Out.Voices[V];
		P.ShotId = Options.ShotId;
		P.Seed = HashMix(Options.Seed, static_cast<uint64>(V) + 1);
		P.OutputGain = Options.PanCompensation / Mode.FullScalePa();
	}

	FShapedNoise::Prewarm(Options.SampleRate); // cached after the first call
	FBuildState S(Result, Context, ListenerCore, Options, Out);
	S.Cloth = FReflection::Cloth(Options.SampleRate);
	S.Ref = FMath::Max(0.01, Options.RefDistance);
	S.Floor = Options.DirectivityFloorDb > -200.0 ? DbToGain(Options.DirectivityFloorDb) : 0.0;
	S.bReduced = Options.Tier == ERbTableAudioTier::T1 || Options.Tier == ERbTableAudioTier::T2;

	const bool bCoinOp = IsCoinOp(Context);
	const EModalBank RailBank = bCoinOp ? EModalBank::RailBarBox : EModalBank::RailPro;
	const FBallMaterial Phenolic = PhenolicMaterial();
	const double TableLength = Context.Spec.Length;
	const rb::TableGeometry& Geo = Context.Geometry;

	// Emitter positions: the fixed ones of the tier, ball voices at their balls at t = 0.
	EmitterPositions(Context, Options.Tier, Out.VoicePositionsCore);
	auto RailPoint = [&](int32 Cushion) -> rb::Vec3
	{
		if (Cushion >= 0 && Cushion < Geo.Noses.Size())
		{
			const rb::NoseSegment& N = Geo.Noses[Cushion];
			return rb::Vec3(0.5 * (N.Start.x + N.End.x), 0.5 * (N.Start.y + N.End.y), N.Height);
		}
		return rb::Vec3(0.0, 0.0, 0.0);
	};
	const rb::Vec3 BodyPoint(0.0, 0.0, -0.10);
	auto NearestRail = [&](const rb::Vec3& P) -> int32
	{
		int32 Best = 0;
		double BestD = TNumericLimits<double>::Max();
		for (int32 C = 0; C < rb::kCushionCount; ++C)
		{
			const double D = rb::LengthSquared(rb::Planar(RailPoint(C) - P));
			if (D < BestD)
			{
				BestD = D;
				Best = C;
			}
		}
		return Best;
	};
	rb::Vec3 CuePoint = S.BallPos(rb::kCueBallId, 0.0);
	if (Options.Tier == ERbTableAudioTier::T0)
	{
		for (int32 B = 0; B < NumBallVoices; ++B)
		{
			if (B < rb::kMaxBalls && ((Result.BallsInPlay >> B) & 1u))
			{
				Out.VoicePositionsCore[B] = S.BallPos(B, 0.0);
			}
		}
	}

	// Tip contact durations per strike (TipContactBegin / End).
	TMap<int32, double> TipBegin;
	TMap<int32, double> TipDuration;
	for (const rb::ShotEvent& E : Result.Events)
	{
		if (E.Type == rb::ShotEventType::TipContactBegin && !TipBegin.Contains(E.Feature))
		{
			TipBegin.Add(E.Feature, E.Time);
		}
		else if (E.Type == rb::ShotEventType::TipContactEnd && TipBegin.Contains(E.Feature) && !TipDuration.Contains(E.Feature))
		{
			TipDuration.Add(E.Feature, E.Time - TipBegin[E.Feature]);
		}
	}

	const int32 NumEvents = static_cast<int32>(Result.Events.size());
	bool bCueSet = false;
	for (int32 Index = 0; Index < NumEvents; ++Index)
	{
		const rb::ShotEvent& E = Result.Events[Index];
		const double T = E.Time;
		const double Vn = FMath::Abs(E.NormalSpeed);
		const int32 A = E.A;
		const int32 B = E.B;
		const bool bA = A >= 0 && A < rb::kMaxBalls;
		switch (E.Type)
		{
		case rb::ShotEventType::CueStrike:
		{
			if (!bA)
			{
				break;
			}
			double Tip = TipDuration.Contains(E.Feature) ? TipDuration[E.Feature] : DefaultTipTime;
			const bool bMiscue = (E.Flags & rb::ShotEventFlags::Miscue) != 0;
			if (bMiscue)
			{
				Tip *= 0.6;
			}
			const rb::Vec3 C = S.BallPos(A, T);
			const rb::Vec3 Axis = -SafeNormal(E.Normal, rb::Vec3(1.0, 0.0, 0.0));
			const rb::Vec3 CueAt = C + Axis * 0.35 + rb::Vec3(0.0, 0.0, 0.08);
			if (!bCueSet)
			{
				CuePoint = CueAt;
				bCueSet = true;
			}
			const EImpactKind Kind = bMiscue ? EImpactKind::Miscue : EImpactKind::TipStrike;
			const FRbAudioPlanImpact& Rec = S.Record(Index, Kind, T, E.NormalSpeed, A, INDEX_NONE, C);
			FImpactEvent Ball = FBuildState::SineEvent(Kind, T, FMath::Abs(E.NormalImpulse), Tip, Index, Rec.ImpactId);
			Ball.NormalSpeed = E.NormalSpeed;
			FImpactEvent Cue = Ball;
			S.SetBall(Ball, A, C, Axis, false);
			S.Push(S.MapVoice(A, C), MoveTemp(Ball));
			S.SetBank(Cue, EModalBank::Cue, CueAt);
			S.Push(S.MapVoice(CueVoice, CueAt), MoveTemp(Cue));
			break;
		}
		case rb::ShotEventType::TipRecontact:
		{
			if (!bA || FMath::Abs(E.NormalImpulse) <= 0.0)
			{
				break;
			}
			const rb::Vec3 C = S.BallPos(A, T);
			const rb::Vec3 Axis = -SafeNormal(E.Normal, rb::Vec3(1.0, 0.0, 0.0));
			const FRbAudioPlanImpact& Rec = S.Record(Index, EImpactKind::TipRecontact, T, E.NormalSpeed, A, INDEX_NONE, C);
			FImpactEvent Ball = FBuildState::SineEvent(EImpactKind::TipRecontact, T, FMath::Abs(E.NormalImpulse), RecontactTipTime, Index, Rec.ImpactId);
			FImpactEvent Cue = Ball;
			Cue.Gain = 0.5;
			S.SetBall(Ball, A, C, Axis, false);
			S.Push(S.MapVoice(A, C), MoveTemp(Ball));
			S.SetBank(Cue, EModalBank::Cue, C + Axis * 0.35 + rb::Vec3(0.0, 0.0, 0.08));
			S.Push(S.MapVoice(CueVoice, C), MoveTemp(Cue));
			break;
		}
		case rb::ShotEventType::BallBall:
		{
			if (!bA || B < 0 || B >= rb::kMaxBalls)
			{
				break;
			}
			if ((E.Flags & rb::ShotEventFlags::Pressing) != 0 || Vn < MinNormalSpeed)
			{
				++Out.NumSkippedEvents;
				break;
			}
			const FBallAcoustics Ba = BallAcoustics(Context, A);
			const FBallAcoustics Bb = BallAcoustics(Context, B);
			const double MStar = Ba.Mass * Bb.Mass / (Ba.Mass + Bb.Mass);
			const double K = HertzStiffness(Ba.Radius, Phenolic.YoungModulus, Phenolic.Poisson, Bb.Radius, Phenolic.YoungModulus, Phenolic.Poisson);
			const rb::Vec3 Ca = S.BallPos(A, T);
			const rb::Vec3 Cb = S.BallPos(B, T);
			const rb::Vec3 N = SafeNormal(E.Normal, SafeNormal(Cb - Ca, rb::Vec3(1.0, 0.0, 0.0)));
			const FRbAudioPlanImpact& Rec = S.Record(Index, EImpactKind::BallBall, T, Vn, A, B, 0.5 * (Ca + Cb));
			FImpactEvent Ea = FBuildState::HertzEvent(EImpactKind::BallBall, T, Vn, MStar, K, BallRestitution, Index, Rec.ImpactId);
			FImpactEvent Eb = Ea;
			S.SetBall(Ea, A, Ca, N, false);
			S.SetBall(Eb, B, Cb, -N, false);
			S.Push(S.MapVoice(A, Ca), MoveTemp(Ea));
			S.Push(S.MapVoice(B, Cb), MoveTemp(Eb));
			break;
		}
		case rb::ShotEventType::BallCushion:
		case rb::ShotEventType::BallJaw:
		case rb::ShotEventType::BallRailTop:
		{
			if (!bA)
			{
				break;
			}
			if (Vn < MinNormalSpeed)
			{
				++Out.NumSkippedEvents;
				break;
			}
			const FBallAcoustics Ba = BallAcoustics(Context, A);
			EImpactKind Kind = EImpactKind::BallCushion;
			double T1 = CushionT1;
			double Restitution = CushionRestitution(Vn);
			if (E.Type == rb::ShotEventType::BallJaw)
			{
				Kind = EImpactKind::BallJaw;
				T1 = FacingT1;
			}
			else if (E.Type == rb::ShotEventType::BallRailTop)
			{
				Kind = EImpactKind::BallRailTop;
				T1 = RailTopT1;
				Restitution = RailTopRestitution;
			}
			const double K = StiffnessForContactTime(T1, Ba.Mass);
			const rb::Vec3 C = S.BallPos(A, T);
			const rb::Vec3 Axis = -SafeNormal(E.Normal, rb::Vec3(0.0, 0.0, -1.0));
			const rb::Vec3 Contact = C + Axis * Ba.Radius;
			const FRbAudioPlanImpact& Rec = S.Record(Index, Kind, T, Vn, A, INDEX_NONE, Contact);
			FImpactEvent Eb = FBuildState::HertzEvent(Kind, T, Vn, Ba.Mass, K, Restitution, Index, Rec.ImpactId);
			FImpactEvent Er = Eb;
			S.SetBall(Eb, A, C, Axis, false);
			S.Push(S.MapVoice(A, C), MoveTemp(Eb));
			int32 T0Voice;
			if (E.Type == rb::ShotEventType::BallJaw)
			{
				T0Voice = FirstPocketVoice + FMath::Clamp<int32>(E.Feature, 0, 5);
			}
			else
			{
				const int32 Cushion = E.Feature < rb::kCushionCount ? static_cast<int32>(E.Feature) : NearestRail(Contact);
				T0Voice = FirstRailVoice + Cushion;
			}
			S.SetBank(Er, RailBank, Contact);
			S.Push(S.MapVoice(T0Voice, Contact), MoveTemp(Er));
			break;
		}
		case rb::ShotEventType::BallSlate:
		{
			if (!bA)
			{
				break;
			}
			if (Vn < MinSlateSpeed)
			{
				++Out.NumSkippedEvents;
				break;
			}
			const FBallAcoustics Ba = BallAcoustics(Context, A);
			const double K = StiffnessForContactTime(SlateT1, Ba.Mass);
			rb::Vec3 C = S.BallPos(A, T);
			C.z = Ba.Radius;
			const FRbAudioPlanImpact& Rec = S.Record(Index, EImpactKind::BallSlate, T, Vn, A, INDEX_NONE, C);
			FImpactEvent Eb = FBuildState::HertzEvent(EImpactKind::BallSlate, T, Vn, Ba.Mass, K, SlateRestitution, Index, Rec.ImpactId);
			FImpactEvent Ebed = Eb;
			S.SetBall(Eb, A, C, rb::Vec3(0.0, 0.0, -1.0), false);
			S.Push(S.MapVoice(A, C), MoveTemp(Eb));
			const rb::Vec3 BedAt = C - rb::Vec3(0.0, 0.0, Ba.Radius);
			S.SetBank(Ebed, EModalBank::Bed, BedAt);
			S.Push(S.MapVoice(BodyVoice, BedAt), MoveTemp(Ebed));
			break;
		}
		case rb::ShotEventType::BallLiner:
		case rb::ShotEventType::BallPocketRim:
		{
			if (!bA)
			{
				break;
			}
			const FBallAcoustics Ba = BallAcoustics(Context, A);
			const bool bRim = E.Type == rb::ShotEventType::BallPocketRim;
			const double K = StiffnessForContactTime(LinerT1 * (bRim ? 0.5 : 1.0), Ba.Mass);
			const double V = FMath::Max(Vn, 0.05);
			const rb::Vec3 C = S.BallPos(A, T);
			const rb::Vec3 Axis = -SafeNormal(E.Normal, rb::Vec3(0.0, 0.0, 1.0));
			const FRbAudioPlanImpact& Rec = S.Record(Index, EImpactKind::BallLiner, T, V, A, INDEX_NONE, C);
			FImpactEvent Eb = FBuildState::HertzEvent(EImpactKind::BallLiner, T, V, Ba.Mass, K, LinerRestitution, Index, Rec.ImpactId);
			FImpactEvent Ep = Eb;
			S.SetBall(Eb, A, C, Axis, false);
			S.Push(S.MapVoice(A, C), MoveTemp(Eb));
			S.SetBank(Ep, EModalBank::Pocket, C);
			S.Push(S.MapVoice(FirstPocketVoice + FMath::Clamp<int32>(E.Feature, 0, 5), C), MoveTemp(Ep));
			break;
		}
		case rb::ShotEventType::BallPocketed:
		{
			if (!bA)
			{
				break;
			}
			const FBallAcoustics Ba = BallAcoustics(Context, A);
			const rb::Vec3 C = S.BallPos(A, T);
			const double VDrop = FMath::Sqrt(2.0 * Gravity * 0.08); // ESTIMATE last fall into the pocket bottom
			const FRbAudioPlanImpact& Rec = S.Record(Index, EImpactKind::PocketDrop, T, VDrop, A, INDEX_NONE, C);
			FImpactEvent Eb = FBuildState::SineEvent(EImpactKind::PocketDrop, T, Ba.Mass * 1.3 * VDrop, 3.0e-3, Index, Rec.ImpactId);
			FImpactEvent Ep = Eb;
			S.SetBall(Eb, A, C, rb::Vec3(0.0, 0.0, -1.0), true);
			const int32 PocketVoice = FirstPocketVoice + FMath::Clamp<int32>(E.Feature, 0, 5);
			S.Push(S.MapVoice(A, C), MoveTemp(Eb));
			S.SetBank(Ep, EModalBank::Pocket, C);
			S.Push(S.MapVoice(PocketVoice, C), MoveTemp(Ep));
			if (bCoinOp)
			{
				// Gully run to the trap row at the foot end (1.5 s from the foot pockets ... 3 s from the head pockets, venue-dive-bar
				// 3.3) and the click into the balls already there, muffled by the cabinet (low-pass 1.2 kHz).
				const rb::Vec3 Tray(0.5 * TableLength + 0.05, 0.0, -0.30);
				const double Run = 1.5 + 1.5 * FMath::Min(1.0, FMath::Abs(C.x - Tray.x) / TableLength);
				FContinuousSegment G;
				G.StartTime = T + 0.02;
				G.EndTime = T + Run;
				G.Kind = ENoiseKind::GullyRun;
				G.Gain = GullyRms / S.Ref;
				Out.Voices[S.MapVoice(BodyVoice, BodyPoint)].Continuous.Add(G);
				++Out.NumGullyRuns;
				FBallAcoustics Std;
				Std.Material = Phenolic;
				const double MStar = Ba.Mass * Std.Mass / (Ba.Mass + Std.Mass);
				const double K = HertzStiffness(Ba.Radius, Phenolic.YoungModulus, Phenolic.Poisson, Std.Radius, Phenolic.YoungModulus, Phenolic.Poisson);
				const rb::Vec3 Dir(1.0, 0.0, 0.0);
				const FRbAudioPlanImpact& TrayRec = S.Record(INDEX_NONE, EImpactKind::TrapClick, T + Run, 0.6, A, INDEX_NONE, Tray);
				FImpactEvent T1e = FBuildState::HertzEvent(EImpactKind::TrapClick, T + Run, 0.6, MStar, K, BallRestitution, INDEX_NONE, TrayRec.ImpactId);
				T1e.LowPassHz = 1200.0;
				FImpactEvent T2e = T1e;
				S.SetBall(T1e, A, Tray - Dir * Ba.Radius, Dir, true);
				// The ball already in the trap: a standard ball, rigid body only.
				T2e.Kernels = GetBallKernels(Std, Options.SampleRate, true);
				{
					const rb::Vec3 Cc = Tray + Dir * Std.Radius;
					const rb::Vec3 D = ListenerCore - Cc;
					const double R = FMath::Max(rb::Length(D), Std.Radius + 1e-3);
					const double Cos = rb::Dot(D / R, -Dir);
					for (int32 N = 0; N <= 3; ++N)
					{
						T2e.Paths[0].Weights[N] = S.Weight(N, Cos);
					}
					T2e.Paths[0].NearField = C0 / (R * Options.SampleRate);
					T2e.Paths[0].Gain = 1.0 / S.Ref;
					T2e.Paths[0].DelaySeconds = (R - Std.Radius) / C0;
					T2e.NumPaths = 1;
					T2e.ListenerDistance = R;
				}
				S.SetBank(T2e, EModalBank::Cabinet, Tray);
				const int32 TrayVoice = S.MapVoice(BodyVoice, Tray);
				S.Push(TrayVoice, MoveTemp(T1e));
				S.Push(TrayVoice, MoveTemp(T2e));
			}
			break;
		}
		default:
			break;
		}
	}

	// Continuous layers from the ball tracks: rolling rumble on cloth (rolling and sliding), sliding hiss (AU-22 / AU-23).
	const double Cloth = ClothFactor(Context);
	for (int32 Ball = 0; Ball < rb::kMaxBalls; ++Ball)
	{
		if (!((Result.BallsInPlay >> Ball) & 1u))
		{
			continue;
		}
		const int32 Voice = S.MapVoice(Ball, S.BallPos(Ball, 0.0));
		if (!Out.Voices.IsValidIndex(Voice))
		{
			continue;
		}
		for (const rb::TrajectorySegment& Seg : Result.Tracks[Ball].Segments)
		{
			const rb::MotionState State = Seg.Motion.State;
			if (Seg.Kind != rb::SegmentKind::Analytic || (State != rb::MotionState::Sliding && State != rb::MotionState::Rolling))
			{
				continue;
			}
			const double T0 = Seg.Motion.T0;
			const double T1 = Seg.T1;
			if (!FMath::IsFinite(T1) || T1 <= T0)
			{
				continue;
			}
			const rb::Vec3 V0 = rb::Planar(Seg.Motion.Vel0);
			const rb::Vec3 V1 = rb::Planar(Seg.Motion.Vel0 + Seg.Motion.Accel2 * (2.0 * (T1 - T0)));
			const double S0 = rb::Length(V0);
			const double S1 = rb::Length(V1);
			FContinuousSegment R;
			R.StartTime = T0;
			R.EndTime = T1;
			R.Speed0 = S0;
			R.SpeedSlope = (S1 - S0) / (T1 - T0);
			R.Kind = ENoiseKind::RollingCloth;
			R.Gain = RollingRmsPerMps * Cloth / S.Ref;
			Out.Voices[Voice].Continuous.Add(R);
			if (State == rb::MotionState::Sliding)
			{
				FContinuousSegment H = R;
				H.Kind = ENoiseKind::SlidingCloth;
				H.Gain = SlidingHissFactor * RollingRmsPerMps * Cloth / S.Ref;
				Out.Voices[Voice].Continuous.Add(H);
			}
		}
	}
	if (Options.Tier == ERbTableAudioTier::T0)
	{
		Out.VoicePositionsCore[CueVoice] = CuePoint;
	}
	for (FVoicePlan& P : Out.Voices)
	{
		P.Impacts.StableSort([](const FImpactEvent& L, const FImpactEvent& R) { return L.ShotTime < R.ShotTime; });
		P.Continuous.StableSort([](const FContinuousSegment& L, const FContinuousSegment& R) { return L.StartTime < R.StartTime; });
	}
	Out.Impacts.StableSort([](const FRbAudioPlanImpact& L, const FRbAudioPlanImpact& R) { return L.ShotTime < R.ShotTime; });

	// The reverb feed (audio.md 3.6 / 6.4): every voice's events with the ball radiation at its radiated power (direction independent).
	{
		FVoicePlan& Feed = Out.ReverbFeed;
		Feed.ShotId = Options.ShotId;
		Feed.Seed = HashMix(Options.Seed, 0xFEED'0000ull);
		Feed.OutputGain = Options.PanCompensation / Mode.FullScalePa();
		for (const FVoicePlan& P : Out.Voices)
		{
			for (const FImpactEvent& E : P.Impacts)
			{
				FImpactEvent F = E;
				if (F.Kernels.IsValid() && F.NumPaths > 0)
				{
					FRadiationPath& Path = F.Paths[0];
					for (int32 N = 0; N <= 3; ++N)
					{
						Path.Weights[N] = (S.bReduced && N != 1) ? 0.0 : PowerWeight(N);
					}
					Path.NearField = 0.0;     // far field: the near-field term carries no radiated power
					Path.bReflected = false;
					F.NumPaths = 1;           // free-field power (E_rad of 3.7): the cloth image is a listener-side effect
				}
				Feed.Impacts.Add(MoveTemp(F));
			}
			Feed.Continuous.Append(P.Continuous);
		}
		Feed.Impacts.StableSort([](const FImpactEvent& L, const FImpactEvent& R) { return L.ShotTime < R.ShotTime; });
		Feed.Continuous.StableSort([](const FContinuousSegment& L, const FContinuousSegment& R) { return L.StartTime < R.StartTime; });
	}

	// Presentation envelope of the table stem (audio.md 4.2): a mono render of every impact at the listener.
	const uint64 PresentationCycles = FPlatformTime::Cycles64();
	if (Options.bPresentation && Out.Impacts.Num() > 0)
	{
		double Last = 0.0;
		for (const FRbAudioPlanImpact& I : Out.Impacts)
		{
			Last = FMath::Max(Last, I.ArrivalTime);
		}
		const double Start = -0.01;
		const double Duration = FMath::Min(Last - Start + 0.6, 60.0);
		TArray<double> Mono;
		RenderImpactsAtListener(Out, Options.SampleRate, S.Ref, Start, Duration, Mono);
		double Peak = 0.0;
		for (double V : Mono)
		{
			Peak = FMath::Max(Peak, FMath::Abs(V));
		}
		Out.StemPeakPa = Peak;
		Out.StemPeakSpl = PaToDbSpl(FMath::Max(Peak, 1e-12));
		Out.Presentation = ComputePresentationEnvelope(Mono, Options.SampleRate, Start, Mode);
		for (FVoicePlan& P : Out.Voices)
		{
			P.Presentation = Out.Presentation;
		}
		Out.ReverbFeed.Presentation = Out.Presentation; // the room hears the presented stem (a 126 dB break must not overdrive the reverb)
		if (bKeepMonoStem)
		{
			Out.MonoStemPa = MoveTemp(Mono);
			Out.MonoStemStartTime = Start;
		}
	}
	Out.PresentationMilliseconds = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - PresentationCycles);
	Out.BuildMilliseconds = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles);
}

double FRbAudioPlanBuilder::PowerWeight(int32 N)
{
	return N >= 0 ? 1.0 / FMath::Sqrt(2.0 * N + 1.0) : 0.0;
}

bool FRbAudioPlanBuilder::CuePointCore(const rb::ShotResult& Result, rb::Vec3& OutCore)
{
	using namespace RbAudioPlanPrivate;
	for (const rb::ShotEvent& E : Result.Events)
	{
		if (E.Type != rb::ShotEventType::CueStrike || E.A < 0 || E.A >= rb::kMaxBalls)
		{
			continue;
		}
		rb::BallState State;
		const rb::Vec3 C = rb::StateAt(Result, E.A, E.Time, State) ? State.Position : Result.Finals[E.A].State.Position;
		const rb::Vec3 Axis = -SafeNormal(E.Normal, rb::Vec3(1.0, 0.0, 0.0));
		OutCore = C + Axis * 0.35 + rb::Vec3(0.0, 0.0, 0.08);
		return true;
	}
	return false;
}

void FRbAudioPlanBuilder::Prewarm(const FRbTableContext& Context, double SampleRate)
{
	using namespace RbAudio;
	using namespace RbAudioPlanPrivate;
	for (int32 Ball = 0; Ball < FMath::Min(Context.Balls.Count, rb::kMaxBalls); ++Ball)
	{
		const FBallAcoustics B = BallAcoustics(Context, Ball);
		GetBallKernels(B, SampleRate, false);
		GetBallKernels(B, SampleRate, true);
	}
	FBallAcoustics Std;
	Std.Material = PhenolicMaterial();
	GetBallKernels(Std, SampleRate, true);
	GetContactShape(BallRestitution);
	GetContactShape(SlateRestitution);
	GetContactShape(RailTopRestitution);
	GetContactShape(LinerRestitution);
	for (double V = 0.0; V <= 12.0; V += 0.25)
	{
		GetContactShape(CushionRestitution(V));
	}
	for (int32 Bank = 1; Bank < static_cast<int32>(EModalBank::Count); ++Bank)
	{
		GetModalBankDesign(static_cast<EModalBank>(Bank), SampleRate);
	}
	DecimationFir(SampleRate);
	FShapedNoise::Prewarm(SampleRate); // the voices' rolling / sliding / gully noise (never normalised on the audio thread)
	RbAudioLive::Prewarm(SampleRate);  // loose-ball floor hits: contact shapes of every floor, the standard ball
}

void FRbAudioPlanBuilder::EmitterPositions(const FRbTableContext& Context, ERbTableAudioTier Tier, TArray<rb::Vec3>& Out)
{
	using namespace RbAudio;
	using namespace RbAudioPlanPrivate;
	const int32 Count = NumVoices(Tier);
	Out.SetNum(Count);
	const rb::TableGeometry& Geo = Context.Geometry;
	const rb::Vec3 HeadSpot(Geo.Landmarks.HeadSpot.x, Geo.Landmarks.HeadSpot.y, 0.03);
	if (Tier == ERbTableAudioTier::T0)
	{
		for (int32 B = 0; B < NumBallVoices; ++B)
		{
			Out[B] = HeadSpot;
		}
		for (int32 C = 0; C < 6; ++C)
		{
			if (C < Geo.Noses.Size())
			{
				const rb::NoseSegment& N = Geo.Noses[C];
				Out[FirstRailVoice + C] = rb::Vec3(0.5 * (N.Start.x + N.End.x), 0.5 * (N.Start.y + N.End.y), N.Height);
			}
			if (C < Geo.Pockets.Size())
			{
				const rb::PocketGeometry& P = Geo.Pockets[C];
				Out[FirstPocketVoice + C] = rb::Vec3(P.CaptureCenter.x, P.CaptureCenter.y, -0.05);
			}
			else
			{
				Out[FirstPocketVoice + C] = rb::Vec3(0.0, 0.0, -0.05);
			}
		}
		Out[BodyVoice] = rb::Vec3(0.0, 0.0, -0.10);
		Out[CueVoice] = HeadSpot + rb::Vec3(-0.35, 0.0, 0.08);
	}
	else if (Tier == ERbTableAudioTier::T1)
	{
		const double Hx = 0.25 * Context.Spec.Length;
		const double Hy = 0.25 * Context.Spec.Width;
		for (int32 Q = 0; Q < 4; ++Q)
		{
			Out[Q] = rb::Vec3((Q & 1) ? Hx : -Hx, (Q & 2) ? Hy : -Hy, 0.04);
		}
	}
	else if (Count > 0)
	{
		Out[0] = rb::Vec3(0.0, 0.0, 0.0);
	}
}

void FRbAudioPlanBuilder::RenderImpactsAtListener(const FRbShotAudioPlan& Plan, double SampleRate, double RefDistance, double StartTime, double Duration,
	TArray<double>& Out)
{
	using namespace RbAudio;
	using namespace RbAudioPlanPrivate;
	const int32 N = FMath::Max(0, static_cast<int32>(Duration * SampleRate));
	Out.SetNumZeroed(N);
	// Every impact is rendered into its own segment in parallel (a break has ~160 voice events: the plan must be ready in a few
	// milliseconds, audio.md 8.3), then the segments are added in the plan's order: the same additions as a serial render into Out,
	// so the result is bit-identical and deterministic (AU-T13).
	TArray<const FImpactEvent*> Events;
	for (const FVoicePlan& Voice : Plan.Voices)
	{
		for (const FImpactEvent& E : Voice.Impacts)
		{
			Events.Add(&E);
		}
	}
	struct FSegment
	{
		int64 First = 0;
		TArray<double> Samples;
	};
	TArray<FSegment> Segments;
	Segments.SetNum(Events.Num());
	const int32 NumChunks = FMath::Clamp(FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 1, 1, 8);
	ParallelFor(FMath::Min(NumChunks, FMath::Max(1, Events.Num())), [&](int32 Chunk)
	{
		FImpactRenderer Renderer(SampleRate); // own scratch buffers per chunk
		for (int32 I = Chunk; I < Events.Num(); I += NumChunks)
		{
			const FImpactEvent& E = *Events[I];
			FImpactEvent Abs = E;
			Abs.Gain *= RefDistance / FMath::Max(E.ListenerDistance, 1e-3) * E.LouderEarGain;
			const double EventFrame = (E.ShotTime - StartTime) * SampleRate;
			int32 First = 0;
			int32 End = 0;
			Renderer.Extent(Abs, First, End);
			FSegment& Seg = Segments[I];
			Seg.First = static_cast<int64>(std::floor(EventFrame)) + First;
			const int64 Last = FMath::Min<int64>(static_cast<int64>(std::floor(EventFrame)) + End, N);
			const int64 Begin = FMath::Max<int64>(Seg.First, 0);
			if (Last <= Begin)
			{
				continue;
			}
			Seg.First = Begin;
			Seg.Samples.SetNumZeroed(static_cast<int32>(Last - Begin));
			Renderer.Render(Abs, EventFrame, Begin, TArrayView<double>(Seg.Samples));
		}
	});
	for (const FSegment& Seg : Segments)
	{
		double* Dst = Out.GetData() + Seg.First;
		const double* Src = Seg.Samples.GetData();
		for (int32 J = 0; J < Seg.Samples.Num(); ++J)
		{
			Dst[J] += Src[J];
		}
	}
}
