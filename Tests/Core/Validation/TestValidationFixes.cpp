// Owner: WP-10 (validation & benchmarks). Regression tests of the WP-10 fixes (architecture 17.12):
//  * A-VAL-2: the shots that broke VAL ROB-11 on the full prior-art 7.5 sets before WP-10 (B1 shot 9217: a ball passing behind the
//    facing's back end into the undercut; 8-ball breaks 217 and 807: contacts found on the whole-arc pivot proxy, resolved with the
//    balls up to 52 um inside each other) and the two shots that broke intermediate versions of the back-end edge (B1 346: a ball
//    entering the edge from above the facing top; B1 1833: the rail-top junction), plus the shots of the fixes found by the WP-10
//    scans beyond those sets (see the test), run clean: status Ok, no overlap or missed-event diagnostic, no final overlap.
//  * A-VAL-3: tip re-contact friction (MOT B.5 with the relative velocity, the WP-1 doubt of the WP-6a review): on the cone edge
//    the tangential impulse follows the SLIP of the tip over the ball's contact point, not the stroke direction. For a ball at
//    rest both coincide (B.5 unchanged); for a moving ball running sideways into the tip they are opposite. Every re-contact is
//    dissipative and leaves the pair separating along the normal.
//  * A-VAL-4: the facing's bottom edge over the pocket's hole (found by the WP-10 scan on TABLE_9FT_TIGHT).
//  * A-VAL-5 (WP-10 review): a table-feature seed whose ball is off the cloth over a pocket's opening becomes a rigid off-cloth
//    island member (SimIsland.cpp StartIslandImpl), so the island holds it instead of flying it out at the first step.

#include "Validation/ValidationUtil.h"

#include "rb/Core/Random.h"
#include "rb/Equipment/Cue.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Detect.h"

using namespace rb;
using simtest::kR;

RB_TEST(Integ_ARCH_VAL2_Rob11RegressionShots)
{
	const TableGeometry& T = simtest::NineFoot();
	SimInput& In = simtest::InputSlot();
	ShotResult& R = simtest::ResultSlot();
	Simulator Sim;
	struct Shot
	{
		const char* Name;
		int Kind; // 1 = B1 shot, 8 / 9 = 8-ball / 9-ball B2 break (the generators of Integ_VAL_ROB11_Slow_FullBenchmarkSets)
		std::uint64_t Seed;
	};
	// ROB-11's own sets (B1 seed 5000 + n, 8-ball breaks 90000 + n) and, from the WP-10 scans beyond them: PERF-05's B1 set (seed
	// 745323: a pressing rail-top contact inside a side pocket's cut rim ran into the event cap, SimStatus::Aborted), a 20 000-break
	// scan (9-ball seed 2007612: two balls rattling in a side pocket, a pocket ball-ball contact handed to one-step islands 592 times,
	// 750 overlaps) and a 300 000-shot scan (B1 2098296: a ball in the hole above the facing end's top point; B1 2188204: a ball
	// meeting the end line above h from in front of the facing's end).
	const Shot Shots[9] = {{"B1 9217", 1, 5000u + 9217u}, {"B2-8 217", 8, 90000u + 217u}, {"B2-8 807", 8, 90000u + 807u}, {"B1 346", 1, 5000u + 346u},
		{"B1 1833", 1, 5000u + 1833u}, {"B1 745323", 1, 745323u}, {"B2-9 2007612", 9, 2007612u}, {"B1 2098296", 1, 2098296u}, {"B1 2188204", 1, 2188204u}};
	for (const Shot& S : Shots)
	{
		if (S.Kind == 1)
		{
			e2e::MakeB1Shot(S.Seed, T, In);
		}
		else
		{
			e2e::MakeB2Break(S.Seed, T, In, S.Kind == 8);
		}
		const SimStatus Status = Sim.Run(In, R);
		const e2e::ShotInvariants I = e2e::CheckInvariants(R, In);
		std::printf("  A-VAL-2 %-13s status %d, overlaps %d, missed %d, final overlap %.3g m, events %d\n", S.Name, static_cast<int>(Status),
			R.Diagnostics.OverlapWarnings, R.Diagnostics.MissedEvents, I.Overlap, R.Diagnostics.EventsProcessed);
		RB_CHECK(Status == SimStatus::Ok);
		RB_CHECK(R.Diagnostics.OverlapWarnings == 0);
		RB_CHECK(R.Diagnostics.MissedEvents == 0);
		RB_CHECK(I.Overlap <= 1e-9);
		RB_CHECK(I.AtRest);
	}
}

