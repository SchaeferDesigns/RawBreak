#pragma once

// Pure venue-lighting maths of the dive bar (Docs/specs/venue-dive-bar.md 4.1-4.7; Docs/ue-architecture.md 18.8): lighting-state
// ramps, the photosensitivity-safe light animations (TV content, jukebox colour cycle, dart-machine chase, passing headlights),
// the Harding-style flash counter (VDB-T8), the analytic 3-shade lamp model of 4.4 (lux probe, VDB-T1), neon proxy flux (4.3) and
// the exposure conversions of the EV report (VDB-T2). No UObjects: unit-testable (RawBreak.Unit.Venue.*). Owner: M2-A.

#include "CoreMinimal.h"

#include "RbVenueLighting.generated.h"

UENUM(BlueprintType)
enum class ERbVenueLightAnimation : uint8
{
	None,
	Tv,         // TV content: smooth band-limited brightness noise (depth, rate), never a flash (4.2 L16 / L17, R10)
	Cycle,      // slow smooth colour cycle through CycleColors, period >= 4 s (jukebox panels, L18)
	Chase,      // attract-mode LED chase: <= 2 steps / s, whole-marquee change < 10 % per step (dart machine, L19)
	Headlights, // passing car: a sweep of 1.5-3 s every 20-120 s (seeded), smooth envelope (L35)
};

// One venue light as the level generator describes it (from Art/DiveBar/lights.json). The light actor carries the tag
// "LT_DB_<Id>" (RbVenueLighting::LightTag); its saved intensity is Intensity * OpenFactor.
USTRUCT(BlueprintType)
struct RAWBREAK_API FRbVenueLight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	FName Group;

	// Full photometric intensity in the light's units (lumens for the dive bar), i.e. the value at state factor 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float Intensity = 0.0f;

	// Intensity factor per lighting state (ERbLightingState order: Open, LightsUp, AfterHours).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float OpenFactor = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float LightsUpFactor = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float AfterHoursFactor = 0.0f;

	// Ramp time of a state change [s] (>= RbVenueLighting::MinRampSeconds; troffer ballast warm-up 0.8 s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float RampSeconds = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	ERbVenueLightAnimation Animation = ERbVenueLightAnimation::None;

	// Tv / Chase: modulation depth (fraction of the intensity); Cycle: unused.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float AnimDepth = 0.0f;

	// Tv: noise rate [Hz]; Cycle: period [s]; Chase: steps per second.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float AnimRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	int32 AnimSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	TArray<FLinearColor> CycleColors;

	// Headlights: interval between sweeps [s] (min, max), sweep duration [s] (min, max), yaw range of the sweep [deg].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	FVector2D IntervalRange = FVector2D(20.0, 120.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	FVector2D SweepRange = FVector2D(1.5, 3.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	FVector2D YawRange = FVector2D(-35.0, 45.0);

	// Neon proxy (4.3): the tubes' total flux [lm]; the proxy's flux must be TubeFlux / pi (VDB-T11). 0 = not a neon proxy.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float TubeFluxLm = 0.0f;

	// Outside the room (street) / inside an enclosure (coolers, TVs, jukebox, ...): volumetric scattering must be 0 (4.1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	bool bOutside = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	bool bEnclosed = false;

	// Actors with this tag carry the light's emissive surface (troffer lenses): their MIDs' "Emissive" follows the factor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	FName EmissiveActorTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float EmissiveNits = 0.0f;

	float FactorFor(uint8 State) const;
};

// A lighting-state ramp of one light (4.6): smoothstep from the current value to the target over >= MinRampSeconds, so no light
// ever steps within a frame (VDB-T12). Plain value type (ARbVenueInfo keeps one per light; unit tests drive it).
struct RAWBREAK_API FRbLightRamp
{
	float From = 1.0f;
	float To = 1.0f;
	float Current = 1.0f;
	float Time = 0.0f;
	float Duration = 0.0f;

	// Starts a ramp from Current to Target over Seconds (clamped to >= RbVenueLighting::MinRampSeconds); no-op at the target.
	void Start(float Target, float Seconds);
	// Sets the value without a ramp (initial state at load only).
	void Snap(float Value);
	// Advances by Dt [s] and returns Current.
	float Advance(float Dt);
	bool IsActive() const { return Duration > 0.0f; }
};

// Point of the 4.4 lux table with its band.
struct FRbLuxPoint
{
	const TCHAR* Name = TEXT("");
	FVector3d Core = FVector3d::ZeroVector; // core table frame [m] (z above the cloth)
	double BandMin = 0.0;
	double BandMax = 0.0;
	double Lux = 0.0;                       // filled by the probe
};

