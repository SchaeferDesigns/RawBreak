#include "Audio/RbAudioLive.h"

#include "RbAudio/RbAudioMath.h"
#include "RbAudio/RbBallKernels.h"
#include "RbAudio/RbContactShape.h"
#include "RbAudio/RbImpactSynth.h"

#include <cmath>

// Owner: M2-C.

namespace RbAudioLive
{
	void FloorContact(RbAudio::EFloorSurface Surface, double& OutT1, double& OutE, double& OutReflection)
	{
		switch (Surface)
		{
		case RbAudio::EFloorSurface::Concrete: OutT1 = 0.25e-3; OutE = 0.60; OutReflection = 0.95; break; // sealed concrete
		case RbAudio::EFloorSurface::Rubber: OutT1 = 1.60e-3; OutE = 0.30; OutReflection = 0.60; break;   // anti-fatigue mat
		case RbAudio::EFloorSurface::Wood: OutT1 = 0.40e-3; OutE = 0.50; OutReflection = 0.90; break;
		case RbAudio::EFloorSurface::Vct:
		default: OutT1 = 0.30e-3; OutE = 0.55; OutReflection = 0.90; break;                               // AU-25 (ESTIMATE)
		}
	}

	RbAudio::FImpactEvent MakeFloorHitEvent(const FRbFloorHitParams& P, double SampleRate)
	{
		using namespace RbAudio;
		double T1 = 0.3e-3;
		double E = 0.55;
		double Reflection = 0.9;
		FloorContact(P.Surface, T1, E, Reflection);
		const double V = FMath::Max(P.NormalSpeed, 0.0);
		const double M = FMath::Max(P.BallMass, 1e-3);
		const double R = FMath::Max(P.BallRadius, 1e-3);
		const double K = StiffnessForContactTime(T1, M);

		FImpactEvent Ev;
		Ev.Kind = EImpactKind::FloorHit;
		Ev.Pulse = EPulseShape::Hertz;
		Ev.Shape = GetContactShape(E);
		const FContactPulse Pulse = HertzPulse(*Ev.Shape, V, M, K);
		Ev.ContactTime = Pulse.Duration;
		Ev.PeakForce = Pulse.PeakForce;
		Ev.Impulse = Pulse.Impulse;
		Ev.NormalSpeed = V;
		Ev.Seed = P.Seed;

		FBallAcoustics Ball;
		Ball.Radius = R;
		Ball.Mass = M;
		Ball.Material = PhenolicMaterial();
		Ev.Kernels = GetBallKernels(Ball, SampleRate, false);

		const FVector N = P.PlaneNormal.GetSafeNormal(1e-6, FVector::UpVector);
		const FVector Contact = P.ContactPointCm / 100.0;
		const FVector Listener = P.ListenerCm / 100.0;
		const FVector Centre = Contact + N * R;
		const FVector Axis = -N; // centre -> contact point
		const double Ref = FMath::Max(0.01, P.RefDistance);
		const double Floor = 0.1; // |P_1| floor (-20 dB, audio.md 3.6 DirectivityFloorDb)

		auto SetPath = [&](FRadiationPath& Path, const FVector& C, const FVector& A)
		{
			const FVector D = Listener - C;
			const double Dist = FMath::Max(D.Length(), R + 1e-3);
			const double Cos = FVector::DotProduct(D / Dist, A);
			for (int32 Order = 0; Order <= 3; ++Order)
			{
				double W = Legendre(Order, Cos);
				if (Order == 1 && FMath::Abs(W) < Floor)
				{
					W = W < 0.0 ? -Floor : Floor;
				}
				Path.Weights[Order] = W;
			}
			Path.NearField = C0 / (Dist * SampleRate);
			return Dist;
		};
		const double Direct = SetPath(Ev.Paths[0], Centre, Axis);
		Ev.Paths[0].Gain = 1.0 / Ref;
		Ev.Paths[0].DelaySeconds = 0.0; // relative to the direct arrival (the voice carries no propagation delay)
		Ev.Paths[0].bReflected = false;
		// Image in the floor plane: centre mirrored through the contact plane, axis mirrored (points away from the floor).
		const FVector CentreImage = Contact - N * R;
		const double Image = SetPath(Ev.Paths[1], CentreImage, N);
		Ev.Paths[1].Gain = Direct / (Image * Ref);
		Ev.Paths[1].DelaySeconds = (Image - Direct) / C0;
		Ev.Paths[1].bReflected = true;
		Ev.Reflection = FReflection::Rigid(Reflection);
		Ev.NumPaths = 2;
		Ev.ListenerDistance = Direct;
		return Ev;
	}

	bool RenderFloorHit(const FRbFloorHitParams& P, double SampleRate, TArray<float>& Out, int32 PreFrames)
	{
		Out.Reset();
		if (!(P.NormalSpeed >= 0.003))
		{
			return false;
		}
		const RbAudio::FImpactEvent Ev = MakeFloorHitEvent(P, SampleRate);
		RbAudio::FImpactRenderer Renderer(SampleRate);
		int32 First = 0;
		int32 End = 0;
		Renderer.Extent(Ev, First, End);
		const int32 Pre = FMath::Max(PreFrames, -First + 1);
		const int32 Count = Pre + FMath::Max(End, 1);
		TArray<double> Buffer;
		Buffer.SetNumZeroed(Count);
		Renderer.Render(Ev, static_cast<double>(Pre), 0, TArrayView<double>(Buffer));
		Out.SetNumUninitialized(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			Out[I] = static_cast<float>(Buffer[I]);
		}
		return true;
	}

	void RenderFootstep(const RbAudio::FFootstepParams& Params, double RefDistance, double SampleRate, TArray<float>& Out)
	{
		RbAudio::FFootstepSynth::Render(Params, SampleRate, Out);
		const float Scale = static_cast<float>(1.0 / FMath::Max(0.01, RefDistance));
		for (float& S : Out)
		{
			S *= Scale;
		}
	}
}
