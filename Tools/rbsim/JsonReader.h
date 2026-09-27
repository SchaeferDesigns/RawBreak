#pragma once

// Small strict JSON reader (DOM) for rbsim --in (not part of BilliardsCore). Owner: WP-7.
// RFC 8259 grammar; numbers are validated against the JSON number syntax and converted with strtod (correctly
// rounded, so the 17-significant-digit numbers of JsonWriter round-trip bitwise). No exceptions: Parse returns false
// and a message with the byte offset.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace rbsim
{
	struct JsonValue
	{
		enum class Kind : unsigned char
		{
			Null,
			Bool,
			Number,
			String,
			Array,
			Object,
		};

		Kind Type = Kind::Null;
		bool BoolValue = false;
		double NumberValue = 0.0;
		std::string StringValue;
		std::vector<JsonValue> Items;                              // Array
		std::vector<std::pair<std::string, JsonValue>> Members;    // Object (document order)

		bool IsNull() const { return Type == Kind::Null; }
		bool IsBool() const { return Type == Kind::Bool; }
		bool IsNumber() const { return Type == Kind::Number; }
		bool IsString() const { return Type == Kind::String; }
		bool IsArray() const { return Type == Kind::Array; }
		bool IsObject() const { return Type == Kind::Object; }

		// Object member by key (first match), nullptr if absent or not an object.
		const JsonValue* Find(const char* Key) const;
	};

	// Parses a complete document (trailing whitespace only). MaxDepth bounds the nesting.
	bool ParseJson(const std::string& Text, JsonValue& Out, std::string& Error, int MaxDepth = 64);
}
