#ifndef _DIXELU_INTEGERS_H_
#define _DIXELU_INTEGERS_H_

#if (defined(__cpp_constexpr) && (__cpp_constexpr >= 201304L))

#ifndef __DIXELU_RELAXED_CONSTEXPR
#define __DIXELU_RELAXED_CONSTEXPR constexpr
#endif
#else
#ifndef __DIXELU_RELAXED_CONSTEXPR
#define __DIXELU_RELAXED_CONSTEXPR
#endif
#endif

#if (defined(__cpp_constexpr) && (__cpp_constexpr >= 200704L))
#ifndef __DIXELU_STRICT_CONSTEXPR
#define __DIXELU_STRICT_CONSTEXPR constexpr
#endif
#else
#ifndef __DIXELU_STRICT_CONSTEXPR
#define __DIXELU_STRICT_CONSTEXPR
#endif
#endif

#ifndef __DIXELU_CONDITIONAL_CPP14_SPECIFIERS
#define __DIXELU_CONDITIONAL_CPP14_SPECIFIERS inline __DIXELU_RELAXED_CONSTEXPR
#endif

#include <limits.h>
#include <limits>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <algorithm>
#include <cinttypes>
#include <string>

#include <stdint.h>
#ifdef _MSC_VER
#  include <intrin.h>
#endif

namespace dixelu
{
	namespace details
	{
		template<std::uint64_t deg, typename base_type>
		struct tree_bits
		{
			static constexpr base_type base_bits = sizeof(base_type) * CHAR_BIT;
			static constexpr base_type value = (tree_bits<deg - 1, base_type>::value << 1);
		};

		template<typename base_type>
		struct tree_bits<0, base_type>
		{
			static constexpr base_type base_bits = sizeof(base_type) * CHAR_BIT;
			static constexpr base_type value = base_bits * 2;
		};

		// https://stackoverflow.com/a/58381061
		/* Prevents a partial vectorization from GCC. */
#if defined(__GNUC__) && !defined(__clang__) && defined(__i386__)
		// __attribute__((__target__("no-sse")))
#endif
		template<typename __uint64__type_emulator>
		static constexpr __uint64__type_emulator multiply64to128(
			__uint64__type_emulator lhs,
			__uint64__type_emulator rhs,
			__uint64__type_emulator& high)
		{
			/*
			 * GCC and Clang usually provide __uint128_t on 64-bit targets,
			 * although Clang also defines it on WASM despite having to use
			 * builtins for most purposes - including multiplication.
			 */
#if defined(__SIZEOF_INT128__) && !defined(__wasm__)
			__uint128_t product = (__uint128_t)lhs * (__uint128_t)rhs;
			high = (uint64_t)(product >> 64);
			return (uint64_t)(product & 0xFFFFFFFFFFFFFFFF);

			/* Use the _umul128 intrinsic on MSVC x64 to hint for mulq. */
#elif defined(_MSC_VER) && defined(_M_IX64)
#   pragma intrinsic(_umul128)
			 /* This intentionally has the same signature. */
			return _umul128(lhs, rhs, &high);

#else
			 /*
			  * Fast yet simple grade school multiply that avoids
			  * 64-bit carries with the properties of multiplying by 11
			  * and takes advantage of UMAAL on ARMv6 to only need 4
			  * calculations.
			  */

			  /* First calculate all the cross products. */
			__uint64__type_emulator lo_lo = (lhs & 0xFFFFFFFF) * (rhs & 0xFFFFFFFF);
			__uint64__type_emulator hi_lo = (lhs >> 32) * (rhs & 0xFFFFFFFF);
			__uint64__type_emulator lo_hi = (lhs & 0xFFFFFFFF) * (rhs >> 32);
			__uint64__type_emulator hi_hi = (lhs >> 32) * (rhs >> 32);

			/* Now add the products together. These will never overflow. */
			__uint64__type_emulator cross = (lo_lo >> 32) + (hi_lo & 0xFFFFFFFF) + lo_hi;
			__uint64__type_emulator upper = (hi_lo >> 32) + (cross >> 32) + hi_hi;

			high = upper;
			return (cross << 32) | (lo_lo & 0xFFFFFFFF);
#endif /* portable */
		}
	}

