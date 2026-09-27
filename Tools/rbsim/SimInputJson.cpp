// Owner: WP-7 (output, playback & tools). Schema: Docs/architecture.md sections 5.3 and 14 (rbsimInput v1).
#include "SimInputJson.h"

#include "rb/Core/Constants.h"
#include "rb/Core/Error.h"
#include "rb/Physics/ParamTable.h"
#include "rb/Version.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace rbsim
{
	namespace
	{
		struct NamedState
		{
			const char* Name;
			rb::MotionState State;
		};

		constexpr NamedState kStates[] = {
			{"Stationary", rb::MotionState::Stationary},
			{"Spinning", rb::MotionState::Spinning},
			{"Sliding", rb::MotionState::Sliding},
			{"Rolling", rb::MotionState::Rolling},
			{"Airborne", rb::MotionState::Airborne},
			{"PocketPivot", rb::MotionState::PocketPivot},
			{"PocketFall", rb::MotionState::PocketFall},
			{"Pocketed", rb::MotionState::Pocketed},
			{"OffTable", rb::MotionState::OffTable},
		};

		void WriteVec3(JsonWriter& J, const char* Key, const rb::Vec3& V)
		{
			const double Values[3] = {V.x, V.y, V.z};
			J.Key(Key);
			J.RealArray(Values, 3);
		}

		void WriteVec2(JsonWriter& J, const char* Key, const rb::Vec2& V)
		{
			const double Values[2] = {V.x, V.y};
			J.Key(Key);
			J.RealArray(Values, 2);
		}

		void WriteTableSpec(JsonWriter& J, const rb::TableSpec& S)
		{
			J.BeginObject();
			J.Field("name", S.Name != nullptr ? S.Name : "");
			J.FieldInt("preset", static_cast<int>(S.Preset));
			J.FieldReal("length", S.Length);
			J.FieldReal("width", S.Width);
			J.FieldReal("bedHeight", S.BedHeight);
			J.FieldReal("cushionNoseHeight", S.CushionNoseHeight);
			J.FieldReal("cushionWidth", S.CushionWidth);
			J.FieldReal("cushionNoseProfileRadius", S.CushionNoseProfileRadius);
			J.FieldReal("railWidthTotal", S.RailWidthTotal);
			J.FieldReal("railTopZ", S.RailTopZ);
			J.FieldReal("slateThickness", S.SlateThickness);
			J.FieldReal("sightInset", S.SightInset);
			J.FieldReal("sightDiameter", S.SightDiameter);
			const rb::PocketSpec* Pockets[2] = {&S.Corner, &S.Side};
			const char* PocketKeys[2] = {"corner", "side"};
			for (int k = 0; k < 2; ++k)
			{
				J.Key(PocketKeys[k]);
				J.BeginObject();
				J.FieldReal("mouth", Pockets[k]->Mouth);
				J.FieldReal("cutAngle", Pockets[k]->CutAngle);
				J.FieldReal("shelf", Pockets[k]->Shelf);
				J.FieldReal("jawRadius", Pockets[k]->JawRadius);
				J.FieldReal("captureRadius", Pockets[k]->CaptureRadius);
				J.EndObject();
			}
			J.FieldReal("backdraft", S.Backdraft);
			J.FieldReal("dropPointRadius", S.DropPointRadius);
			J.FieldReal("facingThickness", S.FacingThickness);
			J.FieldReal("linerUndercut", S.LinerUndercut);
			J.FieldBool("hasPockets", S.HasPockets);
			J.FieldInt("cloth", static_cast<int>(S.Cloth));
			J.FieldReal("facingRestitutionScale", S.FacingRestitutionScale);
			J.FieldReal("linerRestitution", S.LinerRestitution);
			J.FieldReal("linerFriction", S.LinerFriction);
			J.EndObject();
		}

		void WriteState(JsonWriter& J, const rb::BallState& S)
		{
			J.BeginObject();
			WriteVec3(J, "r", S.Position);
			WriteVec3(J, "v", S.Velocity);
			WriteVec3(J, "w", S.Omega);
			J.Field("state", MotionStateName(S.State));
			J.EndObject();
		}

		// ------------------------------------------------------------------------------------------------
		// Reading
		// ------------------------------------------------------------------------------------------------
		class Reader
		{
		public:
			explicit Reader(std::string& InError)
				: Error(InError)
			{
			}

			bool Fail(const std::string& Path, const char* What)
			{
				if (Error.empty())
				{
					Error = Path + ": " + What;
				}
				return false;
			}

			const JsonValue* FindMember(const JsonValue& Obj, const char* Key, const std::string& Path)
			{
				const JsonValue* V = Obj.Find(Key);
				if (V == nullptr)
				{
					Fail(Path + "." + Key, "missing");
				}
				return V;
			}

			static bool AsReal(const JsonValue& V, double& Out)
			{
				if (V.IsNumber())
				{
					Out = V.NumberValue;
					return true;
				}
				if (V.IsString())
				{
					if (V.StringValue == "Infinity")
					{
						Out = std::numeric_limits<double>::infinity();
						return true;
					}
					if (V.StringValue == "-Infinity")
					{
						Out = -std::numeric_limits<double>::infinity();
						return true;
					}
					if (V.StringValue == "NaN")
					{
						Out = std::numeric_limits<double>::quiet_NaN();
						return true;
					}
				}
				return false;
			}

			bool ReadReal(const JsonValue& Obj, const char* Key, double& Out, const std::string& Path)
			{
				const JsonValue* V = FindMember(Obj, Key, Path);
				if (V == nullptr)
				{
					return false;
				}
				return AsReal(*V, Out) || Fail(Path + "." + Key, "expected a number");
			}

			bool ReadBool(const JsonValue& Obj, const char* Key, bool& Out, const std::string& Path)
			{
				const JsonValue* V = FindMember(Obj, Key, Path);
				if (V == nullptr)
				{
					return false;
				}
				if (!V->IsBool())
				{
					return Fail(Path + "." + Key, "expected true or false");
				}
				Out = V->BoolValue;
				return true;
			}

			bool ReadInt(const JsonValue& Obj, const char* Key, long long Min, long long Max, long long& Out, const std::string& Path)
			{
				const JsonValue* V = FindMember(Obj, Key, Path);
				if (V == nullptr)
				{
					return false;
				}
				if (!V->IsNumber() || std::floor(V->NumberValue) != V->NumberValue || V->NumberValue < static_cast<double>(Min)
					|| V->NumberValue > static_cast<double>(Max))
				{
					return Fail(Path + "." + Key, "expected an integer in range");
				}
				Out = static_cast<long long>(V->NumberValue);
				return true;
			}

			bool ReadReals(const JsonValue& Obj, const char* Key, double* Out, int Count, const std::string& Path)
			{
				const JsonValue* V = FindMember(Obj, Key, Path);
				if (V == nullptr)
				{
					return false;
				}
				if (!V->IsArray() || static_cast<int>(V->Items.size()) != Count)
				{
					return Fail(Path + "." + Key, "expected an array of numbers of the right length");
				}
				for (int i = 0; i < Count; ++i)
				{
					if (!AsReal(V->Items[static_cast<std::size_t>(i)], Out[i]))
					{
						return Fail(Path + "." + Key, "expected numbers");
					}
				}
				return true;
			}

			bool ReadVec3(const JsonValue& Obj, const char* Key, rb::Vec3& Out, const std::string& Path)
			{
				double V[3] = {};
				if (!ReadReals(Obj, Key, V, 3, Path))
				{
					return false;
				}
				Out = {V[0], V[1], V[2]};
				return true;
			}

			bool ReadVec2(const JsonValue& Obj, const char* Key, rb::Vec2& Out, const std::string& Path)
			{
				double V[2] = {};
				if (!ReadReals(Obj, Key, V, 2, Path))
				{
					return false;
				}
				Out = {V[0], V[1]};
				return true;
			}

			const JsonValue* ReadObject(const JsonValue& Parent, const char* Key, const std::string& Path)
			{
				const JsonValue* V = FindMember(Parent, Key, Path);
				if (V != nullptr && !V->IsObject())
				{
					Fail(Path + "." + Key, "expected an object");
					return nullptr;
				}
				return V;
			}

			const JsonValue* ReadArray(const JsonValue& Parent, const char* Key, const std::string& Path)
			{
				const JsonValue* V = FindMember(Parent, Key, Path);
				if (V != nullptr && !V->IsArray())
				{
					Fail(Path + "." + Key, "expected an array");
					return nullptr;
				}
				return V;
			}

			std::string& Error;
		};

		bool ReadTableSpec(Reader& R, const JsonValue& T, LoadedSimInput& Out)
		{
			const std::string P = "tableSpec";
			const JsonValue* Name = R.FindMember(T, "name", P);
			if (Name == nullptr || !Name->IsString())
			{
				return R.Fail(P + ".name", "expected a string");
			}
			Out.TableName = Name->StringValue;
			rb::TableSpec& S = Out.Geometry.Spec;
			S = rb::TableSpec{};
			long long Preset = 0;
			long long Cloth = 0;
			bool Ok = R.ReadInt(T, "preset", 0, static_cast<long long>(rb::TablePreset::Custom), Preset, P) && R.ReadReal(T, "length", S.Length, P)
				&& R.ReadReal(T, "width", S.Width, P) && R.ReadReal(T, "bedHeight", S.BedHeight, P) && R.ReadReal(T, "cushionNoseHeight", S.CushionNoseHeight, P)
				&& R.ReadReal(T, "cushionWidth", S.CushionWidth, P) && R.ReadReal(T, "cushionNoseProfileRadius", S.CushionNoseProfileRadius, P)
				&& R.ReadReal(T, "railWidthTotal", S.RailWidthTotal, P) && R.ReadReal(T, "railTopZ", S.RailTopZ, P)
				&& R.ReadReal(T, "slateThickness", S.SlateThickness, P) && R.ReadReal(T, "sightInset", S.SightInset, P)
				&& R.ReadReal(T, "sightDiameter", S.SightDiameter, P);
			if (!Ok)
			{
				return false;
			}
			rb::PocketSpec* Pockets[2] = {&S.Corner, &S.Side};
			const char* PocketKeys[2] = {"corner", "side"};
			for (int k = 0; k < 2; ++k)
			{
				const std::string PP = P + "." + PocketKeys[k];
				const JsonValue* Pocket = R.ReadObject(T, PocketKeys[k], P);
				if (Pocket == nullptr)
				{
					return false;
				}
				if (!(R.ReadReal(*Pocket, "mouth", Pockets[k]->Mouth, PP) && R.ReadReal(*Pocket, "cutAngle", Pockets[k]->CutAngle, PP)
						&& R.ReadReal(*Pocket, "shelf", Pockets[k]->Shelf, PP) && R.ReadReal(*Pocket, "jawRadius", Pockets[k]->JawRadius, PP)
						&& R.ReadReal(*Pocket, "captureRadius", Pockets[k]->CaptureRadius, PP)))
				{
					return false;
				}
			}
			Ok = R.ReadReal(T, "backdraft", S.Backdraft, P) && R.ReadReal(T, "dropPointRadius", S.DropPointRadius, P)
				&& R.ReadReal(T, "facingThickness", S.FacingThickness, P) && R.ReadReal(T, "linerUndercut", S.LinerUndercut, P)
				&& R.ReadBool(T, "hasPockets", S.HasPockets, P) && R.ReadInt(T, "cloth", 0, static_cast<long long>(rb::ClothPreset::NappedBar), Cloth, P)
				&& R.ReadReal(T, "facingRestitutionScale", S.FacingRestitutionScale, P) && R.ReadReal(T, "linerRestitution", S.LinerRestitution, P)
				&& R.ReadReal(T, "linerFriction", S.LinerFriction, P);
			if (!Ok)
			{
				return false;
			}
			S.Preset = static_cast<rb::TablePreset>(Preset);
			S.Cloth = static_cast<rb::ClothPreset>(Cloth);
			S.Name = Out.TableName.c_str();
			return true;
		}

		bool ReadState(Reader& R, const JsonValue& Parent, const char* Key, rb::BallState& Out, const std::string& Path)
		{
			const JsonValue* S = R.ReadObject(Parent, Key, Path);
			if (S == nullptr)
			{
				return false;
			}
			const std::string P = Path + "." + Key;
			if (!(R.ReadVec3(*S, "r", Out.Position, P) && R.ReadVec3(*S, "v", Out.Velocity, P) && R.ReadVec3(*S, "w", Out.Omega, P)))
			{
				return false;
			}
			const JsonValue* Name = R.FindMember(*S, "state", P);
			if (Name == nullptr || !Name->IsString() || !ParseMotionState(Name->StringValue.c_str(), Out.State))
			{
				return R.Fail(P + ".state", "expected a motion state name");
			}
			return true;
		}

		bool ReadBalls(Reader& R, const JsonValue& Root, rb::SimInput& In)
		{
			const JsonValue* Balls = R.ReadArray(Root, "balls", "");
			if (Balls == nullptr)
			{
				return false;
			}
			for (std::size_t i = 0; i < Balls->Items.size(); ++i)
			{
				const JsonValue& B = Balls->Items[i];
				const std::string P = "balls[" + std::to_string(i) + "]";
				if (!B.IsObject())
				{
					return R.Fail(P, "expected an object");
				}
				long long Id = 0;
				if (!R.ReadInt(B, "id", 0, rb::kMaxBalls - 1, Id, P))
				{
					return false;
				}
				rb::SimBall& Ball = In.Balls[Id];
				if (Ball.InPlay)
				{
					return R.Fail(P + ".id", "duplicate ball id");
				}
				Ball.InPlay = true;
				double Q[4] = {};
				if (!(R.ReadReal(B, "radius", Ball.Spec.Radius, P) && R.ReadReal(B, "mass", Ball.Spec.Mass, P) && R.ReadReal(B, "inertia", Ball.Spec.Inertia, P)
						&& ReadState(R, B, "state", Ball.State, P) && R.ReadReals(B, "q", Q, 4, P)))
				{
					return false;
				}
				Ball.Orientation = {Q[0], Q[1], Q[2], Q[3]};
				const JsonValue* Marks = R.ReadArray(B, "chalkMarks", P);
				if (Marks == nullptr)
				{
					return false;
				}
				for (std::size_t m = 0; m < Marks->Items.size(); ++m)
				{
					const JsonValue& M = Marks->Items[m];
					const std::string MP = P + ".chalkMarks[" + std::to_string(m) + "]";
					rb::ChalkMark Mark;
					if (!M.IsObject())
					{
						return R.Fail(MP, "expected an object");
					}
					if (!(R.ReadVec3(M, "dir", Mark.BodyDir, MP) && R.ReadReal(M, "strength", Mark.Strength, MP) && R.ReadReal(M, "radius", Mark.Radius, MP)))
					{
						return false;
					}
					if (!Ball.ChalkMarks.PushBack(Mark))
					{
						return R.Fail(MP, "too many chalk marks");
					}
				}
			}
			return true;
		}

		bool ReadStrikes(Reader& R, const JsonValue& Root, rb::SimInput& In)
		{
			const JsonValue* Strikes = R.ReadArray(Root, "strikes", "");
			if (Strikes == nullptr)
			{
				return false;
			}
			for (std::size_t i = 0; i < Strikes->Items.size(); ++i)
			{
				const JsonValue& S = Strikes->Items[i];
				const std::string P = "strikes[" + std::to_string(i) + "]";
				if (!S.IsObject())
				{
					return R.Fail(P, "expected an object");
				}
				rb::StrikeRequest Request;
				long long Ball = 0;
				rb::CueStrikeInput& I = Request.Input;
				if (!(R.ReadInt(S, "ball", 0, rb::kMaxBalls - 1, Ball, P) && R.ReadReal(S, "V", I.Speed, P) && R.ReadReal(S, "theta", I.Elevation, P)
						&& R.ReadReal(S, "phi", I.Azimuth, P) && R.ReadReal(S, "a", I.OffsetA, P) && R.ReadReal(S, "b", I.OffsetB, P)
						&& R.ReadReal(S, "lambdaOverride", I.LambdaOverride, P) && R.ReadBool(S, "squirt", I.SquirtEnabled, P)
						&& R.ReadBool(S, "tipTouchesCloth", I.TipTouchesCloth, P)))
				{
					return false;
				}
				Request.Ball = static_cast<rb::BallId>(Ball);
				const JsonValue* Cue = R.ReadObject(S, "cue", P);
				if (Cue == nullptr)
				{
					return false;
				}
				const std::string CP = P + ".cue";
				rb::CueSpec& C = I.Cue;
				if (!(R.ReadReal(*Cue, "mass", C.Mass, CP) && R.ReadReal(*Cue, "endMass", C.EndMass, CP) && R.ReadReal(*Cue, "tipRestitution", C.TipRestitution, CP)
						&& R.ReadReal(*Cue, "tipFriction", C.TipFriction, CP) && R.ReadReal(*Cue, "tipFrictionKinetic", C.TipFrictionKinetic, CP)
						&& R.ReadReal(*Cue, "tipDomeRadius", C.TipDomeRadius, CP) && R.ReadReal(*Cue, "tipDiameter", C.TipDiameter, CP)
						&& R.ReadReal(*Cue, "length", C.Length, CP) && R.ReadReal(*Cue, "contactTime", C.ContactTime, CP)
						&& R.ReadReal(*Cue, "followThroughDistance", C.FollowThroughDistance, CP) && R.ReadBool(*Cue, "jumpCue", C.JumpCue, CP)))
				{
					return false;
				}
				if (!In.Strikes.PushBack(Request))
				{
					return R.Fail(P, "too many strikes");
				}
			}
			return true;
		}

		bool ReadContext(Reader& R, const JsonValue& Root, rb::SimInput& In)
		{
			const JsonValue* C = R.ReadObject(Root, "context", "");
			if (C == nullptr)
			{
				return false;
			}
			const std::string P = "context";
			rb::ShotContext& X = In.Context;
			long long InHand = 0;
			if (!(R.ReadInt(*C, "inHand", 0, static_cast<long long>(rb::CueBallInHand::Baulk), InHand, P) && R.ReadVec2(*C, "placed", X.PlacedPosition, P)
					&& R.ReadBool(*C, "templatePresent", X.TemplatePresent, P) && R.ReadReal(*C, "shotClockElapsed", X.ShotClockElapsed, P)
					&& R.ReadBool(*C, "footOnFloor", X.FootOnFloor, P) && R.ReadReal(*C, "frozenTolerance", X.FrozenTolerance, P)))
			{
				return false;
			}
			X.InHand = static_cast<rb::CueBallInHand>(InHand);
			const JsonValue* Contacts = R.ReadArray(*C, "nonTipContacts", P);
			if (Contacts == nullptr)
			{
				return false;
			}
			for (std::size_t i = 0; i < Contacts->Items.size(); ++i)
			{
				const JsonValue& N = Contacts->Items[i];
				const std::string NP = P + ".nonTipContacts[" + std::to_string(i) + "]";
				if (!N.IsObject())
				{
					return R.Fail(NP, "expected an object");
				}
				rb::NonTipContact Contact;
				long long Ball = 0;
				long long Source = 0;
				if (!(R.ReadInt(N, "ball", -1, rb::kMaxBalls - 1, Ball, NP) && R.ReadInt(N, "source", 0, static_cast<long long>(rb::NonTipSource::Other), Source, NP)
						&& R.ReadReal(N, "t", Contact.Time, NP)))
				{
					return false;
				}
				Contact.Ball = static_cast<rb::BallId>(Ball);
				Contact.Source = static_cast<rb::NonTipSource>(Source);
				if (!X.NonTipContacts.PushBack(Contact))
				{
					return R.Fail(NP, "too many non-tip contacts");
				}
			}
			return true;
		}
	}

	const char* MotionStateName(rb::MotionState State)
	{
		for (const NamedState& S : kStates)
		{
			if (S.State == State)
			{
				return S.Name;
			}
		}
		return "?";
	}

	bool ParseMotionState(const char* Name, rb::MotionState& Out)
	{
		for (const NamedState& S : kStates)
		{
			if (std::strcmp(S.Name, Name) == 0)
			{
				Out = S.State;
				return true;
			}
		}
		return false;
	}

	void WriteSimInput(const rb::SimInput& In, JsonWriter& J)
	{
		J.BeginObject();
		J.FieldInt("rbsimInput", kSimInputSchemaVersion);
		J.Field("coreVersion", rb::CoreVersion());
		J.Key("tableSpec");
		WriteTableSpec(J, In.Table != nullptr ? In.Table->Spec : rb::TableSpec{});
		J.Key("environment");
		J.BeginObject();
		J.FieldReal("lampUndersideZ", In.Environment.LampUndersideZ);
		const double Footprint[4] = {In.Environment.LampFootprint.Lo.x, In.Environment.LampFootprint.Lo.y, In.Environment.LampFootprint.Hi.x,
			In.Environment.LampFootprint.Hi.y};
		J.Key("lampFootprint");
		J.RealArray(Footprint, 4);
		J.EndObject();
		J.Key("params");
		J.BeginObject();
		for (int i = 0; i < rb::PhysicsParamCount(); ++i)
		{
			const rb::PhysicsParamInfo Info = rb::PhysicsParamAt(i);
			double Value = 0.0;
			rb::GetPhysicsParam(In.Params, Info.Key, Value);
			J.FieldReal(Info.Key, Value);
		}
		J.EndObject();
		J.Key("balls");
		J.BeginArray();
		for (int Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			const rb::SimBall& B = In.Balls[Id];
			if (!B.InPlay)
			{
				continue;
			}
			J.BeginObject();
			J.FieldInt("id", Id);
			J.FieldReal("radius", B.Spec.Radius);
			J.FieldReal("mass", B.Spec.Mass);
			J.FieldReal("inertia", B.Spec.Inertia);
			J.Key("state");
			WriteState(J, B.State);
			const double Q[4] = {B.Orientation.w, B.Orientation.x, B.Orientation.y, B.Orientation.z};
			J.Key("q");
			J.RealArray(Q, 4);
			J.Key("chalkMarks"); // v1.2 (human-factors 4.3): body-frame marks, read by the cling physics
			J.BeginArray();
			for (const rb::ChalkMark& M : B.ChalkMarks)
			{
				J.BeginObject();
				WriteVec3(J, "dir", M.BodyDir);
				J.FieldReal("strength", M.Strength);
				J.FieldReal("radius", M.Radius);
				J.EndObject();
			}
			J.EndArray();
			J.EndObject();
		}
		J.EndArray();
		J.Key("strikes");
		J.BeginArray();
		for (const rb::StrikeRequest& S : In.Strikes)
		{
			J.BeginObject();
			J.FieldInt("ball", S.Ball);
			J.FieldReal("V", S.Input.Speed);
			J.FieldReal("theta", S.Input.Elevation);
			J.FieldReal("phi", S.Input.Azimuth);
			J.FieldReal("a", S.Input.OffsetA);
			J.FieldReal("b", S.Input.OffsetB);
			J.FieldReal("lambdaOverride", S.Input.LambdaOverride);
			J.FieldBool("squirt", S.Input.SquirtEnabled);
			J.FieldBool("tipTouchesCloth", S.Input.TipTouchesCloth);
			J.Key("cue");
			J.BeginObject();
			J.FieldReal("mass", S.Input.Cue.Mass);
			J.FieldReal("endMass", S.Input.Cue.EndMass);
			J.FieldReal("tipRestitution", S.Input.Cue.TipRestitution);
			J.FieldReal("tipFriction", S.Input.Cue.TipFriction);
			J.FieldReal("tipFrictionKinetic", S.Input.Cue.TipFrictionKinetic);
			J.FieldReal("tipDomeRadius", S.Input.Cue.TipDomeRadius);
			J.FieldReal("tipDiameter", S.Input.Cue.TipDiameter);
			J.FieldReal("length", S.Input.Cue.Length);
			J.FieldReal("contactTime", S.Input.Cue.ContactTime);
			J.FieldReal("followThroughDistance", S.Input.Cue.FollowThroughDistance);
			J.FieldBool("jumpCue", S.Input.Cue.JumpCue);
			J.EndObject();
			J.EndObject();
		}
		J.EndArray();
		J.Key("context");
		J.BeginObject();
		J.FieldInt("inHand", static_cast<int>(In.Context.InHand));
		WriteVec2(J, "placed", In.Context.PlacedPosition);
		J.FieldBool("templatePresent", In.Context.TemplatePresent);
		J.FieldReal("shotClockElapsed", In.Context.ShotClockElapsed);
		J.FieldBool("footOnFloor", In.Context.FootOnFloor);
		J.FieldReal("frozenTolerance", In.Context.FrozenTolerance);
		J.Key("nonTipContacts");
		J.BeginArray();
		for (const rb::NonTipContact& C : In.Context.NonTipContacts)
		{
			J.BeginObject();
			J.FieldInt("ball", C.Ball);
			J.FieldInt("source", static_cast<int>(C.Source));
			J.FieldReal("t", C.Time);
			J.EndObject();
		}
		J.EndArray();
		J.EndObject();
		J.Key("record");
		J.BeginObject();
		J.FieldBool("trajectories", In.Record.Trajectories);
		J.FieldBool("eventStates", In.Record.EventStates);
		J.FieldBool("logTransitions", In.Record.LogTransitions);
		J.FieldBool("logObservers", In.Record.LogObservers);
		J.FieldBool("shotRecord", In.Record.ShotRecord);
		J.EndObject();
		J.EndObject();
	}

	bool ReadSimInput(const JsonValue& Root, LoadedSimInput& Out, std::string& Error)
	{
		Error.clear();
		Reader R(Error);
		Out.Input = rb::SimInput{};
		Out.MissingParams = 0;
		if (!Root.IsObject())
		{
			return R.Fail("document", "expected an object");
		}
		long long Version = 0;
		if (!R.ReadInt(Root, "rbsimInput", 1, 1000000, Version, "document"))
		{
			return false;
		}
		if (Version != kSimInputSchemaVersion)
		{
			return R.Fail("rbsimInput", "unsupported schema version");
		}

		const JsonValue* Table = R.ReadObject(Root, "tableSpec", "");
		if (Table == nullptr || !ReadTableSpec(R, *Table, Out))
		{
			return false;
		}
		const rb::TableSpec Spec = Out.Geometry.Spec;
		if (rb::BuildTableGeometry(Spec, Out.Geometry) != rb::ErrorCode::Ok)
		{
			return R.Fail("tableSpec", "rejected by BuildTableGeometry");
		}
		Out.Geometry.Spec.Name = Out.TableName.c_str();
		rb::SimInput& In = Out.Input;
		In.Table = &Out.Geometry;

		const JsonValue* Env = R.ReadObject(Root, "environment", "");
		if (Env == nullptr)
		{
			return false;
		}
		double Footprint[4] = {};
		if (!(R.ReadReal(*Env, "lampUndersideZ", In.Environment.LampUndersideZ, "environment") && R.ReadReals(*Env, "lampFootprint", Footprint, 4, "environment")))
		{
			return false;
		}
		In.Environment.LampFootprint.Lo = {Footprint[0], Footprint[1]};
		In.Environment.LampFootprint.Hi = {Footprint[2], Footprint[3]};

		const JsonValue* Params = R.ReadObject(Root, "params", "");
		if (Params == nullptr)
		{
			return false;
		}
		In.Params = rb::MakePhysicsParams(Spec);
		for (const std::pair<std::string, JsonValue>& Member : Params->Members)
		{
			double Value = 0.0;
			if (!Reader::AsReal(Member.second, Value))
			{
				return R.Fail("params." + Member.first, "expected a number");
			}
			if (!rb::SetPhysicsParam(In.Params, Member.first.c_str(), Value))
			{
				return R.Fail("params." + Member.first, "unknown key or invalid value");
			}
		}
		for (int i = 0; i < rb::PhysicsParamCount(); ++i)
		{
			if (Params->Find(rb::PhysicsParamAt(i).Key) == nullptr)
			{
				++Out.MissingParams;
			}
		}

		if (!ReadBalls(R, Root, In) || !ReadStrikes(R, Root, In) || !ReadContext(R, Root, In))
		{
			return false;
		}

		const JsonValue* Record = R.ReadObject(Root, "record", "");
		if (Record == nullptr)
		{
			return false;
		}
		rb::RecordOptions& O = In.Record;
		return R.ReadBool(*Record, "trajectories", O.Trajectories, "record") && R.ReadBool(*Record, "eventStates", O.EventStates, "record")
			&& R.ReadBool(*Record, "logTransitions", O.LogTransitions, "record") && R.ReadBool(*Record, "logObservers", O.LogObservers, "record")
			&& R.ReadBool(*Record, "shotRecord", O.ShotRecord, "record");
	}

	bool ParseSimInput(const std::string& Text, LoadedSimInput& Out, std::string& Error)
	{
		JsonValue Root;
		if (!ParseJson(Text, Root, Error))
		{
			return false;
		}
		return ReadSimInput(Root, Out, Error);
	}
}
