#include "Cue/RbCueMeshBuilder.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshOverlay.h"

// Owner: UE-4. Lathe of the cue profile (RbCueMeshBuilder.h): the profile runs from the tip apex over the dome, the
// leather, the tapered body and the rounded bumper to the centre of the butt end, with the material on the axis side,
// so one winding rule gives outward triangles for every band, step and end fan.
// The helpers live in RbCueMeshBuilder's own anonymous namespace, not the global one: the unity build compiles this file in
// one translation unit with other mesh builders that have file-local helpers of the same names.

namespace RbCueMeshBuilder
{
namespace
{
	using UE::Geometry::FDynamicMesh3;
	using UE::Geometry::FIndex3i;

	constexpr double kStandardLengthCm = 147.32; // rb::kCuePlaying19oz: the option defaults describe this cue
	constexpr int32 kFilletSteps = 4;

	uint32 FloatBits(float V)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &V, sizeof(Bits));
		return Bits;
	}

	using FElementKey = TTuple<int32, uint32, uint32, uint32, uint32>;

	// Overlay elements shared between triangles when (vertex, value) are identical, split otherwise (hard edges, UV seam,
	// section seams, poles).
	template <typename OverlayType, typename ValueType, int32 N>
	struct TElementCache
	{
		OverlayType* Overlay = nullptr;
		TMap<FElementKey, int32> Map;

		int32 Get(int32 Vertex, const ValueType& Value)
		{
			uint32 Bits[4] = {0, 0, 0, 0};
			for (int32 Index = 0; Index < N; ++Index)
			{
				Bits[Index] = FloatBits(Value[Index]);
			}
			const FElementKey Key(Vertex, Bits[0], Bits[1], Bits[2], Bits[3]);
			if (const int32* Found = Map.Find(Key))
			{
				return *Found;
			}
			const int32 Element = Overlay->AppendElement(Value);
			Map.Add(Key, Element);
			return Element;
		}
	};

	// One point of the profile in the (X, R) half plane: position, outward unit normal (NX, NR), s behind the rim [cm].
	struct FProfilePoint
	{
		double X = 0.0;
		double R = 0.0;
		double NX = 0.0;
		double NR = 1.0;
		double S = 0.0;
	};

	// Consecutive profile points of one section with smooth normals. Consecutive strips share their end / start position.
	struct FStrip
	{
		ERbCueSection Section = ERbCueSection::Tip;
		double SectionStart = 0.0; // [cm] s range of the section (UV1.y)
		double SectionEnd = 0.0;
		TArray<FProfilePoint> Points;
	};

	struct FLayout
	{
		double LengthCm = 0.0;
		double DomeRadiusCm = 0.0;
		double RimHalfWidthCm = 0.0;
		double RimDepthCm = 0.0;
		FRbCueMeshOptions Options; // resolved
		double FerruleEnd = 0.0;
		double JointStart = 0.0;
		double JointEnd = 0.0;
		double WrapStart = 0.0;
		double WrapEnd = 0.0;
		double BumperStart = 0.0;
	};

	double SafeLengthCm(const rb::CueSpec& Cue)
	{
		return FMath::IsFinite(Cue.Length) && Cue.Length > 0.0 ? 100.0 * Cue.Length : kStandardLengthCm;
	}

	FLayout MakeLayout(const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, const FRbCueMeshOptions& In)
	{
		FLayout Layout;
		const double L = SafeLengthCm(Cue);
		Layout.LengthCm = L;
		Layout.DomeRadiusCm = FMath::IsFinite(Cue.TipDomeRadius) && Cue.TipDomeRadius > 0.0 ? 100.0 * Cue.TipDomeRadius : 1.06;
		const double HalfWidth = FMath::IsFinite(Cue.TipDiameter) && Cue.TipDiameter > 0.0 ? 50.0 * Cue.TipDiameter : 0.6375;
		Layout.RimHalfWidthCm = FMath::Min(HalfWidth, Layout.DomeRadiusCm);
		Layout.RimDepthCm = RbCueMeshBuilder::RimDepthCm(Cue);

		FRbCueMeshOptions& O = Layout.Options;
		O = In;
		O.RadialSegments = FMath::Max(8, (FMath::Max(In.RadialSegments, 1) + 3) / 4 * 4);
		O.DomeRings = FMath::Clamp(In.DomeRings, 2, 64);
		O.MaxRingSpacingCm = FMath::IsFinite(In.MaxRingSpacingCm) && In.MaxRingSpacingCm > 0.1 ? In.MaxRingSpacingCm : 5.0;

		// Rim -> ferrule end (the core's Ferrule contact zone includes the leather) and the leather height inside it.
		const double BodyFerrule = FMath::IsFinite(Body.FerruleLength) && Body.FerruleLength > 0.0 ? 100.0 * Body.FerruleLength : 2.5;
		const double Ferrule = In.FerruleLengthCm >= 0.0 && FMath::IsFinite(In.FerruleLengthCm) ? In.FerruleLengthCm : BodyFerrule;
		O.FerruleLengthCm = FMath::Clamp(Ferrule, 0.2, 0.5 * L);
		O.TipHeightCm = FMath::Clamp(FMath::IsFinite(In.TipHeightCm) ? In.TipHeightCm : 0.6, 0.05, 0.6 * O.FerruleLengthCm);
		Layout.FerruleEnd = O.FerruleLengthCm;

		// Joint and butt sections (option positions describe the 58 in cue; other lengths scale the butt layout).
		const double BodyShaft = FMath::IsFinite(Body.ShaftLength) && Body.ShaftLength > 0.0 ? 100.0 * Body.ShaftLength : 74.0;
		const double Joint = In.JointFromTipCm >= 0.0 && FMath::IsFinite(In.JointFromTipCm) ? In.JointFromTipCm : BodyShaft;
		O.BumperFilletCm = FMath::Clamp(FMath::IsFinite(In.BumperFilletCm) ? In.BumperFilletCm : 0.3, 0.02, 0.5);
		O.BumperLengthCm = FMath::Clamp(FMath::IsFinite(In.BumperLengthCm) ? In.BumperLengthCm : 1.2, O.BumperFilletCm + 0.05, 0.1 * L);
		Layout.BumperStart = L - O.BumperLengthCm;
		O.JointFromTipCm = FMath::Clamp(Joint, Layout.FerruleEnd, Layout.BumperStart);
		Layout.JointStart = O.JointFromTipCm;
		O.JointCollarCm = FMath::Clamp(FMath::IsFinite(In.JointCollarCm) ? In.JointCollarCm : 2.0, 0.0, Layout.BumperStart - Layout.JointStart);
		Layout.JointEnd = Layout.JointStart + O.JointCollarCm;
		const double Scale = L / kStandardLengthCm;
		const double WrapStart = (FMath::IsFinite(In.WrapStartFromTipCm) ? In.WrapStartFromTipCm : 100.0) * Scale;
		const double WrapLength = FMath::Max(0.0, (FMath::IsFinite(In.WrapLengthCm) ? In.WrapLengthCm : 25.0) * Scale);
		Layout.WrapStart = FMath::Clamp(WrapStart, Layout.JointEnd, Layout.BumperStart);
		Layout.WrapEnd = FMath::Clamp(Layout.WrapStart + WrapLength, Layout.WrapStart, Layout.BumperStart);
		O.WrapStartFromTipCm = Layout.WrapStart;
		O.WrapLengthCm = Layout.WrapEnd - Layout.WrapStart;
		return Layout;
	}

	// Outward profile normal of the taper (tangent (dX, dR) = (-1, k) per cm of s): (k, 1) / |(k, 1)|.
	void TaperNormal(double K, double& NX, double& NR)
	{
		const double Len = FMath::Sqrt(1.0 + K * K);
		NX = K / Len;
		NR = 1.0 / Len;
	}

	// Taper points of [S0, S1] every <= MaxSpacing cm (both ends included).
	void AddTaperPoints(FStrip& Strip, const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, const FLayout& Layout, double S0, double S1)
	{
		const double K = (RbCueMeshBuilder::TaperRadiusCm(Cue, Body, Layout.LengthCm) - RbCueMeshBuilder::TaperRadiusCm(Cue, Body, 0.0)) /
			Layout.LengthCm;
		double NX = 0.0;
		double NR = 1.0;
		TaperNormal(K, NX, NR);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt32((S1 - S0) / Layout.Options.MaxRingSpacingCm));
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const double S = Step == Steps ? S1 : S0 + (S1 - S0) * static_cast<double>(Step) / static_cast<double>(Steps);
			FProfilePoint P;
			P.S = S;
			P.X = Layout.RimDepthCm - S;
			P.R = RbCueMeshBuilder::TaperRadiusCm(Cue, Body, S);
			P.NX = NX;
			P.NR = NR;
			Strip.Points.Add(P);
		}
	}

	FStrip MakeTaperStrip(ERbCueSection Section, const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, const FLayout& Layout, double S0,
		double S1)
	{
		FStrip Strip;
		Strip.Section = Section;
		Strip.SectionStart = S0;
		Strip.SectionEnd = S1;
		AddTaperPoints(Strip, Cue, Body, Layout, S0, S1);
		return Strip;
	}

	TArray<FStrip> MakeStrips(const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, const FLayout& Layout)
	{
		const FRbCueMeshOptions& O = Layout.Options;
		const double L = Layout.LengthCm;
		const double RDome = Layout.DomeRadiusCm;
		const double RD = Layout.RimDepthCm;
		const double Cap = RDome - RD; // dome cap height ahead of the rim
		TArray<FStrip> Strips;

		// 1. Dome (sphere cap about the origin) from the apex (pole) to the rim; the tip section spans the cap and the leather.
		{
			FStrip Dome;
			Dome.Section = ERbCueSection::Tip;
			Dome.SectionStart = -Cap;
			Dome.SectionEnd = O.TipHeightCm;
			const double RimAngle = FMath::Asin(FMath::Clamp(Layout.RimHalfWidthCm / RDome, 0.0, 1.0));
			for (int32 Ring = 0; Ring <= O.DomeRings; ++Ring)
			{
				const double Angle = RimAngle * static_cast<double>(Ring) / static_cast<double>(O.DomeRings);
				FProfilePoint P;
				P.NX = FMath::Cos(Angle);
				P.NR = FMath::Sin(Angle);
				P.X = Ring == 0 ? RDome : (Ring == O.DomeRings ? RD : RDome * P.NX);
				P.R = Ring == 0 ? 0.0 : (Ring == O.DomeRings ? Layout.RimHalfWidthCm : RDome * P.NR);
				P.S = RD - P.X;
				Dome.Points.Add(P);
			}
			Strips.Add(MoveTemp(Dome));
		}

		// 2. Leather side: a flush cone from the rim to the ferrule radius at the leather's end (hard edge at the rim).
		{
			FStrip Leather;
			Leather.Section = ERbCueSection::Tip;
			Leather.SectionStart = -Cap;
			Leather.SectionEnd = O.TipHeightCm;
			const double REnd = RbCueMeshBuilder::TaperRadiusCm(Cue, Body, O.TipHeightCm);
			const double DR = REnd - Layout.RimHalfWidthCm;
			const double Len = FMath::Sqrt(DR * DR + O.TipHeightCm * O.TipHeightCm);
			FProfilePoint A;
			A.X = RD;
			A.R = Layout.RimHalfWidthCm;
			A.S = 0.0;
			A.NX = DR / Len;
			A.NR = O.TipHeightCm / Len;
			FProfilePoint B = A;
			B.X = RD - O.TipHeightCm;
			B.R = REnd;
			B.S = O.TipHeightCm;
			Leather.Points.Add(A);
			Leather.Points.Add(B);
			Strips.Add(MoveTemp(Leather));
		}

		// 3.. Taper sections (every vertex on r(s)).
		auto AddTaper = [&](ERbCueSection Section, double S0, double S1)
		{
			if (S1 - S0 > 1e-6)
			{
				Strips.Add(MakeTaperStrip(Section, Cue, Body, Layout, S0, S1));
			}
		};
		AddTaper(ERbCueSection::Ferrule, O.TipHeightCm, Layout.FerruleEnd);
		AddTaper(ERbCueSection::Shaft, Layout.FerruleEnd, Layout.JointStart);
		AddTaper(ERbCueSection::Joint, Layout.JointStart, Layout.JointEnd);
		AddTaper(ERbCueSection::Forearm, Layout.JointEnd, Layout.WrapStart);
		AddTaper(ERbCueSection::Wrap, Layout.WrapStart, Layout.WrapEnd);
		AddTaper(ERbCueSection::Sleeve, Layout.WrapEnd, Layout.BumperStart);

		// Bumper: taper side up to the fillet, then the fillet quarter circle and the flat end disc to the axis (pole).
		{
			const double Fillet = O.BumperFilletCm;
			FStrip Side = MakeTaperStrip(ERbCueSection::Bumper, Cue, Body, Layout, Layout.BumperStart, L - Fillet);
			Side.SectionEnd = L;
			Strips.Add(MoveTemp(Side));

			FStrip End;
			End.Section = ERbCueSection::Bumper;
			End.SectionStart = Layout.BumperStart;
			End.SectionEnd = L;
			const double CenterS = L - Fillet;
			const double CenterR = RbCueMeshBuilder::TaperRadiusCm(Cue, Body, CenterS) - Fillet;
			for (int32 Step = 0; Step <= kFilletSteps; ++Step)
			{
				const double Beta = 0.5 * UE_DOUBLE_PI * static_cast<double>(Step) / static_cast<double>(kFilletSteps);
				const double SinB = Step == kFilletSteps ? 1.0 : FMath::Sin(Beta);
				const double CosB = Step == kFilletSteps ? 0.0 : FMath::Cos(Beta);
				FProfilePoint P;
				P.S = Step == kFilletSteps ? L : CenterS + Fillet * SinB;
				P.X = RD - P.S;
				P.R = Step == 0 ? RbCueMeshBuilder::TaperRadiusCm(Cue, Body, CenterS) : CenterR + Fillet * CosB;
				P.NX = -SinB;
				P.NR = CosB;
				End.Points.Add(P);
			}
			FProfilePoint Pole;
			Pole.S = L;
			Pole.X = RD - L;
			Pole.R = 0.0;
			Pole.NX = -1.0;
			Pole.NR = 0.0;
			End.Points.Add(Pole);
			Strips.Add(MoveTemp(End));
		}
		return Strips;
	}
}

	double RimDepthCm(const rb::CueSpec& Cue)
	{
		const double RDome = FMath::IsFinite(Cue.TipDomeRadius) && Cue.TipDomeRadius > 0.0 ? 100.0 * Cue.TipDomeRadius : 1.06;
		const double Half = FMath::IsFinite(Cue.TipDiameter) && Cue.TipDiameter > 0.0 ? 50.0 * Cue.TipDiameter : 0.6375;
		return FMath::Sqrt(FMath::Max(0.0, RDome * RDome - Half * Half));
	}

	double TaperRadiusCm(const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, double SCm)
	{
		const double L = SafeLengthCm(Cue);
		const double Rt = 100.0 * Body.TipRadius;
		const double Rb = 100.0 * Body.ButtRadius;
		const double S = FMath::Clamp(SCm, 0.0, L);
		return Rt + (Rb - Rt) * S / L;
	}

	FRbCueMeshOptions Resolve(const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, const FRbCueMeshOptions& Options)
	{
		return MakeLayout(Cue, Body, Options).Options;
	}

	TArray<FRbCueSectionRange> SectionRanges(const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, const FRbCueMeshOptions& Options)
	{
		const FLayout Layout = MakeLayout(Cue, Body, Options);
		const FRbCueMeshOptions& O = Layout.Options;
		TArray<FRbCueSectionRange> Out;
		auto Add = [&Out](ERbCueSection Section, double S0, double S1)
		{
			if (S1 - S0 > 1e-6)
			{
				Out.Add(FRbCueSectionRange{Section, S0, S1});
			}
		};
		Add(ERbCueSection::Tip, -(Layout.DomeRadiusCm - Layout.RimDepthCm), O.TipHeightCm);
		Add(ERbCueSection::Ferrule, O.TipHeightCm, Layout.FerruleEnd);
		Add(ERbCueSection::Shaft, Layout.FerruleEnd, Layout.JointStart);
		Add(ERbCueSection::Joint, Layout.JointStart, Layout.JointEnd);
		Add(ERbCueSection::Forearm, Layout.JointEnd, Layout.WrapStart);
		Add(ERbCueSection::Wrap, Layout.WrapStart, Layout.WrapEnd);
		Add(ERbCueSection::Sleeve, Layout.WrapEnd, Layout.BumperStart);
		Add(ERbCueSection::Bumper, Layout.BumperStart, Layout.LengthCm);
		return Out;
	}

	FVector4f SectionColor(ERbCueSection Section)
	{
		switch (Section)
		{
		case ERbCueSection::Tip: return FVector4f(0.035f, 0.075f, 0.20f, 0.80f);     // leather under blue chalk
		case ERbCueSection::Ferrule: return FVector4f(0.78f, 0.76f, 0.70f, 0.25f);   // ivory-white ferrule
		case ERbCueSection::Shaft: return FVector4f(0.72f, 0.54f, 0.32f, 0.30f);     // satin maple
		case ERbCueSection::Joint: return FVector4f(0.020f, 0.020f, 0.020f, 0.15f);  // black phenolic collar
		case ERbCueSection::Forearm: return FVector4f(0.055f, 0.022f, 0.010f, 0.12f); // lacquered dark-stained wood
		case ERbCueSection::Wrap: return FVector4f(0.018f, 0.018f, 0.020f, 0.90f);   // black Irish linen
		case ERbCueSection::Sleeve: return FVector4f(0.040f, 0.016f, 0.008f, 0.12f); // lacquered dark wood
		case ERbCueSection::Bumper: return FVector4f(0.012f, 0.012f, 0.012f, 0.70f); // rubber
		case ERbCueSection::Count: break;
		}
		return FVector4f(0.5f, 0.5f, 0.5f, 0.5f);
	}

	const TCHAR* SectionName(ERbCueSection Section)
	{
		switch (Section)
		{
		case ERbCueSection::Tip: return TEXT("Tip");
		case ERbCueSection::Ferrule: return TEXT("Ferrule");
		case ERbCueSection::Shaft: return TEXT("Shaft");
		case ERbCueSection::Joint: return TEXT("Joint");
		case ERbCueSection::Forearm: return TEXT("Forearm");
		case ERbCueSection::Wrap: return TEXT("Wrap");
		case ERbCueSection::Sleeve: return TEXT("Sleeve");
		case ERbCueSection::Bumper: return TEXT("Bumper");
		case ERbCueSection::Count: break;
		}
		return TEXT("?");
	}

	void BuildCue(const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, const FRbCueMeshOptions& Options, FDynamicMesh3& Out)
	{
		const FLayout Layout = MakeLayout(Cue, Body, Options);
		const TArray<FStrip> Strips = MakeStrips(Cue, Body, Layout);
		const int32 S = Layout.Options.RadialSegments;

		Out.Clear();
		Out.EnableTriangleGroups(0);
		Out.EnableAttributes();
		UE::Geometry::FDynamicMeshAttributeSet* Attributes = Out.Attributes();
		Attributes->SetNumUVLayers(2);
		Attributes->EnablePrimaryColors();

		TElementCache<UE::Geometry::FDynamicMeshNormalOverlay, FVector3f, 3> Normals{Attributes->PrimaryNormals()};
		TElementCache<UE::Geometry::FDynamicMeshUVOverlay, FVector2f, 2> UV0{Attributes->GetUVLayer(0)};
		TElementCache<UE::Geometry::FDynamicMeshUVOverlay, FVector2f, 2> UV1{Attributes->GetUVLayer(1)};
		TElementCache<UE::Geometry::FDynamicMeshColorOverlay, FVector4f, 4> Colors{Attributes->PrimaryColors()};

		// Exact cos / sin on the quarter meridians (mirror-symmetric rings).
		TArray<double> CosPsi;
		TArray<double> SinPsi;
		CosPsi.SetNum(S + 1);
		SinPsi.SetNum(S + 1);
		const int32 Quarter = S / 4;
		for (int32 J = 0; J <= S; ++J)
		{
			const int32 Wrapped = J % S;
			if (Wrapped % Quarter == 0)
			{
				static constexpr double QuarterCos[4] = {1.0, 0.0, -1.0, 0.0};
				static constexpr double QuarterSin[4] = {0.0, 1.0, 0.0, -1.0};
				CosPsi[J] = QuarterCos[Wrapped / Quarter];
				SinPsi[J] = QuarterSin[Wrapped / Quarter];
			}
			else
			{
				const double Psi = UE_DOUBLE_TWO_PI * static_cast<double>(Wrapped) / static_cast<double>(S);
				CosPsi[J] = FMath::Cos(Psi);
				SinPsi[J] = FMath::Sin(Psi);
			}
		}

		// Mesh vertices per profile point: a pole (R = 0) has one vertex, a ring S; consecutive strips share their joint ring.
		TArray<TArray<TArray<int32>>> StripRings; // [strip][point] -> vertex ids (1 for a pole, S for a ring)
		StripRings.SetNum(Strips.Num());
		const TArray<int32>* PreviousLast = nullptr;
		for (int32 StripIndex = 0; StripIndex < Strips.Num(); ++StripIndex)
		{
			const FStrip& Strip = Strips[StripIndex];
			TArray<TArray<int32>>& Rings = StripRings[StripIndex];
			Rings.SetNum(Strip.Points.Num());
			for (int32 PointIndex = 0; PointIndex < Strip.Points.Num(); ++PointIndex)
			{
				if (PointIndex == 0 && PreviousLast)
				{
					Rings[0] = *PreviousLast; // the same position as the previous strip's end (hard edge / section seam)
					continue;
				}
				const FProfilePoint& P = Strip.Points[PointIndex];
				if (P.R <= 0.0)
				{
					Rings[PointIndex].Add(Out.AppendVertex(FVector3d(P.X, 0.0, 0.0)));
				}
				else
				{
					for (int32 J = 0; J < S; ++J)
					{
						Rings[PointIndex].Add(Out.AppendVertex(FVector3d(P.X, P.R * CosPsi[J], P.R * SinPsi[J])));
					}
				}
			}
			PreviousLast = &Rings.Last();
		}

		struct FCorner
		{
			int32 Vertex = 0;
			int32 N = 0;
			int32 U0 = 0;
			int32 U1 = 0;
			int32 C = 0;
		};
		for (int32 StripIndex = 0; StripIndex < Strips.Num(); ++StripIndex)
		{
			const FStrip& Strip = Strips[StripIndex];
			const int32 Group = static_cast<int32>(Strip.Section);
			const FVector4f Color = SectionColor(Strip.Section);
			const double SectionLength = FMath::Max(1e-9, Strip.SectionEnd - Strip.SectionStart);
			// Corner at profile point I, unwrapped column J (0 .. S); a pole takes the triangle's mid column for its UV / normal.
			auto MakeCorner = [&](int32 I, int32 J, int32 TriColumn) -> FCorner
			{
				const FProfilePoint& P = Strip.Points[I];
				const TArray<int32>& Ring = StripRings[StripIndex][I];
				const bool bPole = Ring.Num() == 1;
				double CP = 0.0;
				double SP = 0.0;
				double V = 0.0;
				if (bPole)
				{
					const double Psi = UE_DOUBLE_TWO_PI * (static_cast<double>(TriColumn) + 0.5) / static_cast<double>(S);
					CP = FMath::Cos(Psi);
					SP = FMath::Sin(Psi);
					V = (static_cast<double>(TriColumn) + 0.5) / static_cast<double>(S);
				}
				else
				{
					CP = CosPsi[J];
					SP = SinPsi[J];
					V = static_cast<double>(J) / static_cast<double>(S);
				}
				FCorner Corner;
				Corner.Vertex = bPole ? Ring[0] : Ring[J % S];
				const FVector3f Normal(static_cast<float>(P.NX), static_cast<float>(P.NR * CP), static_cast<float>(P.NR * SP));
				Corner.N = Normals.Get(Corner.Vertex, bPole ? FVector3f(static_cast<float>(P.NX >= 0.0 ? 1.0 : -1.0), 0.0f, 0.0f) : Normal);
				Corner.U0 = UV0.Get(Corner.Vertex, FVector2f(static_cast<float>(0.01 * P.S), static_cast<float>(V)));
				const double T = FMath::Clamp((P.S - Strip.SectionStart) / SectionLength, 0.0, 1.0);
				Corner.U1 = UV1.Get(Corner.Vertex, FVector2f(static_cast<float>(Group), static_cast<float>(T)));
				Corner.C = Colors.Get(Corner.Vertex, Color);
				return Corner;
			};
			auto AddTriangle = [&](const FCorner& A, const FCorner& B, const FCorner& C)
			{
				const int32 Tri = Out.AppendTriangle(A.Vertex, B.Vertex, C.Vertex, Group);
				check(Tri >= 0);
				Normals.Overlay->SetTriangle(Tri, FIndex3i(A.N, B.N, C.N));
				UV0.Overlay->SetTriangle(Tri, FIndex3i(A.U0, B.U0, C.U0));
				UV1.Overlay->SetTriangle(Tri, FIndex3i(A.U1, B.U1, C.U1));
				Colors.Overlay->SetTriangle(Tri, FIndex3i(A.C, B.C, C.C));
			};

			for (int32 I = 0; I + 1 < Strip.Points.Num(); ++I)
			{
				const bool bFrontPole = StripRings[StripIndex][I].Num() == 1;
				const bool bBackPole = StripRings[StripIndex][I + 1].Num() == 1;
				for (int32 J = 0; J < S; ++J)
				{
					// Front = point I (nearer the tip), back = I + 1. Outward winding in UE (VectorUtil::Normal):
					// (F_j, F_j+1, B_j) and (F_j+1, B_j+1, B_j); the fans at the poles keep the non-degenerate one.
					if (bFrontPole)
					{
						AddTriangle(MakeCorner(I, J, J), MakeCorner(I + 1, J + 1, J), MakeCorner(I + 1, J, J));
					}
					else if (bBackPole)
					{
						AddTriangle(MakeCorner(I, J, J), MakeCorner(I, J + 1, J), MakeCorner(I + 1, J, J));
					}
					else
					{
						const FCorner F0 = MakeCorner(I, J, J);
						const FCorner F1 = MakeCorner(I, J + 1, J);
						const FCorner B0 = MakeCorner(I + 1, J, J);
						const FCorner B1 = MakeCorner(I + 1, J + 1, J);
						AddTriangle(F0, F1, B0);
						AddTriangle(F1, B1, B0);
					}
				}
			}
		}
	}
}