namespace
{
	const ClothParams kCloth{0.2, 0.010, 10.0};
	constexpr double kG = 9.81;

	CueTipPath MakePath(const CueSpec& Cue, double Speed)
	{
		CueTipPath Path;
		Path.Strike = 0;
		Path.StruckBall = 0;
		Path.Start = {-kR - Cue.TipDomeRadius, 0.0, kR};
		Path.Direction = {1.0, 0.0, 0.0};
		Path.Speed0 = Speed;
		Path.Deceleration = 2.0;
		Path.StartTime = 0.0;
		Path.StopTime = Speed / 2.0;
		Path.DomeRadius = Cue.TipDomeRadius;
		return Path;
	}

	Vec3 ContactVelocity(const BallState& S, const Vec3& Normal) { return S.Velocity + Cross(S.Omega, Normal * (-kR)); }

	Vec3 Tangential(const Vec3& V, const Vec3& Normal) { return V - Normal * Dot(V, Normal); }
}

RB_TEST(ARCH_VAL3_RecontactFrictionFollowsTheSlip)
{
	const CueSpec Cue = kCuePlaying19oz;
	const BallSpec Spec = MakeBallSpec(kR, kDefaultBallMass);
	const SlateParams Slate{};
	const NumericsConfig Numerics{};
	const double Reach = kR + Cue.TipDomeRadius;
	const CueTipPath Path = MakePath(Cue, 1.2);
	const double T = 0.05;
	const double TipSpeed = 1.2 - 2.0 * T;
	const Vec3 TipCenter = Path.Start + Path.Direction * (1.2 * T - T * T);

	// (a) Ball at rest, touched outside the friction cone (sin psi 0.8 > rho_max 0.514): the tangential impulse follows the stroke's
	// tangential direction t_hat = d - (d . n) n (B.5 unchanged).
	const double Wide = 0.8 * Reach;
	BallState Rest;
	Rest.Position = {TipCenter.x + Sqrt(Reach * Reach - Wide * Wide), Wide, kR};
	Rest.State = MotionState::Stationary;
	const Vec3 N = Normalized(Rest.Position - TipCenter);
	const Vec3 StrokeT = Normalized(Tangential(Path.Direction, N));
	const TipRecontactResult A = ResolveTipRecontact(Path, T, Rest, Spec, Cue, kCloth, Slate, kG, Numerics);
	RB_REQUIRE(A.Impulse > 0.0);
	const Vec3 DvA = Tangential(A.Ball.Velocity - Rest.Velocity, N);
	RB_CHECK(Length(DvA) > 0.0);
	RB_CHECK_NEAR(Dot(Normalized(DvA), StrokeT), 1.0, 1e-12);

	// (b) The same contact point on a ball running sideways into the tip (v = -2.4 m/s along y, rolling): the tip's slip over the
	// contact point is AGAINST t_hat, so kinetic friction pushes the contact point along -t_hat; the stroke-direction formula
	// pushed it along +t_hat, i.e. with the slip and adding to it.
	BallState Moving = Rest;
	Moving.Velocity = {0.0, -2.4, 0.0};
	Moving.Omega = RollingOmegaH(Moving.Velocity, kR);
	Moving.State = MotionState::Rolling;
	const Vec3 Slip = Tangential(Path.Direction * TipSpeed - ContactVelocity(Moving, N), N);
	RB_REQUIRE(Dot(Slip, StrokeT) < 0.0);
	const TipRecontactResult B = ResolveTipRecontact(Path, T, Moving, Spec, Cue, kCloth, Slate, kG, Numerics);
	RB_REQUIRE(B.Impulse > 0.0);
	const Vec3 DcB = Tangential(ContactVelocity(B.Ball, N) - ContactVelocity(Moving, N), N);
	RB_CHECK(Dot(DcB, Slip) > 0.0);
	RB_CHECK(Dot(DcB, StrokeT) < 0.0);

	// (c) Random plane re-contacts (tip and ball centres at z = R; random ball velocity and spin, so the slip - and with it the
	// friction - may point up or down): whenever an impulse acts, the kinetic energy of ball + tip does not grow (e_tip <= 1), and
	// on the cone edge with slip the contact point's tangential velocity changes toward the tip's (along the slip). The pair
	// separates along the normal after the loop's resolution: ResolveTipRecontact separates it exactly, but a downward friction
	// component is then taken by the slate (ApplyTableReaction: bounce and slate friction), which can leave a residual approach;
	// the loop finds the contact again at the same instant and resolves it with the ball off the slate - at most one more round
	// (checked here the same way). A cue pushed backwards stops (Speed0 0) and leaves the event system (CueStrike.h).
	Rng Random(0xA7A13u);
	const double Limit = MiscueLimit(Cue.TipFriction);
	int Impulses = 0;
	int EdgeCases = 0;
	int EnergyRises = 0;
	int Approaching = 0;
	int SecondRound = 0;
	int Stopped = 0;
	int WithTheSlip = 0;
	const auto Residual = [](const TipRecontactResult& H, const Vec3& Normal)
	{ return Dot(H.Tip.Direction * H.Tip.Speed0 - ContactVelocity(H.Ball, Normal), Normal); };
	for (int k = 0; k < 20000; ++k)
	{
		const double Psi = Random.NextUniform(-0.97, 0.97) * 0.5 * kPi;
		const Vec3 Dir{Cos(Psi), Sin(Psi), 0.0};
		BallState Ball;
		Ball.Position = TipCenter + Dir * Reach;
		const double Speed = Random.NextUniform(0.0, 3.0);
		const double Heading = Random.NextUniform(-kPi, kPi);
		Ball.Velocity = {Speed * Cos(Heading), Speed * Sin(Heading), 0.0};
		Ball.Omega = {Random.NextUniform(-100.0, 100.0), Random.NextUniform(-100.0, 100.0), Random.NextUniform(-100.0, 100.0)};
		Ball.State = MotionState::Sliding;
		const TipRecontactResult H = ResolveTipRecontact(Path, T, Ball, Spec, Cue, kCloth, Slate, kG, Numerics);
		if (!(H.Impulse > 0.0))
		{
			continue;
		}
		++Impulses;
		const double Before = MechanicalEnergy(Ball, Spec, kG) + 0.5 * Cue.Mass * TipSpeed * TipSpeed;
		const double After = MechanicalEnergy(H.Ball, Spec, kG) + 0.5 * Cue.Mass * H.Tip.Speed0 * H.Tip.Speed0;
		EnergyRises += After > Before * (1.0 + 1e-12) ? 1 : 0;
		const Vec3 SlipK = Tangential(Path.Direction * TipSpeed - ContactVelocity(Ball, Dir), Dir);
		const bool Edge = !(Dot(Path.Direction, Dir) > 0.0 && Length(Tangential(Path.Direction, Dir)) <= Limit);
		if (Edge && Length(SlipK) > 1e-6)
		{
			++EdgeCases;
			const Vec3 Dc = Tangential(ContactVelocity(H.Ball, Dir) - ContactVelocity(Ball, Dir), Dir);
			WithTheSlip += Dot(Dc, SlipK) > 0.0 ? 1 : 0;
		}
		if (!(H.Tip.Speed0 > 0.0))
		{
			++Stopped;
			continue;
		}
		TipRecontactResult Final = H;
		if (Residual(H, Dir) > 1e-12)
		{
			++SecondRound;
			Final = ResolveTipRecontact(H.Tip, T, H.Ball, Spec, Cue, kCloth, Slate, kG, Numerics);
			const double Again = MechanicalEnergy(Final.Ball, Spec, kG) + 0.5 * Cue.Mass * Final.Tip.Speed0 * Final.Tip.Speed0;
			EnergyRises += Again > After * (1.0 + 1e-12) ? 1 : 0;
		}
		Approaching += Final.Tip.Speed0 > 0.0 && Residual(Final, Dir) > 1e-12 ? 1 : 0;
	}
	std::printf("  A-VAL-3: %d re-contacts with impulse (%d stop the cue, %d need a second round after the slate reaction), %d on the cone edge with slip; "
				"energy rises %d, still approaching %d, edge impulses along the slip %d/%d\n",
		Impulses, Stopped, SecondRound, EdgeCases, EnergyRises, Approaching, WithTheSlip, EdgeCases);
	RB_CHECK(Impulses > 1000 && EdgeCases > 100);
	RB_CHECK(EnergyRises == 0);
	RB_CHECK(Approaching == 0);
	RB_CHECK(WithTheSlip == EdgeCases);
}

