#pragma once

// Tiny streaming JSON writer for rbsim (not part of BilliardsCore). Owner: WP-7.
// Numbers are written with 17 significant digits (round-trip exact); NaN/inf become null (Number) or the strings
// "Infinity", "-Infinity", "NaN" (Real: the rbsimInput dump, which must reproduce every double bitwise).

#include <cstdint>
#include <string>

namespace rbsim
{
	class JsonWriter
	{
	public:
		explicit JsonWriter(bool Pretty = true);

		void BeginObject();
		void EndObject();
		void BeginArray();
		void EndArray();

		// Inside an object: writes "Name": and expects exactly one value next.
		void Key(const char* Name);

		void String(const char* Value);
		void Number(double Value);
		void Real(double Value); // like Number, but non-finite values as strings (exact round trip, JsonReader side: ReadReal)
		void Integer(std::int64_t Value);
		void Bool(bool Value);
		void Null();

		// Convenience for key/value pairs inside an object.
		void Field(const char* Name, const char* Value) { Key(Name); String(Value); }
		void Field(const char* Name, double Value) { Key(Name); Number(Value); }
		void FieldReal(const char* Name, double Value) { Key(Name); Real(Value); }
		void FieldInt(const char* Name, std::int64_t Value) { Key(Name); Integer(Value); }
		void FieldBool(const char* Name, bool Value) { Key(Name); Bool(Value); }

		// Compact arrays of numbers on one line, e.g. vectors [x, y, z].
		void NumberArray(const double* Values, int Count);
		void RealArray(const double* Values, int Count);

		const std::string& Text() const { return Out; }

	private:
		void BeforeValue();
		void NewLine();
		void WriteEscaped(const char* Value);

		std::string Out;
		bool PrettyPrint = true;
		int Depth = 0;
		bool NeedComma[64] = {};
		bool AfterKey = false;
		int InlineDepth = 0; // > 0 while writing a compact array
	};
}