	/* long_uint<0> ~ 128 bit unsigned integer */
	template <std::uint64_t deg>
	struct long_uint
	{
		using base_type = std::uint64_t; // largest of the integer types
		using size_type = std::uint64_t;
		using self_type = long_uint<deg>;
		using down_type = typename std::conditional<deg == 0, base_type, long_uint<deg - 1>>::type;

		static constexpr size_type base_bits = details::tree_bits<0, base_type>::base_bits;
		static constexpr size_type bits = details::tree_bits<deg, base_type>::value;
		static constexpr size_type down_type_bits = bits >> 1;
		static constexpr size_type down_type_size = down_type_bits / base_bits;
		static constexpr size_type size = bits / base_bits;
		static constexpr bool is_constexpr_expensive = true; // change to false after optimisations;

		struct __fill_fields_tag {};

		down_type hi;
		down_type lo;

		__DIXELU_STRICT_CONSTEXPR
			long_uint() :
			hi(), lo()
		{ }

		__DIXELU_STRICT_CONSTEXPR
			long_uint(base_type value) :
			hi(), lo(value)
		{ }

		template<uint64_t __deg = deg>
		__DIXELU_STRICT_CONSTEXPR
			explicit long_uint(base_type value,
				const __fill_fields_tag& fill_fields_tag,
				typename std::enable_if<(__deg > 0), void>::type* = 0) :
			hi(value, typename long_uint<__deg - 1>::__fill_fields_tag{}),
			lo(value, typename long_uint<__deg - 1>::__fill_fields_tag{})
		{ }

		template<uint64_t __deg = deg>
		__DIXELU_STRICT_CONSTEXPR
			explicit long_uint(base_type value,
				const __fill_fields_tag& fill_fields_tag,
				typename std::enable_if<(__deg == 0), void>::type* = 0) :
			hi(value), lo(value)
		{ }

		template<uint64_t __deg>
		__DIXELU_RELAXED_CONSTEXPR
			explicit long_uint(const long_uint<__deg>& value,
				typename std::enable_if<(__deg < deg), void>::type* = 0) : hi(), lo((down_type)value) { }

		template<uint64_t __deg>
		__DIXELU_RELAXED_CONSTEXPR
			explicit long_uint(const long_uint<__deg>& value,
				typename std::enable_if<(__deg > deg), void>::type* = 0) :
			hi(value.lo.hi),
			lo(value.lo.lo)
		{ }