// A-VAL-4: the facing's bottom edge (Detect.h PredictFacingBottomEdge, WP-10). Over the pocket's hole the undercut face ends in the
// air at z = 0; a ball in the hole moving up and toward it touches that edge at distance R with its center below the face (the
// in-plane up direction of the face points away from it), resolved from the nearest point of the line. A ball on the shelf in front
// of the face meets the face (PredictFacingOnShelf), never the bottom edge; the edge's corner with the back-end edge is closed (a
// ball below the corner, beyond the facing's end, meets the end edge's bottom point instead).
RB_TEST(Integ_ARCH_VAL4_FacingBottomEdge)
{
	const TableGeometry& T = simtest::Table(kTableNineFootTight);
	const Facing& Face = T.Facings[5];
	const Vec3 Up{Face.PocketNormal.x * Sin(Face.Backdraft), Face.PocketNormal.y * Sin(Face.Backdraft), Cos(Face.Backdraft)};
	const Vec3 Along = ToVec3(Face.Direction);
	const Vec3 Front = ToVec3(Face.PocketNormal);
	const Vec3 Bottom = FacingBottomEdgeStart(Face);
	const NumericsConfig N{};
	const auto Falling = [](const Vec3& P, const Vec3& V)
	{
		MotionSegment S;
		S.State = MotionState::PocketFall;
		S.Radius = kR;
		S.Pos0 = P;
		S.Vel0 = V;
		S.Accel2 = {0.0, 0.0, -0.5 * 9.81};
		S.TauEnd = kInfinity;
		return S;
	};
	// (a) Below the edge at 80 % of its length, 1 cm in front of it, rising and moving toward the face.
	const Vec3 Start = Bottom + Along * (0.8 * Face.Length) + Front * 0.01 - Vec3::UnitZ() * 0.045;
	const MotionSegment Rising = Falling(Start, Vec3::UnitZ() * 1.2 - Front * 0.3);
	const ContactPrediction Hit = PredictFacingBottomEdge(Rising, kR, Face, kInfinity, N);
	RB_REQUIRE(Hit.Found);
	const Vec3 P = PositionAt(Rising, Hit.Time - Rising.T0);
	const Vec3 OnLine = Bottom + Along * Clamp(Dot(P - Bottom, Along), 0.0, Face.Length);
	RB_CHECK_NEAR(Length(P - OnLine), kR, 1e-9);
	RB_CHECK(Dot(P - OnLine, Up) < 0.0);
	BallState B;
	B.Position = P;
	B.State = MotionState::PocketFall;
	const FixedContact C = MakeFixedContact({TableFeatureKind::FacingTopEdge, 5, kFacingBottomEdge}, T, B, MakeBallSpec(kR, kDefaultBallMass), DetectOptions{});
	RB_CHECK_NEAR(Dot(C.Normal, (P - OnLine) / kR), 1.0, 1e-12);
	// The dispatcher finds it too (the ball falls in the facing's pocket).
	BallTableContext Ctx;
	Ctx.Pocket = Face.Pocket;
	const FeaturePrediction D = PredictTableEvent(Rising, MakeBallSpec(kR, kDefaultBallMass), Ctx, T, EnvironmentSpec{}, DetectOptions{}, 9.81, kInfinity, N);
	RB_CHECK(D.Contact.Found && D.Contact.Time <= Hit.Time);
	// (b) On the shelf in front of the face, rolling into it: the face, not the bottom edge.
	const Vec3 Shelf = ToVec3(Face.Start + Face.Direction * (0.3 * Face.Length) + Face.PocketNormal * 0.05, kR);
	MotionSegment Rolling;
	Rolling.State = MotionState::Rolling;
	Rolling.Radius = kR;
	Rolling.Pos0 = Shelf;
	Rolling.Vel0 = -Front * 0.5;
	Rolling.TauEnd = 1.0;
	RB_CHECK(!PredictFacingBottomEdge(Rolling, kR, Face, kInfinity, N).Found);
	RB_CHECK(PredictFacingOnShelf(Rolling, kR, Face, FacingContactOffset(kR, Face.TopHeight, Face.Backdraft), kInfinity, N).Found);
	// (c) Below the corner, beyond the facing's end: the end edge's bottom point, not the bottom line.
	const FacingEndEdge E = MakeFacingEndEdge(Face, FacingEndEdgeTop(T, Face));
	const MotionSegment Corner = Falling(E.Lower + Along * 0.012 + Front * 0.004 - Vec3::UnitZ() * 0.04, Vec3::UnitZ() * 1.0);
	const ContactPrediction AtLine = PredictFacingBottomEdge(Corner, kR, Face, kInfinity, N);
	const ContactPrediction AtEnd = PredictFacingEndEdge(Corner, kR, Face, FacingEndEdgeTop(T, Face), kInfinity, N);
	RB_CHECK(AtEnd.Found);
	RB_CHECK(!AtLine.Found || AtLine.Time >= AtEnd.Time);
	std::printf("  A-VAL-4: bottom edge hit after %.4f s at distance R (%.3g m off), dispatcher %.4f s; corner: end edge at %.4f s\n", Hit.Time,
		Length(P - OnLine) - kR, D.Contact.Time, AtEnd.Time);
}

