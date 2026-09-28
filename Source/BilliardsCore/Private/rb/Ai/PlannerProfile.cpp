#include "rb/Core/FpGuard.h"
// Owner: WP-12 (AI opponent). Planner profiles (Docs/architecture.md 7.6; human-factors 5.5). All values TUNING.
#include "rb/Ai/PlannerProfile.h"

#include "rb/Core/Constants.h"
#include "rb/Math/Scalar.h"

namespace rb::ai
{
	namespace
	{
		struct Knobs
		{
			bool Banks;
			bool Kicks;
			bool Combinations;
			bool PushOuts;
			int PotFamilies;
			int SpeedVariants;
			int SpinVariants;
			int SafetyTargets;
			int Placements;
			int AimIterations;
			int NoisyCandidates;
			int SecondPlyCandidates;
			int SecondPlyFamilies;
			int SimulationBudget;
			double PerceivedAimSigmaDeg;
			double RunoutRate;
			double SafetyQuality;
			double KickSkill;
			double SafetyBias;
			double SpeedBias;
			double BreakSpeed;
			double ChoiceTolerance;
			double LagTarget;      // [m]
			double AttributeLevel; // mean of the six attributes of the 5.5 profile (OpponentModelFromAttributes)
		};

		// The WP-12 table (Docs/architecture.md 7.6). Perceived aim sigmas: the cue-ball direction spread each profile's hand and
		// human layer produce on a medium shot (A-AI-8 prints the measured values), rounded.
		constexpr Knobs kKnobs[human::kAiProfileCount] = {
			// Banks Kicks Combos Push | fam spd spin saf plc aim M ply2 fam2 budget | sigma  q     SQ    kick  sbias speed brk  choice lag  level
			{false, false, false, false, 4, 2, 1, 0, 3, 0, 0, 0, 0, 80, 0.40, 0.35, 0.00, 0.30, 0.00, 1.00, 6.0, 0.12, 0.30, 14.2},  // Tourist
			{false, false, false, false, 6, 3, 3, 0, 5, 0, 0, 0, 0, 200, 0.22, 0.55, 0.15, 0.45, 0.00, 1.25, 7.5, 0.06, 0.18, 35.8}, // Bar regular
			{true, false, false, true, 8, 3, 4, 3, 8, 2, 0, 0, 0, 900, 0.12, 0.68, 0.35, 0.60, 0.00, 1.05, 8.0, 0.03, 0.12, 50.0},    // League player
			{true, true, true, true, 10, 4, 5, 4, 10, 2, 0, 3, 4, 2200, 0.08, 0.78, 0.50, 0.70, 0.00, 1.00, 8.5, 0.01, 0.08, 69.2},   // Local hustler
			{true, true, true, true, 12, 4, 6, 5, 12, 3, 24, 4, 5, 5000, 0.06, 0.85, 0.65, 0.80, 0.00, 1.00, 9.0, 0.00, 0.06, 75.0},  // Road player
			{true, true, true, true, 14, 4, 7, 6, 16, 3, 32, 6, 6, 9000, 0.04, 0.90, 0.80, 0.85, 0.00, 1.00, 9.5, 0.00, 0.05, 90.0},  // Touring pro
		};

		int IndexOf(human::AiProfileId Id)
		{
			const int I = static_cast<int>(Id);
			return I < 0 ? 0 : (I >= human::kAiProfileCount ? human::kAiProfileCount - 1 : I);
		}

		OpponentModel ModelOf(const PlannerProfile& P)
		{
			OpponentModel M;
			M.AimSigma = P.PerceivedAimSigma;
			M.RunoutRate = P.RunoutRate;
			M.SafetyQuality = P.SafetyQuality;
			M.KickSkill = P.KickSkill;
			M.PlaysSafeties = P.Safeties;
			return M;
		}
	}

