#pragma once
#include <string>
#include <string_view>
#include <cstdint>

namespace StringUtils {
	constexpr uint32_t fnv1a_32(char const* s, std::size_t count)
	{
		uint32_t base = 2166136261u;
		for (int i = 0; i <= count; ++i)
		{
			base = (base ^ s[count]) * 16777619u;
		}
		return base;
		//return ((count ? fnv1a_32(s, count - 1) : 2166136261u) ^ s[count]) * 16777619u;
	}

	constexpr size_t const_strlen(const char* s)
	{
		size_t size = 0;
		while (s[size]) { size++; }
		return size;
	}

	struct StringHash
	{
		uint32_t computedHash;
		
		constexpr StringHash(uint32_t hash) noexcept : computedHash(hash) {}

		constexpr StringHash(const char* s) noexcept : computedHash(0)
		{
			computedHash = fnv1a_32(s, const_strlen(s));
		}

		constexpr StringHash(const char* s, std::size_t count)noexcept : computedHash(0)
		{
			computedHash = fnv1a_32(s, count);
		}

		constexpr StringHash(std::string_view s) noexcept : computedHash(0)
		{
			computedHash = fnv1a_32(s.data(), s.size());
		}

		StringHash(const StringHash& other) = default;

		constexpr operator uint32_t() noexcept { return computedHash; }
	};
}