// A-VAL-4b (WP-10 review, adversarial): the facing's back-end and bottom edges against brute force. Random ballistic segments
// (gravity, 0.1 - 3 m/s, from anywhere within a few centimetres of the edges, never starting inside) around every facing of
// three presets (9-ft pro, 9-ft tight: the bottom edge over the hole, 7-ft true: sloped cushion tops, the upper segment ends at
// WallTopZ); each is sampled every 5 us for 0.1 s. Where the edge is the contact feature - for the end edge a center beyond the
// facing's end (s >= L), or above h on the pocket side of the plan line (Detect.h PredictFacingEndEdge); for the bottom edge a
// center below the face along [0, L] - a sample more than 1e-7 m inside distance R of the edge must come no earlier than the
// predicted contact (no missed contact), and a predicted contact lies at distance R of the edge (no phantom contact), with the
// fixed-contact normal pointing from the nearest edge point to the center.
RB_TEST(ARCH_VAL4b_FacingEdgesAgainstBruteForce)
{
	const TableSpec* Specs[3] = {&kTableNineFootPro, &kTableNineFootTight, &kTableSevenFootTrue};
	const NumericsConfig N{};
	const BallSpec Spec = MakeBallSpec(kR, kDefaultBallMass);
	Rng Random(0xED6Eu);
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr int kPerFacing = 20;
#else
	constexpr int kPerFacing = 200;
#endif
	constexpr double kHorizon = 0.1;
	constexpr double kStep = 5e-6;
	int Segments = 0;
	int EndContacts = 0;
	int BottomContacts = 0;
	int Missed = 0;
	int Phantom = 0;
	int BadNormal = 0;
	double WorstLate = 0.0;
	for (const TableSpec* S : Specs)
	{
		const TableGeometry& T = simtest::Table(*S);
		for (int f = 0; f < T.Facings.Size(); ++f)
		{
			const Facing& Face = T.Facings[f];
			const double TopZ = FacingEndEdgeTop(T, Face);
			const FacingEndEdge E = MakeFacingEndEdge(Face, TopZ);
			const Vec3 Side = ToVec3(Face.Direction);
			const Vec3 Front = ToVec3(Face.PocketNormal);
			const Vec3 Up{Face.PocketNormal.x * Sin(Face.Backdraft), Face.PocketNormal.y * Sin(Face.Backdraft), Cos(Face.Backdraft)};
			const Vec3 Bottom = FacingBottomEdgeStart(Face);
			// Distance to the end polyline and to the bottom line, and whether the edge is the contact feature there.
			const auto EndDistance = [&](const Vec3& P, Vec3& Nearest)
			{
				const Vec3 OnLower = E.Lower + E.LowerDirection * Clamp(Dot(P - E.Lower, E.LowerDirection), 0.0, E.LowerLength);
				const Vec3 OnUpper = E.Joint + Vec3::UnitZ() * Clamp(P.z - E.Joint.z, 0.0, E.UpperLength);
				Nearest = LengthSquared(P - OnUpper) < LengthSquared(P - OnLower) ? OnUpper : OnLower;
				return Length(P - Nearest);
			};
			const auto EndRegion = [&](const Vec3& P)
			{
				const Vec3 W = P - E.Joint;
				return Dot(Side, W) >= 0.0 || (W.z > 0.0 && Dot(Front, W) >= 0.0);
			};
			const auto BottomDistance = [&](const Vec3& P, bool& Region)
			{
				const double Along = Dot(P - Bottom, Side);
				Region = Along >= 0.0 && Along <= Face.Length && Dot(P - Bottom, Up) < 0.0;
				return Length(P - (Bottom + Side * Clamp(Along, 0.0, Face.Length)));
			};
			for (int k = 0; k < kPerFacing; ++k)
			{
				// Around the end edge (even k) or the bottom edge (odd k).
				const Vec3 Anchor = (k & 1) == 0 ? E.Joint + Vec3::UnitZ() * Random.NextUniform(-E.Joint.z, E.UpperLength)
												 : Bottom + Side * Random.NextUniform(0.0, Face.Length);
				const Vec3 Offset{Random.NextUniform(-0.06, 0.06), Random.NextUniform(-0.06, 0.06), Random.NextUniform(-0.06, 0.06)};
				MotionSegment Seg;
				Seg.State = MotionState::PocketFall;
				Seg.Radius = kR;
				Seg.T0 = Random.NextUniform(0.0, 5.0);
				Seg.Pos0 = Anchor + Offset;
				const Vec3 Aim = Normalized(Anchor + Vec3{Random.NextUniform(-0.01, 0.01), Random.NextUniform(-0.01, 0.01), Random.NextUniform(-0.01, 0.01)} -
					Seg.Pos0);
				Seg.Vel0 = Aim * Random.NextUniform(0.1, 3.0);
				Seg.Accel2 = {0.0, 0.0, -0.5 * simtest::kGVal};
				Seg.TauEnd = kInfinity;
				Vec3 Nearest;
				bool BottomRegion = false;
				if (EndDistance(Seg.Pos0, Nearest) <= kR + 1e-3 || BottomDistance(Seg.Pos0, BottomRegion) <= kR + 1e-3)
				{
					continue; // starts touching or inside: not a prediction case
				}
				++Segments;
				const ContactPrediction AtEnd = PredictFacingEndEdge(Seg, kR, Face, TopZ, Seg.T0 + kHorizon, N);
				const ContactPrediction AtBottom = PredictFacingBottomEdge(Seg, kR, Face, Seg.T0 + kHorizon, N);
				// No missed contact: where the distance first falls below R - 1e-7 m (sampled) with the center in the edge's region on both
				// sides of that step, the prediction comes no later. (A center entering the region already inside R - across the plan
				// line below h, or from the face's range - met the face first: that is the face's contact, not the edge's.)
				double FirstEndInside = kInfinity;
				double FirstBottomInside = kInfinity;
				bool EndDone = false;
				bool BottomDone = false;
				Vec3 Before = Seg.Pos0;
				for (double Tau = kStep; Tau <= kHorizon && !(EndDone && BottomDone); Tau += kStep)
				{
					const Vec3 P = PositionAt(Seg, Tau);
					if (!EndDone && EndDistance(P, Nearest) < kR - 1e-7)
					{
						EndDone = true;
						FirstEndInside = EndRegion(P) && EndRegion(Before) ? Seg.T0 + Tau : kInfinity;
					}
					bool RegionBefore = false;
					BottomDistance(Before, RegionBefore);
					if (!BottomDone && BottomDistance(P, BottomRegion) < kR - 1e-7)
					{
						BottomDone = true;
						FirstBottomInside = BottomRegion && RegionBefore ? Seg.T0 + Tau : kInfinity;
					}
					Before = P;
				}
				if (FirstEndInside < kInfinity && !(AtEnd.Found && AtEnd.Time <= FirstEndInside + 1e-12))
				{
					++Missed;
					WorstLate = Max(WorstLate, AtEnd.Found ? AtEnd.Time - FirstEndInside : kInfinity);
				}
				if (FirstBottomInside < kInfinity && !(AtBottom.Found && AtBottom.Time <= FirstBottomInside + 1e-12))
				{
					++Missed;
					WorstLate = Max(WorstLate, AtBottom.Found ? AtBottom.Time - FirstBottomInside : kInfinity);
				}
				// No phantom contact: at the predicted time the center is at distance R of the edge; the fixed contact's normal.
				if (AtEnd.Found)
				{
					++EndContacts;
					const Vec3 P = PositionAt(Seg, AtEnd.Time - Seg.T0);
					Phantom += Abs(EndDistance(P, Nearest) - kR) > 1e-7 ? 1 : 0;
					BallState B;
					B.Position = P;
					B.State = MotionState::PocketFall;
					const FixedContact C = MakeFixedContact({TableFeatureKind::FacingTopEdge, static_cast<std::uint8_t>(f), kFacingEndEdge}, T, B, Spec, DetectOptions{});
					BadNormal += Dot(C.Normal, Normalized(P - Nearest)) < 1.0 - 1e-9 ? 1 : 0;
				}
				if (AtBottom.Found)
				{
					++BottomContacts;
					const Vec3 P = PositionAt(Seg, AtBottom.Time - Seg.T0);
					Phantom += Abs(BottomDistance(P, BottomRegion) - kR) > 1e-7 ? 1 : 0;
					BallState B;
					B.Position = P;
					B.State = MotionState::PocketFall;
					const FixedContact C = MakeFixedContact({TableFeatureKind::FacingTopEdge, static_cast<std::uint8_t>(f), kFacingBottomEdge}, T, B, Spec, DetectOptions{});
					const Vec3 OnLine = Bottom + Side * Clamp(Dot(P - Bottom, Side), 0.0, Face.Length);
					BadNormal += Dot(C.Normal, Normalized(P - OnLine)) < 1.0 - 1e-9 ? 1 : 0;
				}
			}
		}
	}
	std::printf("  A-VAL-4b: %d segments, %d end-edge and %d bottom-edge contacts predicted; missed %d (worst late %.3g s), phantom %d, normals off %d\n",
		Segments, EndContacts, BottomContacts, Missed, WorstLate, Phantom, BadNormal);
	RB_CHECK(Segments > 18 * kPerFacing); // 36 facings; a fifth of the draws start touching and are skipped
	RB_CHECK(EndContacts > kPerFacing && BottomContacts > kPerFacing / 2);
	RB_CHECK(Missed == 0);
	RB_CHECK(Phantom == 0);
	RB_CHECK(BadNormal == 0);
}

