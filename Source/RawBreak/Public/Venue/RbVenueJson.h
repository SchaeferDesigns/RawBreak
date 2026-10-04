#pragma once

// Minimal JSON reader for the venue data files (Art/DiveBar/layout.json, lights.json) used by the level validator and the venue
// tests. The RawBreak module does not link the engine's Json module (a Build.cs change belongs to the architect: request in the
// M2-A report); this reader covers RFC 8259 values (objects, arrays, numbers, strings with escapes, true / false / null) and
// nothing else. Owner: M2-A.

#include "CoreMinimal.h"

struct RAWBREAK_API FRbJson
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
	bool Bool = false;
	double Number = 0.0;
	FString String;
	TArray<FRbJson> Array;
	TArray<TPair<FString, FRbJson>> Object; // insertion order kept

	// Parses Text; false (with the character offset in OutError) on a syntax error.
	static bool Parse(const FString& Text, FRbJson& Out, FString* OutError = nullptr);
	// Loads and parses <ProjectDir>/Relative; false when the file is missing or invalid.
	static bool LoadProjectFile(const FString& Relative, FRbJson& Out, FString* OutError = nullptr);

	bool IsValid() const { return Type != EType::Null; }
	bool IsObject() const { return Type == EType::Object; }
	bool IsArray() const { return Type == EType::Array; }

	// Object member (nullptr when absent / not an object).
	const FRbJson* Find(const TCHAR* Key) const;
	// Member or a shared null value.
	const FRbJson& operator[](const TCHAR* Key) const;
	// Array element or a shared null value.
	const FRbJson& operator[](int32 Index) const;
	int32 Num() const { return Type == EType::Array ? Array.Num() : (Type == EType::Object ? Object.Num() : 0); }

	double AsNumber(double Default = 0.0) const { return Type == EType::Number ? Number : Default; }
	bool AsBool(bool Default = false) const { return Type == EType::Bool ? Bool : Default; }
	FString AsString(const FString& Default = FString()) const { return Type == EType::String ? String : Default; }
	// [x, y, z] * Scale (zero when not an array of three numbers).
	FVector AsVector(double Scale = 1.0) const;
	FVector2D AsVector2D(const FVector2D& Default = FVector2D::ZeroVector) const;

	double GetNumber(const TCHAR* Key, double Default = 0.0) const;
	bool GetBool(const TCHAR* Key, bool Default = false) const;
	FString GetString(const TCHAR* Key, const FString& Default = FString()) const;
	bool Has(const TCHAR* Key) const { return Find(Key) != nullptr; }
};