	PlannerProfile MakePlannerProfile(const human::AiProfile& Profile)
	{
		const Knobs& K = kKnobs[IndexOf(Profile.Id)];
		PlannerProfile P;
		P.Id = Profile.Id;
		P.ModelsThrowAndSquirt = Profile.Knowledge.ModelsThrowAndSquirt;
		P.KnowsTableSlope = Profile.Knowledge.KnowsTableSlope;
		P.PositionDepth = Profile.Knowledge.PlanDepth < 0 ? 0 : Profile.Knowledge.PlanDepth;
		P.Safeties = Profile.Knowledge.Safeties;
		P.Sandbagger = Profile.Knowledge.SandbagsUntilMoney;
		P.Banks = K.Banks;
		P.Kicks = K.Kicks && P.Safeties;
		P.Combinations = K.Combinations;
		P.Caroms = K.Combinations; // the profiles that see combinations see kisses and caroms as well
		P.PushOuts = K.PushOuts;
		P.PotFamilies = K.PotFamilies;
		P.SpeedVariants = K.SpeedVariants;
		P.SpinVariants = K.SpinVariants;
		P.SafetyTargets = P.Safeties ? K.SafetyTargets : 0;
		P.Placements = K.Placements;
		P.AimIterations = P.ModelsThrowAndSquirt ? K.AimIterations : 0;
		P.NoisySamples = Profile.Knowledge.AssumesPerfectExecution ? 0 : Profile.Knowledge.SelfNoiseSamples;
		P.NoisyCandidates = P.NoisySamples > 0 ? (K.NoisyCandidates > 0 ? K.NoisyCandidates : 16) : 0;
		P.SecondPlyCandidates = P.PositionDepth >= 3 ? K.SecondPlyCandidates : 0;
		P.SecondPlyFamilies = P.SecondPlyCandidates > 0 ? K.SecondPlyFamilies : 0;
		P.SimulationBudget = K.SimulationBudget;
		P.PerceivedAimSigma = K.PerceivedAimSigmaDeg * kDegToRad;
		P.RunoutRate = K.RunoutRate;
		P.SafetyQuality = K.SafetyQuality;
		P.KickSkill = K.KickSkill;
		P.SafetyBias = K.SafetyBias;
		P.SpeedBias = K.SpeedBias;
		P.BreakSpeed = K.BreakSpeed;
		P.ChoiceTolerance = K.ChoiceTolerance;
		P.LagTarget = K.LagTarget;
		return P;
	}

	PlannerProfile GetPlannerProfile(human::AiProfileId Id) { return MakePlannerProfile(human::GetAiProfile(Id)); }

	PlannerProfile SandbaggingProfile(const PlannerProfile& Profile)
	{
		PlannerProfile P = GetPlannerProfile(human::AiProfileId::BarRegular);
		P.Id = Profile.Id;
		P.Sandbagger = true;
		P.KnowsTableSlope = Profile.KnowsTableSlope;
		P.SpeedBias = 1.0;             // no need to hit hard: he is hiding, not showing off
		P.ChoiceTolerance = 0.08;      // "dogs" a shot now and then
		return P;
	}

	OpponentModel OpponentModelFor(human::AiProfileId Id) { return ModelOf(GetPlannerProfile(Id)); }

	OpponentModel OpponentModelFromAttributes(const human::ShooterAttributes& A)
	{
		const double Level = (A.Steadiness + A.SpeedControl + A.SpinTouch + A.BridgeStability + A.Stance + A.Nerve) / 6.0;
		int Hi = 1;
		while (Hi < human::kAiProfileCount - 1 && kKnobs[Hi].AttributeLevel < Level)
		{
			++Hi;
		}
		const Knobs& L = kKnobs[Hi - 1];
		const Knobs& H = kKnobs[Hi];
		const double T = Clamp((Level - L.AttributeLevel) / (H.AttributeLevel - L.AttributeLevel), 0.0, 1.0);
		const auto Lerp = [T](double X, double Y) { return X + (Y - X) * T; };
		OpponentModel M;
		M.AimSigma = Exp(Lerp(Log(L.PerceivedAimSigmaDeg), Log(H.PerceivedAimSigmaDeg))) * kDegToRad;
		M.RunoutRate = Lerp(L.RunoutRate, H.RunoutRate);
		M.SafetyQuality = Lerp(L.SafetyQuality, H.SafetyQuality);
		M.KickSkill = Lerp(L.KickSkill, H.KickSkill);
		M.PlaysSafeties = Level >= 45.0;
		return M;
	}
}