// A-VAL-5 (WP-10 review): TABLE_7FT_TRUE B1 seed 13 038 951 of the review scan (100 000 B1 shots and 3 000 + 3 000 breaks per
// table preset). The cue ball bounces out of the head-right corner pocket onto the sloped cushion top, slides along it into the
// facing's back-end edge (WP-10) and hops on the edge, over the pocket's cut disc. Its Zeno contacts went to a cloth island whose
// member left at the first step (MemberExit: over the pocket opening, off the cloth -> Fly), the contact came back at the same
// instant, and the shot alternated between one-step islands and GRI until the event cap: SimStatus::Aborted, the only abort WP-10
// introduced in the scan (before WP-10 the edge did not exist and the shot ended Ok). The ball now joins as a rigid off-cloth
// member (like a pocket-state or rail-top ball), leaves once clear of the edge and drops into the pocket. The sloped cushion top's
// cut rim still costs it a series of one-step rail-top islands with overlap diagnostics (architecture O-24 b), so only the outcome
// is checked here: status Ok, far below the event cap, pocketed, everything at rest.
RB_TEST(Integ_ARCH_VAL5_OffClothPocketSeedIsHeld)
{
	const TableGeometry& T = simtest::Table(kTableSevenFootTrue);
	SimInput& In = simtest::InputSlot();
	ShotResult& R = simtest::ResultSlot();
	e2e::MakeB1Shot(13038951u, T, In);
	Simulator Sim;
	const SimStatus Status = Sim.Run(In, R);
	std::printf("  A-VAL-5 7ft-true B1 13038951: status %d, events %d, islands %d, overlaps %d, cue ball final status %d\n", static_cast<int>(Status),
		R.Diagnostics.EventsProcessed, R.Diagnostics.Islands, R.Diagnostics.OverlapWarnings, static_cast<int>(R.Finals[0].Status));
	RB_CHECK(Status == SimStatus::Ok);
	RB_CHECK(R.Diagnostics.EventsProcessed < 2000);
	RB_CHECK(!R.Diagnostics.IslandBudgetExceeded);
	RB_CHECK(R.Finals[0].Status == BallFinalStatus::Pocketed);
	RB_CHECK(e2e::AllAtRest(R));
}
