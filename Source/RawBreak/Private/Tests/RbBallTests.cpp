// Ball mesh and ball set tests (UE-2, Docs/ue-architecture.md 13): T20 sagitta bound, closed outward-wound unit sphere,
// UV layouts, per-ball radii incl. the oversized bar cue ball, exact table-local / world poses, MPC_RbBalls values.
// Owner: UE-2.

#include "Balls/RbBallMeshBuilder.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbBallTestSupport.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Tests/RbTestFlags.h"

#include "Components/StaticMeshComponent.h"
#include "Distance/DistPoint3Triangle3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Math/RandomStream.h"

#include "rb/Equipment/BallSets.h"

#if WITH_DEV_AUTOMATION_TESTS

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FIndex3i;

namespace
{
	// T20: silhouette sagitta of an N-segment great circle, e = R (1 - cos(pi / N)).
	double SagittaMm(double RadiusMm, int32 Segments)
	{
		return RadiusMm * (1.0 - FMath::Cos(UE_DOUBLE_PI / static_cast<double>(Segments)));
	}

	// Largest distance [unit radii] between the unit sphere and any point of the mesh surface (triangle interiors
	// included): 1 - min over triangles of the distance from the centre to the triangle.
	double MaxInwardDeviation(const FDynamicMesh3& Mesh)
	{
		double MinDistance = TNumericLimits<double>::Max();
		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(Tri, A, B, C);
			UE::Geometry::TDistPoint3Triangle3<double> Distance(FVector3d::ZeroVector, UE::Geometry::FTriangle3d(A, B, C));
			MinDistance = FMath::Min(MinDistance, Distance.Get());
		}
		return 1.0 - MinDistance;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbBallMeshT20Sagitta, "RawBreak.Unit.Balls.Mesh_T20_Sagitta", RB_UNIT_TEST_FLAGS)
bool FRbBallMeshT20Sagitta::RunTest(const FString& Parameters)
{
	// The T20 reference values (plan 13) and the bound UE-2 must meet.
	const double RMm = 1000.0 * rb::kStandardPoolBall.Radius;
	TestNearlyEqual(TEXT("T20 N = 32"), SagittaMm(RMm, 32), 0.13760, 1e-5);
	TestNearlyEqual(TEXT("T20 N = 64"), SagittaMm(RMm, 64), 0.03442, 1e-5);

	FDynamicMesh3 Mesh;
	const FRbBallMeshOptions Options;
	RbBallMeshBuilder::BuildUnitSphere(Options, Mesh);
	TestEqual(TEXT("default segments (plan 6.7)"), Options.Segments, 128);
	TestEqual(TEXT("triangle count"), Mesh.TriangleCount(), RbBallMeshBuilder::TriangleCount(Options));
	TestEqual(TEXT("vertex count"), Mesh.VertexCount(), 2 + (Options.Rings - 1) * Options.Segments);

	const double Deviation = MaxInwardDeviation(Mesh);
	const double DeviationMm = Deviation * RMm;
	AddInfo(FString::Printf(TEXT("max deviation from the sphere: %.5f mm (standard ball), %.5f mm (oversized cue ball)"), DeviationMm,
		Deviation * 1000.0 * rb::kOversizedCueBall.Radius));
	TestTrue(TEXT("sagitta < 0.035 mm at R (T20 bound, triangle interiors)"), DeviationMm < 0.035);
	TestTrue(TEXT("sagitta < 0.035 mm for the oversized cue ball"), Deviation * 1000.0 * rb::kOversizedCueBall.Radius < 0.035);
	// The silhouette chords along the equator are the N = 128 great-circle chords.
	TestTrue(TEXT("equator chord sagitta == e(128)"), DeviationMm >= SagittaMm(RMm, 128) - 1e-9);

	// The baked asset (when rb_bake_ball.py has run) has exactly the builder's triangles and unit radius.
	if (FPackageName::DoesPackageExist(RbAssetPaths::BallMesh))
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), RbAssetPaths::BallMesh, *FPackageName::GetShortName(RbAssetPaths::BallMesh));
		if (const UStaticMesh* Baked = LoadObject<UStaticMesh>(nullptr, *ObjectPath))
		{
			TestEqual(TEXT("baked triangle count"), Baked->GetNumTriangles(0), Mesh.TriangleCount());
			TestNearlyEqual(TEXT("baked radius [cm]"), Baked->GetBounds().BoxExtent.GetMax(), 1.0, 1e-5);
			TestFalse(TEXT("baked mesh without Nanite"), Baked->HasValidNaniteData());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbBallMeshTopology, "RawBreak.Unit.Balls.Mesh_ClosedOutward", RB_UNIT_TEST_FLAGS)