// Lamp model of 4.4: a bulb + shade = I(theta) = I0 cos(theta) for theta <= CutoffDeg, 0 beyond.
struct FRbLampModel
{
	TArray<FVector3d> BulbsCore;  // bulb centres, core table frame [m] (z above the cloth)
	double FluxLm = 1100.0;
	double Efficiency = 0.60;
	double CutoffDeg = 50.2;

	double I0() const;            // eta Phi / (pi (1 - cos^2 theta_c)) = 355.9 cd for the dive bar
	double IlluminanceAt(const FVector3d& CorePoint, const FVector3d& Normal = FVector3d::UnitZ()) const;
};

namespace RbVenueLighting
{
	inline constexpr float MinRampSeconds = 0.8f;       // 4.6: no light steps on or off within a frame
	inline constexpr double MaxFlashesPerSecond = 3.0;   // 4.7 / VDB-T8
	inline constexpr double FlashThreshold = 0.10;       // a luminance change >= 10 % of the maximum counts (Harding-style)

	// "LT_DB_<Id>" (light actor tag and label, venue-dive-bar 13.3).
	RAWBREAK_API FName LightTag(const FName& Id);
	// Every generated venue light actor also carries this tag.
	RAWBREAK_API FName VenueLightTag();

	// Smooth ramp value in [0, 1] at T of a ramp of Duration (smoothstep; Duration <= 0 -> 1).
	RAWBREAK_API double Ramp01(double T, double Duration);

	// Intensity multiplier of an animation at time T [s] (>= 0; 1 = unmodulated). Deterministic in (Light, T).
	RAWBREAK_API double AnimationFactor(const FRbVenueLight& Light, double T);
	// Colour of a Cycle animation at T (white for other kinds).
	RAWBREAK_API FLinearColor AnimationColor(const FRbVenueLight& Light, double T);
	// Headlights: yaw offset [deg] of the sweep at T (0 outside a sweep) and whether a sweep is active.
	RAWBREAK_API double HeadlightYaw(const FRbVenueLight& Light, double T, bool* bOutActive = nullptr);

	// Harding-style flash count: a flash is a pair of opposing luminance changes, each >= Threshold of the sequence's maximum;
	// returns the largest number of flashes inside any 1-s window of the sequence sampled at SampleRateHz.
	RAWBREAK_API double MaxFlashesPerSecondOf(const TArray<double>& Luminance, double SampleRateHz, double Threshold = FlashThreshold);

	// Neon (4.3): tube flux Phi = pi^2 L d l [lm] of a tube of luminance L [cd/m^2], diameter d and length l [m]; the proxy rect
	// light that gives the same on-axis intensity has Phi / pi.
	RAWBREAK_API double TubeFlux(double LuminanceNits, double DiameterM, double LengthM);
	RAWBREAK_API double NeonProxyFlux(double TubeFluxLm);

	// The 4.4 band points of a table of the given playfield half extents / rail width [m] (bed z = 0, rails at RailTopZ).
	RAWBREAK_API TArray<FRbLuxPoint> LuxBandPoints(double HalfLength, double HalfWidth, double RailWidth, double RailTopZ);

	// Metered scene EV100 of an average scene luminance [cd/m^2] (EV100 = log2(8 L), the spec's convention: cloth 39.5 -> 8.30),
	// and the camera EV100 of an eye-adaptation exposure multiplier (UE: exposure = 1 / 2^EV100 with the extended range).
	RAWBREAK_API double SceneEv100(double AverageLuminance);
	RAWBREAK_API double CameraEv100(double Exposure);

	// Night look of the venue grade (ARbVenueInfo's exposure compensation curve, UE "Exposure Compensation Curve": x = the metered
	// scene EV100, y = compensation [EV]). The eye does not adapt completely in mesopic light (a room at 2 cd/m^2 still looks dark
	// after minutes; Hunt / Bartleson-Breneman), so a scene metered at the dive bar's EV100 3-5 renders 1.0-0.55 EV below mid grey
	// while the lamp-lit cloth (EV100 >= 8) stays untouched. The metered EV of the report (VDB-T2) is unaffected; the pixel
	// exposure is. Keys sorted by x; clamped outside.
	RAWBREAK_API const TArray<FVector2D>& NightCompensationKeys();
	RAWBREAK_API double NightCompensation(double SceneEv);
	// UE evaluates the exposure compensation curve at log2(L / 0.18) (PostProcessEyeAdaptation.cpp, lens attenuation 0.78), not at
	// the spec's log2(8 L): the curve's x of a scene EV100.
	RAWBREAK_API double UeCurveX(double SceneEv);
}
