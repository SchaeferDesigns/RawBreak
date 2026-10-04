#pragma once

// A minimal JSON reader for the reference data of the audio tests (Tools/audio/out/ref/*.json: golden vectors, kernels, the shape
// table, event logs). Core only (the RawBreak module does not link the Json module): objects, arrays, numbers (parsed with
// strtod, exact to the last bit), strings (basic escapes), true / false / null. Owner: M2-C.

#include "CoreMinimal.h"

namespace RbAudio
{
	struct RAWBREAKAUDIODSP_API FJsonLite
	{
		enum class EType : uint8
		{
			Null,
			Bool,
			Number,
			String,
			Array,
			Object,
		};

		EType Type = EType::Null;
		double Number = 0.0;
		bool Bool = false;
		FString String;
		TArray<FJsonLite> Items;                 // array elements
		TArray<TPair<FString, FJsonLite>> Fields; // object members, in file order

		// Parses Text; false on a syntax error.
		static bool Parse(const FString& Text, FJsonLite& Out);
		static bool ParseFile(const FString& Path, FJsonLite& Out);

		// Object member (null value when missing).
		const FJsonLite& operator[](const FString& Key) const;
		bool Has(const FString& Key) const;
		// Array element (null value when out of range).
		const FJsonLite& At(int32 Index) const;
		int32 Num() const { return Type == EType::Array ? Items.Num() : Fields.Num(); }

		double AsNumber(double Default = 0.0) const { return Type == EType::Number ? Number : Default; }
		// Array of numbers.
		TArray<double> AsNumbers() const;
	};
}