bool FRbBallMeshTopology::RunTest(const FString& Parameters)
{
	// Default and a small odd request (rounded to 32 x 8).
	const FRbBallMeshOptions Requests[2] = {FRbBallMeshOptions{}, FRbBallMeshOptions{30, 7}};
	for (const FRbBallMeshOptions& Request : Requests)
	{
		const FRbBallMeshOptions Used = RbBallMeshBuilder::Sanitize(Request);
		TestEqual(TEXT("segments multiple of 4"), Used.Segments % 4, 0);
		TestEqual(TEXT("rings even"), Used.Rings % 2, 0);
		FDynamicMesh3 Mesh;
		RbBallMeshBuilder::BuildUnitSphere(Request, Mesh);
		TestTrue(TEXT("closed (no boundary edge)"), Mesh.IsClosed());
		TestEqual(TEXT("Euler characteristic V - E + F = 2"), Mesh.VertexCount() - Mesh.EdgeCount() + Mesh.TriangleCount(), 2);
		TestEqual(TEXT("triangle count formula"), Mesh.TriangleCount(), RbBallMeshBuilder::TriangleCount(Request));

		double MaxRadiusError = 0.0;
		for (const int32 V : Mesh.VertexIndicesItr())
		{
			MaxRadiusError = FMath::Max(MaxRadiusError, FMath::Abs(Mesh.GetVertex(V).Length() - 1.0));
		}
		TestTrue(TEXT("every vertex on the unit sphere (1e-12)"), MaxRadiusError < 1e-12);

		int32 Inward = 0;
		int32 Degenerate = 0;
		const UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		double WorstNormal = 1.0;
		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			const FVector3d Centroid = Mesh.GetTriCentroid(Tri);
			const FVector3d Normal = Mesh.GetTriNormal(Tri); // UE winding convention (VectorUtil::Normal)
			if (Mesh.GetTriArea(Tri) < 1e-12)
			{
				++Degenerate;
			}
			if (Normal.Dot(Centroid) <= 0.0)
			{
				++Inward;
			}
			const FIndex3i Tv = Mesh.GetTriangle(Tri);
			const FIndex3i Tn = Normals->GetTriangle(Tri);
			for (int32 K = 0; K < 3; ++K)
			{
				WorstNormal = FMath::Min(WorstNormal, static_cast<double>(FVector3f::DotProduct(Normals->GetElement(Tn[K]), FVector3f(Mesh.GetVertex(Tv[K])))));
			}
		}
		TestEqual(TEXT("every triangle wound outward in UE space"), Inward, 0);
		TestEqual(TEXT("no degenerate triangle"), Degenerate, 0);
		TestTrue(TEXT("shading normals analytic (== position)"), WorstNormal > 1.0 - 1e-6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbBallMeshAttributes, "RawBreak.Unit.Balls.Mesh_UVsAndTangents", RB_UNIT_TEST_FLAGS)
bool FRbBallMeshAttributes::RunTest(const FString& Parameters)
{
	FDynamicMesh3 Mesh;
	RbBallMeshBuilder::BuildUnitSphere(FRbBallMeshOptions{}, Mesh);
	const UE::Geometry::FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();
	TestEqual(TEXT("two UV layers"), Attributes->NumUVLayers(), 2);
	TestTrue(TEXT("tangents"), Attributes->HasTangentSpace());
	const UE::Geometry::FDynamicMeshUVOverlay* UV0 = Attributes->GetUVLayer(0);
	const UE::Geometry::FDynamicMeshUVOverlay* UV1 = Attributes->GetUVLayer(1);
	const UE::Geometry::FDynamicMeshNormalOverlay* Normals = Attributes->PrimaryNormals();
	const UE::Geometry::FDynamicMeshNormalOverlay* Tangents = Attributes->PrimaryTangents();
	const UE::Geometry::FDynamicMeshNormalOverlay* Bitangents = Attributes->PrimaryBiTangents();

	int32 OutOfRange = 0;
	int32 BadDecode = 0;
	int32 Straddling = 0;
	double WorstOrtho = 0.0;
	int32 WrongHanded = 0;
	for (const int32 Tri : Mesh.TriangleIndicesItr())
	{
		const FIndex3i V = Mesh.GetTriangle(Tri);
		FVector2f Uv1[3];
		UV1->GetTriElements(Tri, Uv1[0], Uv1[1], Uv1[2]);
		FVector2f Uv0[3];
		UV0->GetTriElements(Tri, Uv0[0], Uv0[1], Uv0[2]);
		const FIndex3i N = Normals->GetTriangle(Tri);
		const FIndex3i T = Tangents->GetTriangle(Tri);
		const FIndex3i B = Bitangents->GetTriangle(Tri);
		for (int32 K = 0; K < 3; ++K)
		{
			for (const FVector2f& Uv : {Uv0[K], Uv1[K]})
			{
				OutOfRange += (Uv.X < 0.0f || Uv.X > 1.0f || Uv.Y < 0.0f || Uv.Y > 1.0f) ? 1 : 0;
			}
			// UV1 decodes back to the vertex direction (float storage).
			const FVector3d Decoded = RbBallMeshBuilder::OctahedralDecode(Uv1[K]);
			BadDecode += Decoded.Dot(Mesh.GetVertex(V[K])) < 1.0 - 1e-9 ? 1 : 0;
			// Orthonormal frame, bitangent = direction of increasing UV0.v (south).
			const FVector3f Nf = Normals->GetElement(N[K]);
			const FVector3f Tf = Tangents->GetElement(T[K]);
			const FVector3f Bf = Bitangents->GetElement(B[K]);
			WorstOrtho = FMath::Max(WorstOrtho, static_cast<double>(FMath::Abs(FVector3f::DotProduct(Nf, Tf))));
			WorstOrtho = FMath::Max(WorstOrtho, static_cast<double>(FMath::Abs(FVector3f::DotProduct(Nf, Bf))));
			WorstOrtho = FMath::Max(WorstOrtho, static_cast<double>(FMath::Abs(FVector3f::DotProduct(Tf, Bf))));
			WrongHanded += FVector3f::DotProduct(FVector3f::CrossProduct(Nf, Tf), Bf) < 0.0f ? 0 : 1; // N x T = north, B = south
		}
		// No triangle interpolates UV1 across an octahedral fold: its UV centroid decodes next to its own centroid.
		const FVector2f UvCentroid = (Uv1[0] + Uv1[1] + Uv1[2]) / 3.0f;
		const FVector3d Expected = Mesh.GetTriCentroid(Tri).GetSafeNormal();
		Straddling += RbBallMeshBuilder::OctahedralDecode(UvCentroid).Dot(Expected) < FMath::Cos(0.02) ? 1 : 0;
	}
	TestEqual(TEXT("UVs inside [0, 1]"), OutOfRange, 0);
	TestEqual(TEXT("UV1 decodes to the vertex direction"), BadDecode, 0);
	TestEqual(TEXT("no triangle straddles an octahedral fold"), Straddling, 0);
	TestTrue(TEXT("tangent frames orthonormal"), WorstOrtho < 1e-5);
	TestEqual(TEXT("bitangent = -(N x T) (UV0 v grows southward)"), WrongHanded, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbBallOctahedral, "RawBreak.Unit.Balls.Octahedral", RB_UNIT_TEST_FLAGS)
bool FRbBallOctahedral::RunTest(const FString& Parameters)
{
	FRandomStream Random(20260927);
	double Worst = 1.0;
	for (int32 I = 0; I < 2000; ++I)
	{
		const FVector3d D = FVector3d(Random.GetUnitVector());
		const FVector2f Uv = RbBallMeshBuilder::OctahedralEncode(D);
		Worst = FMath::Min(Worst, RbBallMeshBuilder::OctahedralDecode(Uv).Dot(D));
	}
	TestTrue(TEXT("round trip (float UV)"), Worst > 1.0 - 1e-10);
	// Landmarks: +z pole at the centre, the -z pole at the corners chosen by the fold signs, the equator on the diamond.
	TestTrue(TEXT("+z -> centre"), RbBallMeshBuilder::OctahedralEncode(FVector3d(0, 0, 1)).Equals(FVector2f(0.5f, 0.5f)));
	TestTrue(TEXT("-z -> corner (+,+)"), RbBallMeshBuilder::OctahedralEncode(FVector3d(0, 0, -1), 1.0, 1.0).Equals(FVector2f(1.0f, 1.0f)));
	TestTrue(TEXT("-z -> corner (-,+)"), RbBallMeshBuilder::OctahedralEncode(FVector3d(0, 0, -1), -1.0, 1.0).Equals(FVector2f(0.0f, 1.0f)));
	TestTrue(TEXT("+x -> right"), RbBallMeshBuilder::OctahedralEncode(FVector3d(1, 0, 0)).Equals(FVector2f(1.0f, 0.5f)));
	TestTrue(TEXT("-y -> bottom"), RbBallMeshBuilder::OctahedralEncode(FVector3d(0, -1, 0)).Equals(FVector2f(0.5f, 0.0f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbBallSetComponents, "RawBreak.Unit.Balls.Set_RadiiAndComponents", RB_UNIT_TEST_FLAGS)
bool FRbBallSetComponents::RunTest(const FString& Parameters)
{
	RbBallTest::FTestWorld TestWorld;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(TestWorld.World);
	if (!TestNotNull(TEXT("table"), Scene.Table) || !TestNotNull(TEXT("ball set"), Scene.Balls) || !TestTrue(TEXT("context"), Scene.Table->HasContext()))
	{
		return false;
	}
	const FRbTableContext& Context = Scene.Table->GetContext();
	TestEqual(TEXT("16 balls (standard pool set)"), Scene.Balls->GetBallCount(), 16);
	TestTrue(TEXT("attached to the table's ClothOrigin"), Scene.Balls->GetRootComponent()->GetAttachParent() == Scene.Table->GetClothOrigin());
	TestTrue(TEXT("ball set root at the cloth origin"), Scene.Balls->GetActorTransform().Equals(Scene.Table->GetTableToWorld(), 1e-9));
	UStaticMesh* Mesh = Scene.Balls->GetBallMesh();
	if (!TestNotNull(TEXT("ball mesh"), Mesh))
	{
		return false;
	}
	const double MeshRadius = Mesh->GetBounds().BoxExtent.GetMax();
	AddInfo(FString::Printf(TEXT("ball mesh %s (radius %.3f cm)"), *Mesh->GetPathName(), MeshRadius));
	for (int32 Id = 0; Id < Scene.Balls->GetBallCount(); ++Id)
	{
		UStaticMeshComponent* Ball = Scene.Balls->GetBallComponent(Id);
		if (!TestNotNull(TEXT("component"), Ball))
		{
			continue;
		}
		const double Expected = 100.0 * Context.BallRadius(Id);
		TestNearlyEqual(FString::Printf(TEXT("ball %d radius [cm]"), Id), Scene.Balls->GetBallRadiusCm(Id), Expected, 1e-12);
		TestNearlyEqual(FString::Printf(TEXT("ball %d rendered radius [cm]"), Id), Ball->GetRelativeScale3D().X * MeshRadius, Expected, 1e-9);
		TestTrue(TEXT("uniform scale"), Ball->GetRelativeScale3D().AllComponentsEqual(1e-12));
		TestTrue(TEXT("movable"), Ball->Mobility == EComponentMobility::Movable);
		TestTrue(TEXT("no collision (plan pitfall 23)"), Ball->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestFalse(TEXT("hidden until shown"), Scene.Balls->IsBallVisible(Id));
		TestTrue(TEXT("attached to the ball set"), Ball->GetAttachParent() == Scene.Balls->GetRootComponent());
		UMaterialInstanceDynamic* Mid = Scene.Balls->GetBallMaterial(Id);
		if (TestNotNull(TEXT("MID per ball"), Mid))
		{
			TestTrue(TEXT("MID is the component's material"), Ball->GetMaterial(0) == Mid);
			FLinearColor Color;
			if (Mid->GetVectorParameterValue(FHashedMaterialParameterInfo(RbAssetPaths::Param::BallColor), Color))
			{
				TestTrue(TEXT("BallColor = WPA colour"), Color.Equals(ARbBallSet::DefaultBallColor(Id)));
			}
			float Number = -1.0f;
			if (Mid->GetScalarParameterValue(FHashedMaterialParameterInfo(RbAssetPaths::Param::BallNumber), Number))
			{
				TestEqual(TEXT("BallNumber"), Number, static_cast<float>(Id));
			}
		}
	}
	// Stripes share the colour of number - 8; solids, the 8 and the cue ball are distinct.
	for (int32 N = 9; N <= 15; ++N)
	{
		TestTrue(TEXT("stripe colour"), ARbBallSet::DefaultBallColor(N).Equals(ARbBallSet::DefaultBallColor(N - 8)));
	}
	TestFalse(TEXT("8 is not the cue ball"), ARbBallSet::DefaultBallColor(8).Equals(ARbBallSet::DefaultBallColor(0), 0.1f));

	// Re-initialising rebuilds (no duplicates).
	Scene.Balls->InitForTable(Scene.Table);
	TestEqual(TEXT("re-init keeps 16 balls"), Scene.Balls->GetBallCount(), 16);
	int32 Meshes = 0;
	Scene.Balls->ForEachComponent<UStaticMeshComponent>(false, [&Meshes](UStaticMeshComponent* C) { Meshes += IsValid(C) ? 1 : 0; });
	TestEqual(TEXT("no stale ball components after re-init"), Meshes, 16);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbBallSetOversized, "RawBreak.Unit.Balls.Set_OversizedBarCueBall", RB_UNIT_TEST_FLAGS)
bool FRbBallSetOversized::RunTest(const FString& Parameters)
{
	RbBallTest::FTestWorld TestWorld;
	const RbBallTest::FTableWithBalls Scene =
		RbBallTest::SpawnTableWithBalls(TestWorld.World, ERbTablePreset::SevenFootBar, ERbBallSetPreset::OldBarOversizedCue, 42);
	if (!TestNotNull(TEXT("ball set"), Scene.Balls) || !TestTrue(TEXT("context"), Scene.Table && Scene.Table->HasContext()))
	{
		return false;
	}
	rb::BallSet Expected;
	TestTrue(TEXT("BuildBallSet"), rb::Succeeded(rb::BuildBallSet(rb::BallSetPreset::OldBarOversizedCue, 42, Expected)));
	TestEqual(TEXT("16 balls"), Scene.Balls->GetBallCount(), 16);
	TestNearlyEqual(TEXT("oversized cue ball 60.325 mm"), Scene.Balls->GetBallRadiusCm(0), 100.0 * rb::kOversizedCueBall.Radius, 1e-12);
	const double MeshRadius = Scene.Balls->GetBallMesh()->GetBounds().BoxExtent.GetMax();
	bool bAllExact = true;
	bool bSomeDiffer = false;
	for (int32 Id = 0; Id < 16; ++Id)
	{
		const double R = 100.0 * Expected.Balls[Id].Radius;
		bAllExact &= FMath::Abs(Scene.Balls->GetBallComponent(Id)->GetRelativeScale3D().X * MeshRadius - R) < 1e-9;
		bSomeDiffer |= Id > 1 && Expected.Balls[Id].Radius != Expected.Balls[1].Radius;
	}
	TestTrue(TEXT("every ball rendered at its own radius (seeded bar set)"), bAllExact);
	TestTrue(TEXT("the bar set really has per-ball radii"), bSomeDiffer);
	TestTrue(TEXT("cue ball larger than every object ball"), Scene.Balls->GetBallRadiusCm(0) > Scene.Balls->GetBallRadiusCm(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbBallSetPoses, "RawBreak.Unit.Balls.Set_ShowSimBallsPoses", RB_UNIT_TEST_FLAGS)
bool FRbBallSetPoses::RunTest(const FString& Parameters)
{
	RbBallTest::FTestWorld TestWorld;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(TestWorld.World);
	if (!TestNotNull(TEXT("ball set"), Scene.Balls) || !TestTrue(TEXT("context"), Scene.Table && Scene.Table->HasContext()))
	{
		return false;
	}
	FRandomStream Random(7);
	rb::SimInput Input;
	RbShot::InitSimInput(Scene.Table->GetContext(), Input);
	for (int32 Id = 0; Id < 16; ++Id)
	{
		rb::SimBall& Ball = Input.Balls[Id];
		Ball.InPlay = Id % 5 != 3; // 3, 8 and 13 not in play
		Ball.State.Position = rb::Vec3(Random.FRandRange(-1.2, 1.2), Random.FRandRange(-0.6, 0.6), Ball.Spec.Radius);
		Ball.Orientation = rb::Normalized(rb::Quat(Random.FRandRange(-1, 1), Random.FRandRange(-1, 1), Random.FRandRange(-1, 1), Random.FRandRange(-1, 1)));
	}
	// Near the FRotator singularity (pitch 90 deg in UE): the pose must still be exact.
	Input.Balls[4].Orientation = rb::Normalized(rb::Quat(FMath::Sqrt(0.5), 0.0, FMath::Sqrt(0.5) - 1e-9, 0.0));
	Scene.Balls->ShowSimBalls(Input.Balls, rb::kMaxBalls);

	for (int32 Id = 0; Id < 16; ++Id)
	{
		const rb::SimBall& Sim = Input.Balls[Id];
		UStaticMeshComponent* Ball = Scene.Balls->GetBallComponent(Id);
		TestEqual(FString::Printf(TEXT("ball %d visible == in play"), Id), Scene.Balls->IsBallVisible(Id), Sim.InPlay);
		if (!Sim.InPlay)
		{
			continue;
		}
		const FVector Local = FRbCoords::PositionToUE(Sim.State.Position);
		TestTrue(TEXT("table-local position exact"), Ball->GetRelativeLocation() == Local);
		const FQuat LocalQ = Ball->GetRelativeTransform().GetRotation();
		TestTrue(TEXT("table-local orientation exact (no FRotator round trip)"), LocalQ.Equals(FRbCoords::OrientationToUE(Sim.Orientation).GetNormalized(), 1e-15));
		TestTrue(TEXT("world position == ARbTable::CoreToWorld (1e-6 cm)"),
			Ball->GetComponentLocation().Equals(Scene.Table->CoreToWorld(Sim.State.Position), 1e-6));
		TestTrue(TEXT("world orientation == ARbTable::CoreOrientationToWorld"),
			Ball->GetComponentQuat().AngularDistance(Scene.Table->CoreOrientationToWorld(Sim.Orientation)) < 1e-9);
		TestTrue(TEXT("stored orientation"), Scene.Balls->GetBallOrientationUE(Id).Equals(FRbCoords::OrientationToUE(Sim.Orientation), 0.0));
	}
	// Spin -> ball-local omega (rotation smear input): q^-1 w in UE axes.
	const rb::Vec3 Omega(3.0, -20.0, 7.0);
	Scene.Balls->SetBallSpinCore(1, Omega);
	FLinearColor Local;
	if (Scene.Balls->GetBallMaterial(1)->GetVectorParameterValue(FHashedMaterialParameterInfo(RbAssetPaths::Param::BallOmegaLocal), Local))
	{
		const FVector Expected = FRbCoords::OrientationToUE(Input.Balls[1].Orientation).UnrotateVector(FRbCoords::AngularVelocityToUE(Omega));
		TestTrue(TEXT("BallOmegaLocal = q^-1 w"), FVector(Local.R, Local.G, Local.B).Equals(Expected, 1e-4));
	}
	// A hidden ball comes back.
	Input.Balls[3].InPlay = true;
	Scene.Balls->ShowSimBalls(Input.Balls, rb::kMaxBalls);
	TestTrue(TEXT("re-shown"), Scene.Balls->IsBallVisible(3));
	Scene.Balls->SetBallVisible(3, false);
	TestFalse(TEXT("SetBallVisible(false)"), Scene.Balls->IsBallVisible(3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbBallSetMpc, "RawBreak.Unit.Balls.Set_OcclusionMpc", RB_UNIT_TEST_FLAGS)
bool FRbBallSetMpc::RunTest(const FString& Parameters)
{
	RbBallTest::FTestWorld TestWorld;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(TestWorld.World);
	if (!TestNotNull(TEXT("ball set"), Scene.Balls) || !TestTrue(TEXT("context"), Scene.Table && Scene.Table->HasContext()))
	{
		return false;
	}
	UMaterialParameterCollection* Mpc = RbBallTest::MakeBallMpc();
	Scene.Balls->SetOcclusionCollection(Mpc);
	UMaterialParameterCollectionInstance* Instance = TestWorld.World->GetParameterCollectionInstance(Mpc);
	if (!TestNotNull(TEXT("MPC instance"), Instance))
	{
		return false;
	}
	float Radius = 0.0f;
	TestTrue(TEXT("BallRadiusCm written"), Instance->GetScalarParameterValue(RbAssetPaths::Param::BallRadiusCm, Radius));
	TestNearlyEqual(TEXT("BallRadiusCm = object-ball radius"), static_cast<double>(Radius), 2.8575, 1e-5);

	rb::SimInput Input;
	RbShot::InitSimInput(Scene.Table->GetContext(), Input);
	for (int32 Id = 0; Id < 16; ++Id)
	{
		Input.Balls[Id].InPlay = Id != 5;
		Input.Balls[Id].State.Position = rb::Vec3(-1.0 + 0.13 * Id, 0.3 - 0.04 * Id, Input.Balls[Id].Spec.Radius);
	}
	Input.Balls[9].State.Position.z = -0.01; // a ball below the bed plane (in a pocket) does not occlude the cloth
	Scene.Balls->ShowSimBalls(Input.Balls, rb::kMaxBalls);
	for (int32 Id = 0; Id < 16; ++Id)
	{
		FLinearColor Value;
		if (!TestTrue(*FString::Printf(TEXT("Ball%02d written"), Id), Instance->GetVectorParameterValue(RbAssetPaths::Param::MpcBall(Id), Value)))
		{
			continue;
		}
		const FVector World = Scene.Table->CoreToWorld(Input.Balls[Id].State.Position);
		if (Id != 5)
		{
			TestTrue(FString::Printf(TEXT("Ball%02d = world centre [cm]"), Id), FVector(Value.R, Value.G, Value.B).Equals(World, 1e-3));
		}
		const float ExpectedW = (Id == 5 || Id == 9) ? 0.0f : 1.0f;
		TestEqual(FString::Printf(TEXT("Ball%02d W (on the cloth)"), Id), Value.A, ExpectedW);
	}
	// Moving and hiding a ball updates its entry.
	Scene.Balls->SetBallCore(2, rb::Vec3(0.5, -0.25, Input.Balls[2].Spec.Radius), rb::Quat::Identity());
	FLinearColor Moved;
	Instance->GetVectorParameterValue(RbAssetPaths::Param::MpcBall(2), Moved);
	TestTrue(TEXT("moved ball"), FVector(Moved.R, Moved.G, Moved.B).Equals(Scene.Table->CoreToWorld(rb::Vec3(0.5, -0.25, Input.Balls[2].Spec.Radius)), 1e-3));
	Scene.Balls->SetBallVisible(2, false);
	Instance->GetVectorParameterValue(RbAssetPaths::Param::MpcBall(2), Moved);
	TestEqual(TEXT("hidden ball W = 0"), Moved.A, 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
