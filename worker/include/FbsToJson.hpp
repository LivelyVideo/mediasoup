#ifndef MS_FBS_TO_JSON_HPP
#define MS_FBS_TO_JSON_HPP

// Serialize FlatBuffers tables to JSON strings with camelCase field names.
// Used to replicate v3-lively's nlohmann::json data.dump() diagnostic output
// in the FBS world.
//
// Uses the minireflect IterationVisitor API (compiled with --reflect-names)
// to traverse any FBS table and produce compact JSON output.

#include <flatbuffers/minireflect.h>
#include <cctype>
#include <string>

namespace Lively
{
	// Convert FBS snake_case field names to JSON camelCase.
	// e.g. "consumer_id" -> "consumerId", "rtp_parameters" -> "rtpParameters"
	inline std::string SnakeToCamel(const char* name)
	{
		std::string result;

		bool capitalizeNext = false;

		for (const char* p = name; *p; ++p)
		{
			if (*p == '_')
			{
				capitalizeNext = true;
			}
			else
			{
				if (capitalizeNext)
				{
					result += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
					capitalizeNext = false;
				}
				else
				{
					result += *p;
				}
			}
		}

		return result;
	}

	// Lowercase a string (for enum names: VIDEO -> video, SIMULCAST -> simulcast)
	inline std::string ToLower(const char* name)
	{
		std::string result;

		for (const char* p = name; *p; ++p)
		{
			result += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
		}

		return result;
	}

	// IterationVisitor that outputs compact JSON with camelCase keys.
	// Mirrors the output of nlohmann::json::dump() used in v3-lively.
	struct JsonVisitor : public flatbuffers::IterationVisitor
	{
		std::string s;

		void StartSequence() override
		{
			s += "{";
		}

		void EndSequence() override
		{
			s += "}";
		}

		void Field(
		  size_t /*field_idx*/,
		  size_t set_idx,
		  flatbuffers::ElementaryType /*type*/,
		  bool /*is_vector*/,
		  const flatbuffers::TypeTable* /*type_table*/,
		  const char* name,
		  const uint8_t* val) override
		{
			if (!val)
				return;

			if (set_idx)
				s += ",";

			if (name)
			{
				s += "\"";
				s += SnakeToCamel(name);
				s += "\":";
			}
		}

		template<typename T>
		void Named(T x, const char* name)
		{
			if (name)
			{
				s += "\"";
				s += ToLower(name);
				s += "\"";
			}
			else
			{
				s += flatbuffers::NumToString(x);
			}
		}

		void UType(uint8_t x, const char* name) override
		{
			Named(x, name);
		}
		void Bool(bool x) override
		{
			s += x ? "true" : "false";
		}
		void Char(int8_t x, const char* name) override
		{
			Named(x, name);
		}
		void UChar(uint8_t x, const char* name) override
		{
			Named(x, name);
		}
		void Short(int16_t x, const char* name) override
		{
			Named(x, name);
		}
		void UShort(uint16_t x, const char* name) override
		{
			Named(x, name);
		}
		void Int(int32_t x, const char* name) override
		{
			Named(x, name);
		}
		void UInt(uint32_t x, const char* name) override
		{
			Named(x, name);
		}
		void Long(int64_t x) override
		{
			s += flatbuffers::NumToString(x);
		}
		void ULong(uint64_t x) override
		{
			s += flatbuffers::NumToString(x);
		}
		void Float(float x) override
		{
			s += flatbuffers::NumToString(x);
		}
		void Double(double x) override
		{
			s += flatbuffers::NumToString(x);
		}
		void String(const flatbuffers::String* str) override
		{
			flatbuffers::EscapeString(str->c_str(), str->size(), &s, true, false);
		}
		void Unknown(const uint8_t*) override
		{
			s += "null";
		}

		void StartVector() override
		{
			s += "[";
		}

		void EndVector() override
		{
			s += "]";
		}

		void Element(
		  size_t i,
		  flatbuffers::ElementaryType /*type*/,
		  const flatbuffers::TypeTable* /*type_table*/,
		  const uint8_t* /*val*/) override
		{
			if (i)
				s += ",";
		}
	};

	// Serialize any FBS table to a compact JSON string with camelCase keys.
	// Usage: FbsToJson(data, FBS::Transport::ConsumeRequestTypeTable())
	inline std::string FbsToJson(const void* obj, const flatbuffers::TypeTable* typeTable)
	{
		JsonVisitor visitor;

		flatbuffers::IterateObject(
		  reinterpret_cast<const uint8_t*>(obj), typeTable, &visitor);

		return visitor.s;
	}
} // namespace Lively

#endif