		template<uint64_t __deg>
		__DIXELU_RELAXED_CONSTEXPR
			explicit long_uint(const long_uint<__deg>& value,
				typename std::enable_if<(__deg == deg), void>::type* = 0) : hi(value.hi), lo(value.lo) { }

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& operator|=(const self_type& rhs)
		{
			hi |= rhs.hi;
			lo |= rhs.lo;
			return *this;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& operator&=(const self_type& rhs)
		{
			hi &= rhs.hi;
			lo &= rhs.lo;
			return *this;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& operator^=(const self_type& rhs)
		{
			hi ^= rhs.hi;
			lo ^= rhs.lo;
			return *this;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator~() const
		{
			long_uint res;
			res.hi = ~hi;
			res.lo = ~lo;
			return res;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& self_inverse()
		{
			hi = ~hi;
			lo = ~lo;
			return *this;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator|(const self_type& rhs) const
		{
			long_uint res(*this);
			res |= rhs;
			return res;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator|(self_type&& rhs) const
		{
			return (rhs |= *this);
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator&(const self_type& rhs) const
		{
			long_uint res(*this);
			res &= rhs;
			return res;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator&(self_type&& rhs) const
		{
			return (rhs &= *this);
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator^(const self_type& rhs) const
		{
			long_uint res(*this);
			res ^= rhs;
			return res;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator^(self_type&& rhs) const
		{
			return (rhs ^= *this);
		}

		template< bool cond, typename U >
		using resolved_return_type = typename std::enable_if<cond, U>::type;

		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg > 0), bool> get_bit(size_type bit) const
		{
			if (bit < down_type_bits)
				return lo.get_bit(bit);
			bit -= down_type_bits;
			return hi.get_bit(bit);
		}

		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg == 0), bool> get_bit(size_type bit) const
		{
			if (bit < down_type_bits)
				return (lo >> bit) & 1;
			bit -= down_type_bits;
			return (hi >> bit) & 1;
		}

		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg > 0), base_type&> operator[](size_type idx)
		{
			if (idx < down_type_bits / base_bits)
				return lo[idx];
			idx -= down_type_bits / base_bits;
			return hi[idx];
		}

		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg == 0), base_type&> operator[](size_type idx)
		{
			if (idx < down_type_size)
				return lo;
			return hi;
		}

		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg > 0), const base_type&> operator[](size_type idx) const
		{
			if (idx < down_type_size)
				return lo[idx];
			idx -= down_type_size;
			return hi[idx];
		}

		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg == 0), const base_type&> operator[](size_type idx) const
		{
			if (idx < down_type_size)
				return lo;
			return hi;
		}

		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg > 0), void>
			set_bit(size_type bit, bool bit_value)
		{
			if (bit < down_type_bits)
				return lo.set_bit(bit, bit_value);
			bit -= down_type_bits;
			return hi.set_bit(bit, bit_value);
		}

		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg == 0), void>
			set_bit(size_type bit, bool bit_value)
		{
			if (bit < down_type_bits)
			{
				const auto mask = ~(base_type(1) << bit);
				const auto value = (base_type(bit_value) << bit);
				lo &= mask;
				lo |= value;
				return;
			}
			bit -= down_type_bits;
			const auto mask = ~(base_type(1) << bit);
			const auto value = (base_type(bit_value) << bit);
			hi &= mask;
			hi |= value;
			return;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& operator+=(const self_type& rhs)
		{
			const bool lhs_lo_carried_bit = get_bit(down_type_bits - 1);
			const bool rhs_lo_carried_bit = rhs.get_bit(down_type_bits - 1);
			const bool carry_possibility = lhs_lo_carried_bit ^ rhs_lo_carried_bit;
			const bool definite_carry = lhs_lo_carried_bit && rhs_lo_carried_bit;

			hi += rhs.hi;
			lo += rhs.lo;

			if (definite_carry)
				hi += down_type(1);
			else if (carry_possibility)
			{
				const auto carried_bit = get_bit(down_type_bits - 1);
				if (!carried_bit)
					hi += down_type(1);
			}

			return *this;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator+(const self_type& rhs) const
		{
			self_type res(*this);
			res += rhs;
			return res;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator+(self_type&& rhs) const
		{
			return (rhs += *this);
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& operator-=(const self_type& rhs)
		{
			constexpr const self_type one(1);
			return ((*this) += ((~rhs) + one));
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& operator-=(self_type&& rhs)
		{
			constexpr const self_type one(1);
			return ((*this) += (rhs.self_inverse() + one));
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator-(const self_type& rhs) const
		{
			self_type res(*this);
			res -= rhs;
			return res;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator-(self_type&& rhs) const
		{
			self_type copy = *this;
			copy -= std::move(rhs);
			return copy;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			explicit operator size_type() const
		{
			return operator[](0);
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			explicit operator bool() const
		{
			return ((bool)lo | (bool)hi);
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& __experimental_shift_bits_left(size_type shift)
		{
			if (shift < base_bits)
			{
				const auto shifted_part_length = base_bits - shift;
				const auto shift_cut_mask = (~0ULL << shifted_part_length);

				size_type mask_buffer = 0;
				for (size_type i = 0; i < size; ++i)
				{
					auto& current_value = operator[](i);
					mask_buffer = (current_value & shift_cut_mask) >> shifted_part_length;
					current_value = (current_value << shift) | mask_buffer;
				}

				return *this;
			}
			else
			{
				auto rough_shift_length_in_bytes = shift / base_bits;
				auto accurate_shift = shift - rough_shift_length_in_bytes * base_bits;

				for (std::ptrdiff_t i = size - 1; i >= rough_shift_length_in_bytes; --i)
				{
					auto& this_el = operator[](i);
					auto& prev_el = operator[](i - rough_shift_length_in_bytes);
					this_el = prev_el;
				}
				for (std::ptrdiff_t i = rough_shift_length_in_bytes - 1; i >= 0; --i)
					operator[](i) = 0;

				return __experimental_shift_bits_left(accurate_shift);
			}
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& __experimental_shift_bits_right(size_type shift)
		{
			if (shift < base_bits)
			{
				const auto shifted_part_length = base_bits - shift;
				const auto shift_cut_mask = (~0ULL >> shifted_part_length);

				size_type mask_buffer = 0;
				for (std::ptrdiff_t i = size - 1; i >= 0; --i)
				{
					auto& current_value = operator[](i);
					auto shifted_value_copy = (current_value >> shift) | mask_buffer;
					mask_buffer = (current_value & shift_cut_mask) << shifted_part_length;
					current_value = shifted_value_copy;
				}

				return *this;
			}
			else
			{
				auto rough_shift_length_in_bytes = shift / base_bits;
				auto accurate_shift = shift - rough_shift_length_in_bytes * base_bits;

				for (std::ptrdiff_t i = 0; i < size - rough_shift_length_in_bytes; ++i)
				{
					auto& this_el = operator[](i);
					auto& next_el = operator[](i + rough_shift_length_in_bytes);
					this_el = next_el;
				}
				for (std::ptrdiff_t i = size - rough_shift_length_in_bytes; i < size; ++i)
					operator[](i) = 0;

				return __experimental_shift_bits_right(accurate_shift);
			}
		}

		///* https://github.com/glitchub/arith64/blob/master/arith64.c *///
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& operator<<=(size_type rhs)
		{
			rhs &= (bits - 1);
			if (rhs >= down_type_bits)
			{
				hi = (lo <<= (rhs - down_type_bits));
				lo = 0;
			}
			else if (rhs)
			{
				auto lo_copy = lo;
				auto hi_copy = hi;
				hi = (lo_copy >>= (down_type_bits - rhs)) | (hi_copy <<= rhs);
				lo <<= rhs;
			}
			return *this;
		}

		///* https://github.com/glitchub/arith64/blob/master/arith64.c *///
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& operator>>=(size_type rhs)
		{
			rhs &= (bits - 1);
			if (rhs >= down_type_bits)
			{
				lo = (hi >>= (rhs - down_type_bits));
				hi = 0;
			}
			else if (rhs)
			{
				auto lo_copy = lo;
				auto hi_copy = hi;
				lo = (hi_copy <<= (down_type_bits - rhs)) | (lo_copy >>= rhs);
				hi >>= rhs;
			}
			return *this;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator<<(size_type rhs) const
		{
			self_type res(*this);
			res <<= rhs;
			return res;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator>>(size_type rhs) const
		{
			self_type res(*this);
			res >>= rhs;
			return res;
		}

		template<std::uint64_t __deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			explicit operator resolved_return_type<(__deg == deg || !__deg), long_uint<__deg>>() const
		{
			return *this;
		}

		template<std::uint64_t __deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			explicit operator resolved_return_type<(__deg < deg && __deg), long_uint<__deg>>() const
		{
			return (long_uint<__deg>)(lo);
		}

		template<std::uint64_t __deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			explicit operator resolved_return_type<(__deg > deg&& __deg), long_uint<__deg>>() const
		{
			long_uint<deg + 1> res;
			res.lo = *this;
			return (long_uint<__deg>)(res);
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator*(const self_type& rhs) const
		{
			return __direct_karatsuba_mul<deg>(*this, rhs).lo;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& operator*=(const self_type& rhs)
		{
			return (*this = (*this * rhs));
		}

		/*template<std::uint64_t __deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			static long_uint<__deg> __downtype_mul_long(const long_uint<__deg>& lhs, const long_uint<__deg>& rhs)
		{
			long_uint<__deg> carry(1);
			long_uint<__deg> result;
			long_uint<__deg> rolling_rhs = rhs;
			long_uint<__deg> diminishing_lhs = lhs;
			while (diminishing_lhs)
			{
				if (diminishing_lhs & carry)
				{
					result += rolling_rhs;
					diminishing_lhs &= ~(carry);
				}
				rolling_rhs <<= 1;
				carry <<= 1;
			}
			return result;
		}*/

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
		static long_uint<0>
			__direct_mul_call(
				const base_type& lhs,
				const base_type& rhs)
		{
			long_uint<0> result;
			result.lo = details::multiply64to128(lhs, rhs, result.hi);
			return result;
		}

		template<std::uint64_t __deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			static long_uint<__deg + 1>
			__direct_mul_call(const long_uint<__deg>& lhs, const long_uint<__deg>& rhs)
		{
			return __direct_karatsuba_mul<__deg>(lhs, rhs);
		}

		template<std::uint64_t __deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			static long_uint<(__deg + 1)> __direct_karatsuba_mul(const long_uint<__deg>& lhs, const long_uint<__deg>& rhs)
		{
			if (!lhs || !rhs)
				return {};

			const auto& lhs_lo = lhs.lo;
			const auto& rhs_lo = rhs.lo;
			const auto& lhs_hi = lhs.hi;
			const auto& rhs_hi = rhs.hi;

			const long_uint<__deg> hihi = __direct_mul_call(lhs_hi, rhs_hi);
			const long_uint<__deg> lolo = __direct_mul_call(lhs_lo, rhs_lo);

			const bool rhs_diff_is_negative = rhs_hi > rhs_lo;
			const auto rhs_diff = rhs_diff_is_negative ? rhs_hi - rhs_lo : rhs_lo - rhs_hi;
			const bool lhs_diff_is_negative = lhs_lo > lhs_hi;
			const auto lhs_diff = lhs_diff_is_negative ? lhs_lo - lhs_hi : lhs_hi - lhs_lo;

			const bool hihilolo_is_negative = rhs_diff_is_negative ^ lhs_diff_is_negative;
			const long_uint<__deg> hihilolo = __direct_mul_call(rhs_diff, lhs_diff);

			long_uint<__deg + 1> hilo = (long_uint<__deg + 1>)hihi + (long_uint<__deg + 1>)lolo;

			if (hihilolo_is_negative)
				hilo -= (long_uint<__deg + 1>)hihilolo;
			else
				hilo += (long_uint<__deg + 1>)hihilolo;

			long_uint<__deg + 1> res;

			res.hi = hihi;
			res.lo = lolo;

			res += hilo << long_uint<__deg>::down_type_bits;

			return res;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			bool operator<(const self_type& rhs) const
		{
			return hi < rhs.hi || (hi == rhs.hi && lo < rhs.lo);
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			bool operator==(const self_type& rhs) const
		{
			return hi == rhs.hi && lo == rhs.lo;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			bool operator>(const self_type& rhs) const
		{
			return hi > rhs.hi || (hi == rhs.hi && lo > rhs.lo);
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			bool operator>=(const self_type& rhs) const
		{
			return !((*this) < rhs);
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			bool operator<=(const self_type& rhs) const
		{
			return !((*this) > rhs);
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			bool operator!=(const self_type& rhs) const
		{
			return !((*this) == rhs);
		}

		///* https://github.com/glitchub/arith64/blob/master/arith64.c *///
		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg > 0), size_type> __leading_zeros() const
		{
			if (hi == 0)
				return lo.__leading_zeros() + down_type_bits;
			return hi.__leading_zeros();
		}

		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg == 0), size_type> __leading_zeros() const
		{
			size_type b = 0, n = 0;
			base_type a = (hi) ? hi : ((n += base_bits), lo);
			b = !(a & 0xffffffff00000000ULL) << 5; n += b; a <<= b;
			b = !(a & 0xffff000000000000ULL) << 4; n += b; a <<= b;
			b = !(a & 0xff00000000000000ULL) << 3; n += b; a <<= b;
			b = !(a & 0xf000000000000000ULL) << 2; n += b; a <<= b;
			b = !(a & 0xc000000000000000ULL) << 1; n += b; a <<= b;
			return n + !(a & 0x8000000000000000ULL);
		}

		///* https://github.com/glitchub/arith64/blob/master/arith64.c *///
		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg > 0), size_type> __trailing_zeros() const
		{
			if (lo == 0)
				return hi.__trailing_zeros() + down_type_bits;
			return lo.__trailing_zeros();
		}

		template<std::uint64_t __deg = deg>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			resolved_return_type<(__deg == 0), size_type> __trailing_zeros() const
		{
			size_type b = 0, n = 0;
			base_type a = (lo) ? lo : ((n += base_bits), hi);
			b = !(a & 0x00000000ffffffffULL) << 5; n += b; a >>= b;
			b = !(a & 0x000000000000ffffULL) << 4; n += b; a >>= b;
			b = !(a & 0x00000000000000ffULL) << 3; n += b; a >>= b;
			b = !(a & 0x000000000000000fULL) << 2; n += b; a >>= b;
			b = !(a & 0x0000000000000003ULL) << 1; n += b; a >>= b;
			return n + !(a & 0x0000000000000001ULL);
		}

		///* https://github.com/glitchub/arith64/blob/master/arith64.c *///
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			static self_type __divmod(self_type lhs,
				const self_type& rhs,
				self_type& rem_out, bool assign_rem = true)
		{
			auto& a = lhs;
			const auto& b = rhs;

			if (b > a)
			{
				if (assign_rem)
					rem_out = a;
				return self_type();
			}
			if (b == 0)
				return self_type(); // shrug

			const self_type one(1);
			size_type iter_count = b.__leading_zeros() - a.__leading_zeros() + 1;
			self_type rem = a >> iter_count;
			a <<= bits - iter_count;
			self_type wrap = 0;
			while (iter_count-- > 0)
			{
				rem = ((rem << 1) | (a >> (bits - 1)));
				a = ((a << 1) | (wrap & one));
				wrap = (b > rem) ? self_type() : (~self_type()); // warning! hot spot?
				rem -= (b & wrap);
			}

			if (assign_rem)
				rem_out = rem;
			return (a << 1) | (wrap & one);
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator/(const self_type& rhs) const
		{
			self_type res;
			res = __divmod(*this, rhs, res, false);
			return res;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator%(const self_type& rhs) const
		{
			self_type res;
			__divmod(*this, rhs, res, true);
			return res;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& operator/=(const self_type& rhs)
		{
			return (*this = (*this / rhs));
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type& operator%=(const self_type& rhs)
		{
			return (*this = (*this % rhs));
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS
			self_type operator-() const
		{
			self_type res(*this);
			return (self_type() - res);
		}

		// dumb
		inline static std::string to_string(self_type value)
		{
			constexpr size_type radix_size = 19;
			std::string res_array[bits / (radix_size * 3 /*log2 of 10 ~=~ 3*/) + 1]; // SSO?
			std::string res;
			const self_type conversion_radix(10000000000000000000ull); // max
			//self_type conversion_radix(10000000000ull); // sso optimal
			const self_type zero;
			self_type rem;
			size_type idx = 0;
			while (value != zero)
			{
				value = __divmod(value, conversion_radix, rem);
				res_array[idx] = std::to_string(rem[0]);
				idx++;
			}
			if (!idx)
				return "0";
			bool first_run = true;
			while (idx-- > 0)
			{
				res += ((!first_run && res_array[idx].size() < radix_size) ?
					(std::string(radix_size - res_array[idx].size(), '0')) : "") +
					res_array[idx];
				first_run = false;
			}
			return res;
		}

		template<std::uint64_t __deg, bool frontal_0x = true>
		inline static resolved_return_type<(__deg > 0), std::string>
			to_hex_string(const long_uint<__deg>& value, const bool leading_zeros_flag = false)
		{
			std::string result;
			if (leading_zeros_flag || value.hi)
				result += to_hex_string<__deg - 1, false>(value.hi, leading_zeros_flag);
			result += to_hex_string<__deg - 1, false>(value.lo, true);

			if (!leading_zeros_flag && result.size() > 1 && result.front() == '0')
			{
				auto it = std::find_if(result.begin(), result.end(), [](const char& c) {return c > '0'; });
				if (it == result.end())
					--it;
				if (it != result.begin())
					result = std::string(it, result.end());
			}
			if (frontal_0x)
				result = "0x" + result;
			return result;
		}

		template<std::uint64_t __deg, bool frontal_0x = true>
		inline static resolved_return_type<(__deg == 0), std::string>
			to_hex_string(const long_uint<__deg>& value, const bool leading_zeros_flag = false)
		{
			std::stringstream ss;
			if (leading_zeros_flag || value.hi)
				ss << std::setfill('0') << std::setw(16) << std::hex << value.hi;
			ss << std::setfill('0') << std::setw(16) << std::hex << value.lo;
			auto result = ss.str();

			if (!leading_zeros_flag && result.size() > 1 && result.front() == '0')
			{
				auto it = std::find_if(result.begin(), result.end(), [](const char& c) {return c > '0'; });
				if (it == result.end())
					--it;
				if (it != result.begin())
					result = std::string(it, result.end());
			}
			if (frontal_0x)
				result = "0x" + result;
			return result;
		}

		inline static std::string to_hex_string(const base_type& value)
		{
			std::stringstream ss;
			ss << std::setfill('0') << std::setw(16) << std::hex << value;
			auto result = ss.str();

			if (result.size() > 1 && result.front() == '0')
			{
				auto it = std::find_if(result.begin(), result.end(), [](const char& c) {return c > '0'; });
				if (it == result.end())
					--it;
				if (it != result.begin())
					result = std::string(it, result.end());
			}
			return "0x" + result;
		}

		inline static self_type __from_decimal_char_string(const char* str, size_type str_size)
		{
			self_type value;
			self_type radix = 10;

			while (str_size)
			{
				auto ull_value = *str - '0';
				value *= radix;
				value += self_type(ull_value);

				++str;
				--str_size;
			}

			return value;
		}

		inline static self_type __from_decimal_std_string(const std::string& str)
		{
			return __from_decimal_char_string(str.data(), str.size());
		}

		/*	static self_type __from_hex_string(const char* str, size_type str_size)
			{
				constexpr size_type radix_size = 8;
				if (str_size > 2 && (str[1] == 'x' || str[1] == 'X'))
				{
					str_size -= 2;
					str += 2;
				}
				self_type value;
				size_type leftout_size = std::min(radix_size, str_size);
				char* end = (char*)str + leftout_size;
				while (str_size)
				{
					auto ull_value = std::strtoull(str, &end, 16);
					value <<= 64;
					value |= self_type(ull_value);
					str += leftout_size;
					str_size -= leftout_size;
					leftout_size = std::min(radix_size, str_size);
					end = (char*)str + leftout_size;
				}
				return value;
			}*/
	};

	template <std::uint64_t deg>
	inline std::ostream& operator<<(std::ostream& out, const dixelu::long_uint<deg>& a)
	{
		return (out << a.to_string(a));
	}

	template <std::uint64_t deg>
	inline std::istream& operator>>(std::istream& in, dixelu::long_uint<deg>& a)
	{
		std::string str;
		in >> str;
		a = a.__from_decimal_std_string(str);
		return in;
	}
}

namespace std
{
	template<dixelu::long_uint<0>::size_type deg>
	class numeric_limits<dixelu::long_uint<deg>>
	{
		using base = dixelu::long_uint<deg>;
	public:
		static constexpr bool is_specialized = true;

		static constexpr base min() noexcept { return base(); }
		static constexpr base max() noexcept { return base(~0, base::__fill_fields_tag()); }
		static constexpr base lowest() noexcept { return base(); }

		static constexpr int  digits = base::bits;
		static constexpr int  digits10 = base::bits / 3.32192809488736234787;
		static constexpr bool is_signed = false;
		static constexpr bool is_integer = true;
		static constexpr bool is_exact = true;
		static constexpr int  radix = 2;

		static constexpr base epsilon() noexcept { return base(); }
		static constexpr base round_error() noexcept { return base(); }

		static constexpr int min_exponent = 0;
		static constexpr int min_exponent10 = 0;
		static constexpr int max_exponent = 0;
		static constexpr int max_exponent10 = 0;
		static constexpr int max_digits10 = 0;

		static constexpr bool has_infinity = false;
		static constexpr bool has_quiet_NaN = false;
		static constexpr bool has_signaling_NaN = false;

		static constexpr float_denorm_style has_denorm = denorm_absent;
		static constexpr bool               has_denorm_loss = false;

		static constexpr base infinity() noexcept { return base(); }
		static constexpr base quiet_NaN() noexcept { return base(); }
		static constexpr base signaling_NaN() noexcept { return base(); }
		static constexpr base denorm_min() noexcept { return base(); }

		static constexpr bool is_iec559 = false;
		static constexpr bool is_bounded = true;
		static constexpr bool is_modulo = true;

		static constexpr bool traps = false;
		static constexpr bool tinyness_before = false;
		static constexpr float_round_style round_style = round_toward_zero;
	};
}
#endif //_DIXELU_INTEGERS_H_