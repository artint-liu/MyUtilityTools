// SPDX-License-Identifier: Apache-2.0
// ----------------------------------------------------------------------------
// Copyright 2011-2023 Arm Limited
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy
// of the License at:
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.
// ----------------------------------------------------------------------------

#if !defined(ASTCENC_DECOMPRESS_ONLY)

/**
 * @brief Functions for color quantization.
 *
 * The design of the color quantization functionality requires the caller to use higher level error
 * analysis to determine the base encoding that should be used. This earlier analysis will select
 * the basic type of the endpoint that should be used:
 *
 *     * Mode: LDR or HDR
 *     * Quantization level
 *     * Channel count: L, LA, RGB, or RGBA
 *     * Endpoint 2 type: Direct color endcode, or scaled from endpoint 1.
 *
 * However, this leaves a number of decisions about exactly how to pack the endpoints open. In
 * particular we need to determine if blue contraction can be used, or/and if delta encoding can be
 * used. If they can be applied these will allow us to maintain higher precision in the endpoints
 * without needing additional storage.
 */

#include <stdio.h>
#include <assert.h>

#include "astcenc_internal.h"

/**
 * @brief Compute the error of an LDR RGB or RGBA encoding.
 *
 * @param uquant0    The original endpoint 0 color.
 * @param uquant1    The original endpoint 1 color.
 * @param quant0     The unpacked quantized endpoint 0 color.
 * @param quant1     The unpacked quantized endpoint 1 color.
 *
 * @return The MSE of the encoding.
 */
static float get_rgba_encoding_error(
	vfloat4 uquant0,
	vfloat4 uquant1,
	vint4 quant0,
	vint4 quant1
) {
	vfloat4 error0 = uquant0 - int_to_float(quant0);
	vfloat4 error1 = uquant1 - int_to_float(quant1);
	return hadd_s(error0 * error0 + error1 * error1);
}

/**
 * @brief Determine the quantized value given a quantization level.
 *
 * @param quant_level   The quantization level to use.
 * @param value         The value to convert. This must be in the 0-255 range.
 *
 * @return The unpacked quantized value, returned in 0-255 range.
 */
static inline uint8_t quant_color(
	quant_method quant_level,
	int value
) {
	int index = value * 2 + 1;
	return color_unquant_to_uquant_tables[quant_level - QUANT_6][index];
}

/**
 * @brief Determine the quantized value given a quantization level.
 *
 * @param quant_level   The quantization level to use.
 * @param value         The value to convert. This must be in the 0-255 range.
 *
 * @return The unpacked quantized value, returned in 0-255 range.
 */
static inline vint4 quant_color3(
	quant_method quant_level,
	vint4 value
) {
	vint4 index = value * 2 + 1;
	return vint4(
		color_unquant_to_uquant_tables[quant_level - QUANT_6][index.lane<0>()],
		color_unquant_to_uquant_tables[quant_level - QUANT_6][index.lane<1>()],
		color_unquant_to_uquant_tables[quant_level - QUANT_6][index.lane<2>()],
		0);
}

/**
 * @brief Determine the quantized value given a quantization level and residual.
 *
 * @param quant_level   The quantization level to use.
 * @param value         The value to convert. This must be in the 0-255 range.
 * @param valuef        The original value before rounding, used to compute a residual.
 *
 * @return The unpacked quantized value, returned in 0-255 range.
 */
static inline uint8_t quant_color(
	quant_method quant_level,
	int value,
	float valuef
) {
	int index = value * 2;

	// Compute the residual to determine if we should round down or up ties.
	// Test should be residual >= 0, but empirical testing shows small bias helps.
	float residual = valuef - static_cast<float>(value);
	if (residual >= -0.1f)
	{
		index++;
	}

	return color_unquant_to_uquant_tables[quant_level - QUANT_6][index];
}

/**
 * @brief Determine the quantized value given a quantization level and residual.
 *
 * @param quant_level   The quantization level to use.
 * @param value         The value to convert. This must be in the 0-255 range.
 * @param valuef        The original value before rounding, used to compute a residual.
 *
 * @return The unpacked quantized value, returned in 0-255 range.
 */
static inline vint4 quant_color3(
	quant_method quant_level,
	vint4 value,
	vfloat4 valuef
) {
	vint4 index = value * 2;

	// Compute the residual to determine if we should round down or up ties.
	// Test should be residual >= 0, but empirical testing shows small bias helps.
	vfloat4 residual = valuef - int_to_float(value);
	vmask4 mask = residual >= vfloat4(-0.1f);
	index = select(index, index + 1, mask);

	return vint4(
		color_unquant_to_uquant_tables[quant_level - QUANT_6][index.lane<0>()],
		color_unquant_to_uquant_tables[quant_level - QUANT_6][index.lane<1>()],
		color_unquant_to_uquant_tables[quant_level - QUANT_6][index.lane<2>()],
		0);
}

/**
 * @brief Quantize an LDR RGB color.
 *
 * Since this is a fall-back encoding, we cannot actually fail but must produce a sensible result.
 * For this encoding @c color0 cannot be larger than @c color1. If @c color0 is actually larger
 * than @c color1, @c color0 is reduced and @c color1 is increased until the constraint is met.
 *
 * @param      color0        The input unquantized color0 endpoint.
 * @param      color1        The input unquantized color1 endpoint.
 * @param[out] color0_out    The output quantized color0 endpoint.
 * @param[out] color1_out    The output quantized color1 endpoint.
 * @param      quant_level   The quantization level to use.
 */
static void quantize_rgb(
	vfloat4 color0,
	vfloat4 color1,
	vint4& color0_out,
	vint4& color1_out,
	quant_method quant_level
) {
	vint4 color0i, color1i;
	vfloat4 nudge(0.2f);

	do
	{
		vint4 color0q = max(float_to_int_rtn(color0), vint4(0));
		color0i = quant_color3(quant_level, color0q, color0);
		color0 = color0 - nudge;

		vint4 color1q = min(float_to_int_rtn(color1), vint4(255));
		color1i = quant_color3(quant_level, color1q, color1);
		color1 = color1 + nudge;
	} while (hadd_rgb_s(color0i) > hadd_rgb_s(color1i));

	color0_out = color0i;
	color1_out = color1i;
}

/**
 * @brief Quantize an LDR RGBA color.
 *
 * Since this is a fall-back encoding, we cannot actually fail but must produce a sensible result.
 * For this encoding @c color0.rgb cannot be larger than @c color1.rgb (this indicates blue
 * contraction). If @c color0.rgb is actually larger than @c color1.rgb, @c color0.rgb is reduced
 * and @c color1.rgb is increased until the constraint is met.
 *
 * @param      color0        The input unquantized color0 endpoint.
 * @param      color1        The input unquantized color1 endpoint.
 * @param[out] color0_out    The output quantized color0 endpoint.
 * @param[out] color1_out    The output quantized color1 endpoint.
 * @param      quant_level   The quantization level to use.
 */
static void quantize_rgba(
	vfloat4 color0,
	vfloat4 color1,
	vint4& color0_out,
	vint4& color1_out,
	quant_method quant_level
) {
	quantize_rgb(color0, color1, color0_out, color1_out, quant_level);

	float a0 = color0.lane<3>();
	float a1 = color1.lane<3>();

	color0_out.set_lane<3>(quant_color(quant_level, astc::flt2int_rtn(a0), a0));
	color1_out.set_lane<3>(quant_color(quant_level, astc::flt2int_rtn(a1), a1));
}

/**
 * @brief Try to quantize an LDR RGB color using blue-contraction.
 *
 * Blue-contraction is only usable if encoded color 1 is larger than color 0.
 *
 * @param      color0        The input unquantized color0 endpoint.
 * @param      color1        The input unquantized color1 endpoint.
 * @param[out] color0_out    The output quantized color0 endpoint.
 * @param[out] color1_out    The output quantized color1 endpoint.
 * @param      quant_level   The quantization level to use.
 *
 * @return Returns @c false on failure, @c true on success.
 */
static bool try_quantize_rgb_blue_contract(
	vfloat4 color0,
	vfloat4 color1,
	vint4& color0_out,
	vint4& color1_out,
	quant_method quant_level
) {
	// Apply inverse blue-contraction
	color0 += color0 - color0.swz<2, 2, 2, 3>();
	color1 += color1 - color1.swz<2, 2, 2, 3>();

	// If anything overflows BC cannot be used
	vmask4 color0_error = (color0 < vfloat4(0.0f)) | (color0 > vfloat4(255.0f));
	vmask4 color1_error = (color1 < vfloat4(0.0f)) | (color1 > vfloat4(255.0f));
	if (any(color0_error | color1_error))
	{
		return false;
	}

	// Quantize the inverse blue-contracted color
	vint4 color0i = quant_color3(quant_level, float_to_int_rtn(color0), color0);
	vint4 color1i = quant_color3(quant_level, float_to_int_rtn(color1), color1);

	// If color #1 is not larger than color #0 then blue-contraction cannot be used
	// We must test afterwards because quantization can change the order
	if (hadd_rgb_s(color1i) <= hadd_rgb_s(color0i))
	{
		return false;
	}

	color0_out = color1i;
	color1_out = color0i;
	return true;
}

/**
 * @brief Try to quantize an LDR RGBA color using blue-contraction.
 *
 * Blue-contraction is only usable if encoded color 1 RGB is larger than color 0 RGB.
 *
 * @param      color0        The input unquantized color0 endpoint.
 * @param      color1        The input unquantized color1 endpoint.
 * @param[out] color0_out    The output quantized color0 endpoint.
 * @param[out] color1_out    The output quantized color1 endpoint.
 * @param      quant_level   The quantization level to use.
 *
 * @return Returns @c false on failure, @c true on success.
 */
static bool try_quantize_rgba_blue_contract(
	vfloat4 color0,
	vfloat4 color1,
	vint4& color0_out,
	vint4& color1_out,
	quant_method quant_level
) {
	if (try_quantize_rgb_blue_contract(color0, color1, color0_out, color1_out, quant_level))
	{
		float a0 = color0.lane<3>();
		float a1 = color1.lane<3>();

		color0_out.set_lane<3>(quant_color(quant_level, astc::flt2int_rtn(a1), a1));
		color1_out.set_lane<3>(quant_color(quant_level, astc::flt2int_rtn(a0), a0));

		return true;
	}

	return false;
}

/**
 * @brief Try to quantize an LDR RGB color using delta encoding.
 *
 * At decode time we move one bit from the offset to the base and seize another bit as a sign bit;
 * we then unquantize both values as if they contain one extra bit. If the sum of the offsets is
 * non-negative, then we encode a regular delta.
 *
 * @param      color0        The input unquantized color0 endpoint.
 * @param      color1        The input unquantized color1 endpoint.
 * @param[out] color0_out    The output quantized color0 endpoint.
 * @param[out] color1_out    The output quantized color1 endpoint.
 * @param      quant_level   The quantization level to use.
 *
 * @return Returns @c false on failure, @c true on success.
 */
static bool try_quantize_rgb_delta(
	vfloat4 color0,
	vfloat4 color1,
	vint4& color0_out,
	vint4& color1_out,
	quant_method quant_level
) {
	// Transform color0 to unorm9
	vint4 color0a = float_to_int_rtn(color0);
	color0.set_lane<3>(0.0f);
	color0a = lsl<1>(color0a);

	// Mask off the top bit
	vint4 color0b = color0a & 0xFF;

	// Quantize then unquantize in order to get a value that we take differences against
	vint4 color0be = quant_color3(quant_level, color0b);
	color0b = color0be | (color0a & 0x100);

	// Get hold of the second value
	vint4 color1d = float_to_int_rtn(color1);
	color1d = lsl<1>(color1d);

	// ... and take differences
	color1d = color1d - color0b;
	color1d.set_lane<3>(0);

	// Check if the difference is too large to be encodable
	if (any((color1d > vint4(63)) | (color1d < vint4(-64))))
	{
		return false;
	}

	// Insert top bit of the base into the offset
	color1d = color1d & 0x7F;
	color1d = color1d | lsr<1>(color0b & 0x100);

	// Then quantize and unquantize; if this causes either top two bits to flip, then encoding fails
	// since we have then corrupted either the top bit of the base or the sign bit of the offset
	vint4 color1de = quant_color3(quant_level, color1d);

	vint4 color_flips = (color1d ^ color1de) & 0xC0;
	color_flips.set_lane<3>(0);
	if (any(color_flips != vint4::zero()))
	{
		return false;
	}

	// If the sum of offsets triggers blue-contraction then encoding fails
	vint4 ep0 = color0be;
	vint4 ep1 = color1de;
	bit_transfer_signed(ep1, ep0);
	if (hadd_rgb_s(ep1) < 0)
	{
		return false;
	}

	// Check that the offsets produce legitimate sums as well
	ep0 = ep0 + ep1;
	if (any((ep0 < vint4(0)) | (ep0 > vint4(0xFF))))
	{
		return false;
	}

	color0_out = color0be;
	color1_out = color1de;
	return true;
}

/**
 * @brief Try to quantize an LDR RGB color using delta encoding and blue-contraction.
 *
 * Blue-contraction is only usable if encoded color 1 RGB is larger than color 0 RGB.
 *
 * @param      color0        The input unquantized color0 endpoint.
 * @param      color1        The input unquantized color1 endpoint.
 * @param[out] color0_out    The output quantized color0 endpoint.
 * @param[out] color1_out    The output quantized color1 endpoint.
 * @param      quant_level   The quantization level to use.
 *
 * @return Returns @c false on failure, @c true on success.
 */
static bool try_quantize_rgb_delta_blue_contract(
	vfloat4 color0,
	vfloat4 color1,
	vint4& color0_out,
	vint4& color1_out,
	quant_method quant_level
) {
	// Note: Switch around endpoint colors already at start
	std::swap(color0, color1);

	// Apply inverse blue-contraction
	color0 += color0 - color0.swz<2, 2, 2, 3>();
	color1 += color1 - color1.swz<2, 2, 2, 3>();

	// If anything overflows BC cannot be used
	vmask4 color0_error = (color0 < vfloat4(0.0f)) | (color0 > vfloat4(255.0f));
	vmask4 color1_error = (color1 < vfloat4(0.0f)) | (color1 > vfloat4(255.0f));
	if (any(color0_error | color1_error))
	{
		return false;
	}

	// Transform color0 to unorm9
	vint4 color0a = float_to_int_rtn(color0);
	color0.set_lane<3>(0.0f);
	color0a = lsl<1>(color0a);

	// Mask off the top bit
	vint4 color0b = color0a & 0xFF;

	// Quantize then unquantize in order to get a value that we take differences against
	vint4 color0be = quant_color3(quant_level, color0b);
	color0b = color0be | (color0a & 0x100);

	// Get hold of the second value
	vint4 color1d = float_to_int_rtn(color1);
	color1d = lsl<1>(color1d);

	// ... and take differences
	color1d = color1d - color0b;
	color1d.set_lane<3>(0);

	// Check if the difference is too large to be encodable
	if (any((color1d > vint4(63)) | (color1d < vint4(-64))))
	{
		return false;
	}

	// Insert top bit of the base into the offset
	color1d = color1d & 0x7F;
	color1d = color1d | lsr<1>(color0b & 0x100);

	// Then quantize and unquantize; if this causes either top two bits to flip, then encoding fails
	// since we have then corrupted either the top bit of the base or the sign bit of the offset
	vint4 color1de = quant_color3(quant_level, color1d);

	vint4 color_flips = (color1d ^ color1de) & 0xC0;
	color_flips.set_lane<3>(0);
	if (any(color_flips != vint4::zero()))
	{
		return false;
	}

	// If the sum of offsets does not trigger blue-contraction then encoding fails
	vint4 ep0 = color0be;
	vint4 ep1 = color1de;
	bit_transfer_signed(ep1, ep0);
	if (hadd_rgb_s(ep1) >= 0)
	{
		return false;
	}

	// Check that the offsets produce legitimate sums as well
	ep0 = ep0 + ep1;
	if (any((ep0 < vint4(0)) | (ep0 > vint4(0xFF))))
	{
		return false;
	}

	color0_out = color0be;
	color1_out = color1de;
	return true;
}

/**
 * @brief Try to quantize an LDR A color using delta encoding.
 *
 * At decode time we move one bit from the offset to the base and seize another bit as a sign bit;
 * we then unquantize both values as if they contain one extra bit. If the sum of the offsets is
 * non-negative, then we encode a regular delta.
 *
 * This function only compressed the alpha - the other elements in the output array are not touched.
 *
 * @param      color0        The input unquantized color0 endpoint.
 * @param      color1        The input unquantized color1 endpoint.
 * @param[out] color0_out    The output quantized color0 endpoint; must preserve lane 0/1/2.
 * @param[out] color1_out    The output quantized color1 endpoint; must preserve lane 0/1/2.
 * @param      quant_level   The quantization level to use.
 *
 * @return Returns @c false on failure, @c true on success.
 */
static bool try_quantize_alpha_delta(
	vfloat4 color0,
	vfloat4 color1,
	vint4& color0_out,
	vint4& color1_out,
	quant_method quant_level
) {
	float a0 = color0.lane<3>();
	float a1 = color1.lane<3>();

	int a0a = astc::flt2int_rtn(a0);
	a0a <<= 1;
	int a0b = a0a & 0xFF;
	int a0be = quant_color(quant_level, a0b);
	a0b = a0be;
	a0b |= a0a & 0x100;
	int a1d = astc::flt2int_rtn(a1);
	a1d <<= 1;
	a1d -= a0b;

	if (a1d > 63 || a1d < -64)
	{
		return false;
	}

	a1d &= 0x7F;
	a1d |= (a0b & 0x100) >> 1;

	int a1de = quant_color(quant_level, a1d);
	int a1du = a1de;
	if ((a1d ^ a1du) & 0xC0)
	{
		return false;
	}

	a1du &= 0x7F;
	if (a1du & 0x40)
	{
		a1du -= 0x80;
	}

	a1du += a0b;
	if (a1du < 0 || a1du > 0x1FF)
	{
		return false;
	}

	color0_out.set_lane<3>(a0be);
	color1_out.set_lane<3>(a1de);

	return true;
}

/**
 * @brief Try to quantize an LDR LA color using delta encoding.
 *
 * At decode time we move one bit from the offset to the base and seize another bit as a sign bit;
 * we then unquantize both values as if they contain one extra bit. If the sum of the offsets is
 * non-negative, then we encode a regular delta.
 *
 * This function only compressed the alpha - the other elements in the output array are not touched.
 *
 * @param      color0        The input unquantized color0 endpoint.
 * @param      color1        The input unquantized color1 endpoint.
 * @param[out] output        The output endpoints, returned as (l0, l1, a0, a1).
 * @param      quant_level   The quantization level to use.
 *
 * @return Returns @c false on failure, @c true on success.
 */
static bool try_quantize_luminance_alpha_delta(
	vfloat4 color0,
	vfloat4 color1,
	uint8_t output[4],
	quant_method quant_level
) {
	float l0 = hadd_rgb_s(color0) * (1.0f / 3.0f);
	float l1 = hadd_rgb_s(color1) * (1.0f / 3.0f);

	float a0 = color0.lane<3>();
	float a1 = color1.lane<3>();

	int l0a = astc::flt2int_rtn(l0);
	int a0a = astc::flt2int_rtn(a0);
	l0a <<= 1;
	a0a <<= 1;

	int l0b = l0a & 0xFF;
	int a0b = a0a & 0xFF;
	int l0be = quant_color(quant_level, l0b);
	int a0be = quant_color(quant_level, a0b);
	l0b = l0be;
	a0b = a0be;
	l0b |= l0a & 0x100;
	a0b |= a0a & 0x100;

	int l1d = astc::flt2int_rtn(l1);
	int a1d = astc::flt2int_rtn(a1);
	l1d <<= 1;
	a1d <<= 1;
	l1d -= l0b;
	a1d -= a0b;

	if (l1d > 63 || l1d < -64)
	{
		return false;
	}

	if (a1d > 63 || a1d < -64)
	{
		return false;
	}

	l1d &= 0x7F;
	a1d &= 0x7F;
	l1d |= (l0b & 0x100) >> 1;
	a1d |= (a0b & 0x100) >> 1;

	int l1de = quant_color(quant_level, l1d);
	int a1de = quant_color(quant_level, a1d);
	int l1du = l1de;
	int a1du = a1de;

	if ((l1d ^ l1du) & 0xC0)
	{
		return false;
	}

	if ((a1d ^ a1du) & 0xC0)
	{
		return false;
	}

	l1du &= 0x7F;
	a1du &= 0x7F;

	if (l1du & 0x40)
	{
		l1du -= 0x80;
	}

	if (a1du & 0x40)
	{
		a1du -= 0x80;
	}

	l1du += l0b;
	a1du += a0b;

	if (l1du < 0 || l1du > 0x1FF)
	{
		return false;
	}

	if (a1du < 0 || a1du > 0x1FF)
	{
		return false;
	}

	output[0] = static_cast<uint8_t>(l0be);
	output[1] = static_cast<uint8_t>(l1de);
	output[2] = static_cast<uint8_t>(a0be);
	output[3] = static_cast<uint8_t>(a1de);

	return true;
}

/**
 * @brief Try to quantize an LDR RGBA color using delta encoding.
 *
 * At decode time we move one bit from the offset to the base and seize another bit as a sign bit;
 * we then unquantize both values as if they contain one extra bit. If the sum of the offsets is
 * non-negative, then we encode a regular delta.
 *
 * This function only compressed the alpha - the other elements in the output array are not touched.
 *
 * @param      color0        The input unquantized color0 endpoint.
 * @param      color1        The input unquantized color1 endpoint.
 * @param[out] color0_out   The output quantized color0 endpoint
 * @param[out] color1_out   The output quantized color1 endpoint
 * @param      quant_level   The quantization level to use.
 *
 * @return Returns @c false on failure, @c true on success.
 */
static bool try_quantize_rgba_delta(
	vfloat4 color0,
	vfloat4 color1,
	vint4& color0_out,
	vint4& color1_out,
	quant_method quant_level
) {
	return try_quantize_rgb_delta(color0, color1, color0_out, color1_out, quant_level) &&
	       try_quantize_alpha_delta(color0, color1, color0_out, color1_out, quant_level);
}

/**
 * @brief Try to quantize an LDR RGBA color using delta and blue contract encoding.
 *
 * At decode time we move one bit from the offset to the base and seize another bit as a sign bit;
 * we then unquantize both values as if they contain one extra bit. If the sum of the offsets is
 * non-negative, then we encode a regular delta.
 *
 * This function only compressed the alpha - the other elements in the output array are not touched.
 *
 * @param      color0       The input unquantized color0 endpoint.
 * @param      color1       The input unquantized color1 endpoint.
 * @param[out] color0_out   The output quantized color0 endpoint
 * @param[out] color1_out   The output quantized color1 endpoint
 * @param      quant_level  The quantization level to use.
 *
 * @return Returns @c false on failure, @c true on success.
 */
static bool try_quantize_rgba_delta_blue_contract(
	vfloat4 color0,
	vfloat4 color1,
	vint4& color0_out,
	vint4& color1_out,
	quant_method quant_level
) {
	// Note that we swap the color0 and color1 ordering for alpha to match RGB blue-contract
	return try_quantize_rgb_delta_blue_contract(color0, color1, color0_out, color1_out, quant_level) &&
	       try_quantize_alpha_delta(color1, color0, color0_out, color1_out, quant_level);
}

/**
 * @brief Quantize an LDR RGB color using scale encoding.
 *
 * @param      color         The input unquantized color endpoint and scale factor.
 * @param[out] output        The output endpoints, returned as (r0, g0, b0, s).
 * @param      quant_level   The quantization level to use.
 */
static void quantize_rgbs(
	vfloat4 color,
	uint8_t output[4],
	quant_method quant_level
) {
	float scale = 1.0f / 257.0f;

	float r = astc::clamp255f(color.lane<0>() * scale);
	float g = astc::clamp255f(color.lane<1>() * scale);
	float b = astc::clamp255f(color.lane<2>() * scale);

	int ri = quant_color(quant_level, astc::flt2int_rtn(r), r);
	int gi = quant_color(quant_level, astc::flt2int_rtn(g), g);
	int bi = quant_color(quant_level, astc::flt2int_rtn(b), b);

	float oldcolorsum = hadd_rgb_s(color) * scale;
	float newcolorsum = static_cast<float>(ri + gi + bi);

	float scalea = astc::clamp1f(color.lane<3>() * (oldcolorsum + 1e-10f) / (newcolorsum + 1e-10f));
	int scale_idx = astc::flt2int_rtn(scalea * 256.0f);
	scale_idx = astc::clamp(scale_idx, 0, 255);

	output[0] = static_cast<uint8_t>(ri);
	output[1] = static_cast<uint8_t>(gi);
	output[2] = static_cast<uint8_t>(bi);
	output[3] = quant_color(quant_level, scale_idx);
}

/**
 * @brief Quantize an LDR RGBA color using scale encoding.
 *
 * @param      color0       The input unquantized color0 alpha endpoint.
 * @param      color1       The input unquantized color1 alpha endpoint.
 * @param      color        The input unquantized color endpoint and scale factor.
 * @param[out] output       The output endpoints, returned as (r0, g0, b0, s, a0, a1).
 * @param      quant_level  The quantization level to use.
 */
static void quantize_rgbs_alpha(
	vfloat4 color0,
	vfloat4 color1,
	vfloat4 color,
	uint8_t output[6],
	quant_method quant_level
) {
	float a0 = color0.lane<3>();
	float a1 = color1.lane<3>();

	output[4] = quant_color(quant_level, astc::flt2int_rtn(a0), a0);
	output[5] = quant_color(quant_level, astc::flt2int_rtn(a1), a1);

	quantize_rgbs(color, output, quant_level);
}

/**
 * @brief Quantize a LDR L color.
 *
 * @param      color0        The input unquantized color0 endpoint.
 * @param      color1        The input unquantized color1 endpoint.
 * @param[out] output        The output endpoints, returned as (l0, l1).
 * @param      quant_level   The quantization level to use.
 */
static void quantize_luminance(
	vfloat4 color0,
	vfloat4 color1,
	uint8_t output[2],
	quant_method quant_level
) {
	float lum0 = hadd_rgb_s(color0) * (1.0f / 3.0f);
	float lum1 = hadd_rgb_s(color1) * (1.0f / 3.0f);

	if (lum0 > lum1)
	{
		float avg = (lum0 + lum1) * 0.5f;
		lum0 = avg;
		lum1 = avg;
	}

	output[0] = quant_color(quant_level, astc::flt2int_rtn(lum0), lum0);
	output[1] = quant_color(quant_level, astc::flt2int_rtn(lum1), lum1);
}

/**
 * @brief Quantize a LDR LA color.
 *
 * @param      color0        The input unquantized color0 endpoint.
 * @param      color1        The input unquantized color1 endpoint.
 * @param[out] output        The output endpoints, returned as (l0, l1, a0, a1).
 * @param      quant_level   The quantization level to use.
 */
static void quantize_luminance_alpha(
	vfloat4 color0,
	vfloat4 color1,
	uint8_t output[4],
	quant_method quant_level
) {
	float lum0 = hadd_rgb_s(color0) * (1.0f / 3.0f);
	float lum1 = hadd_rgb_s(color1) * (1.0f / 3.0f);

	float a0 = color0.lane<3>();
	float a1 = color1.lane<3>();

	output[0] = quant_color(quant_level, astc::flt2int_rtn(lum0), lum0);
	output[1] = quant_color(quant_level, astc::flt2int_rtn(lum1), lum1);
	output[2] = quant_color(quant_level, astc::flt2int_rtn(a0), a0);
	output[3] = quant_color(quant_level, astc::flt2int_rtn(a1), a1);
}

/**
 * @brief Quantize and unquantize a value ensuring top two bits are the same.
 *
 * @param      quant_level     The quantization level to use.
 * @param      value           The input unquantized value.
 * @param[out] quant_value     The quantized value.
 */
static inline void quantize_and_unquantize_retain_top_two_bits(
	quant_method quant_level,
	uint8_t value,
	uint8_t& quant_value
) {
	int perform_loop;
	uint8_t quantval;

	do
	{
		quantval = quant_color(quant_level, value);

		// Perform looping if the top two bits were modified by quant/unquant
		perform_loop = (value & 0xC0) != (quantval & 0xC0);

		if ((quantval & 0xC0) > (value & 0xC0))
		{
			// Quant/unquant rounded UP so that the top two bits changed;
			// decrement the input in hopes that this will avoid rounding up.
			value--;
		}
		else if ((quantval & 0xC0) < (value & 0xC0))
		{
			// Quant/unquant rounded DOWN so that the top two bits changed;
			// decrement the input in hopes that this will avoid rounding down.
			value--;
		}
	} while (perform_loop);

	quant_value = quantval;
}

/**
 * @brief Quantize and unquantize a value ensuring top four bits are the same.
 *
 * @param      quant_level     The quantization level to use.
 * @param      value           The input unquantized value.
 * @param[out] quant_value     The quantized value in 0-255 range.
 */
static inline void quantize_and_unquantize_retain_top_four_bits(
	quant_method quant_level,
	uint8_t value,
	uint8_t& quant_value
) {
	uint8_t perform_loop;
	uint8_t quantval;

	do
	{
		quantval = quant_color(quant_level, value);
		// Perform looping if the top four bits were modified by quant/unquant
		perform_loop = (value & 0xF0) != (quantval & 0xF0);

		if ((quantval & 0xF0) > (value & 0xF0))
		{
			// Quant/unquant rounded UP so that the top four bits changed;
			// decrement the input value in hopes that this will avoid rounding up.
			value--;
		}
		else if ((quantval & 0xF0) < (value & 0xF0))
		{
			// Quant/unquant rounded DOWN so that the top four bits changed;
			// decrement the input value in hopes that this will avoid rounding down.
			value--;
		}
	} while (perform_loop);

	quant_value = quantval;
}


/* See header for documentation. */
uint8_t pack_color_endpoints(
	vfloat4 color0,
	vfloat4 color1,
	vfloat4 rgbs_color,
	vfloat4 rgbo_color,
	int format,
	uint8_t* output,
	quant_method quant_level
) {
	assert(QUANT_6 <= quant_level && quant_level <= QUANT_256);

	// Clamp colors to a valid LDR range
	// Note that HDR has a lower max, handled in the conversion functions
	color0 = clamp(0.0f, 65535.0f, color0);
	color1 = clamp(0.0f, 65535.0f, color1);

	// Pre-scale the LDR value we need to the 0-255 quantizable range
	vfloat4 color0_ldr = color0 * (1.0f  / 257.0f);
	vfloat4 color1_ldr = color1 * (1.0f  / 257.0f);

	uint8_t retval = 0;
	float best_error = ERROR_CALC_DEFAULT;
	vint4 color0_out, color1_out;
	vint4 color0_out2, color1_out2;

	switch (format)
	{
	case FMT_RGB:
		if (quant_level <= QUANT_160)
		{
			if (try_quantize_rgb_delta_blue_contract(color0_ldr, color1_ldr, color0_out, color1_out, quant_level))
			{
				vint4 color0_unpack;
				vint4 color1_unpack;
				rgba_delta_unpack(color0_out, color1_out, color0_unpack, color1_unpack);

				retval = FMT_RGB_DELTA;
				best_error = get_rgba_encoding_error(color0_ldr, color1_ldr, color0_unpack, color1_unpack);
			}

			if (try_quantize_rgb_delta(color0_ldr, color1_ldr, color0_out2, color1_out2, quant_level))
			{
				vint4 color0_unpack;
				vint4 color1_unpack;
				rgba_delta_unpack(color0_out2, color1_out2, color0_unpack, color1_unpack);

				float error = get_rgba_encoding_error(color0_ldr, color1_ldr, color0_unpack, color1_unpack);
				if (error < best_error)
				{
					retval = FMT_RGB_DELTA;
					best_error = error;
					color0_out = color0_out2;
					color1_out = color1_out2;
				}
			}
		}

		if (quant_level < QUANT_256)
		{
			if (try_quantize_rgb_blue_contract(color0_ldr, color1_ldr, color0_out2, color1_out2, quant_level))
			{
				vint4 color0_unpack;
				vint4 color1_unpack;
				rgba_unpack(color0_out2, color1_out2, color0_unpack, color1_unpack);

				float error = get_rgba_encoding_error(color0_ldr, color1_ldr, color0_unpack, color1_unpack);
				if (error < best_error)
				{
					retval = FMT_RGB;
					best_error = error;
					color0_out = color0_out2;
					color1_out = color1_out2;
				}
			}
		}

		{
			quantize_rgb(color0_ldr, color1_ldr, color0_out2, color1_out2, quant_level);

			vint4 color0_unpack;
			vint4 color1_unpack;
			rgba_unpack(color0_out2, color1_out2, color0_unpack, color1_unpack);

			float error = get_rgba_encoding_error(color0_ldr, color1_ldr, color0_unpack, color1_unpack);
			if (error < best_error)
			{
				retval =  FMT_RGB;
				color0_out = color0_out2;
				color1_out = color1_out2;
			}
		}

		// TODO: Can we vectorize this?
		output[0] = static_cast<uint8_t>(color0_out.lane<0>());
		output[1] = static_cast<uint8_t>(color1_out.lane<0>());
		output[2] = static_cast<uint8_t>(color0_out.lane<1>());
		output[3] = static_cast<uint8_t>(color1_out.lane<1>());
		output[4] = static_cast<uint8_t>(color0_out.lane<2>());
		output[5] = static_cast<uint8_t>(color1_out.lane<2>());
		break;

	case FMT_RGBA:
		if (quant_level <= QUANT_160)
		{
			if (try_quantize_rgba_delta_blue_contract(color0_ldr, color1_ldr, color0_out, color1_out, quant_level))
			{
				vint4 color0_unpack;
				vint4 color1_unpack;
				rgba_delta_unpack(color0_out, color1_out, color0_unpack, color1_unpack);

				retval = FMT_RGBA_DELTA;
				best_error = get_rgba_encoding_error(color0_ldr, color1_ldr, color0_unpack, color1_unpack);
			}

			if (try_quantize_rgba_delta(color0_ldr, color1_ldr, color0_out2, color1_out2, quant_level))
			{
				vint4 color0_unpack;
				vint4 color1_unpack;
				rgba_delta_unpack(color0_out2, color1_out2, color0_unpack, color1_unpack);

				float error = get_rgba_encoding_error(color0_ldr, color1_ldr, color0_unpack, color1_unpack);
				if (error < best_error)
				{
					retval = FMT_RGBA_DELTA;
					best_error = error;
					color0_out = color0_out2;
					color1_out = color1_out2;
				}
			}
		}

		if (quant_level < QUANT_256)
		{
			if (try_quantize_rgba_blue_contract(color0_ldr, color1_ldr, color0_out2, color1_out2, quant_level))
			{
				vint4 color0_unpack;
				vint4 color1_unpack;
				rgba_unpack(color0_out2, color1_out2, color0_unpack, color1_unpack);

				float error = get_rgba_encoding_error(color0_ldr, color1_ldr, color0_unpack, color1_unpack);
				if (error < best_error)
				{
					retval = FMT_RGBA;
					best_error = error;
					color0_out = color0_out2;
					color1_out = color1_out2;
				}
			}
		}

		{
			quantize_rgba(color0_ldr, color1_ldr, color0_out2, color1_out2, quant_level);

			vint4 color0_unpack;
			vint4 color1_unpack;
			rgba_unpack(color0_out2, color1_out2, color0_unpack, color1_unpack);

			float error = get_rgba_encoding_error(color0_ldr, color1_ldr, color0_unpack, color1_unpack);
			if (error < best_error)
			{
				retval =  FMT_RGBA;
				color0_out = color0_out2;
				color1_out = color1_out2;
			}
		}

		// TODO: Can we vectorize this?
		output[0] = static_cast<uint8_t>(color0_out.lane<0>());
		output[1] = static_cast<uint8_t>(color1_out.lane<0>());
		output[2] = static_cast<uint8_t>(color0_out.lane<1>());
		output[3] = static_cast<uint8_t>(color1_out.lane<1>());
		output[4] = static_cast<uint8_t>(color0_out.lane<2>());
		output[5] = static_cast<uint8_t>(color1_out.lane<2>());
		output[6] = static_cast<uint8_t>(color0_out.lane<3>());
		output[7] = static_cast<uint8_t>(color1_out.lane<3>());
		break;

	case FMT_RGB_SCALE:
		quantize_rgbs(rgbs_color, output, quant_level);
		retval = FMT_RGB_SCALE;
		break;

	//case FMT_HDR_RGB_SCALE:
	//	quantize_hdr_rgbo(rgbo_color, output, quant_level);
	//	retval = FMT_HDR_RGB_SCALE;
	//	break;

	//case FMT_HDR_RGB:
	//	quantize_hdr_rgb(color0, color1, output, quant_level);
	//	retval = FMT_HDR_RGB;
	//	break;

	case FMT_RGB_SCALE_ALPHA:
		quantize_rgbs_alpha(color0_ldr, color1_ldr, rgbs_color, output, quant_level);
		retval = FMT_RGB_SCALE_ALPHA;
		break;

	//case FMT_HDR_LUMINANCE_SMALL_RANGE:
	//case FMT_HDR_LUMINANCE_LARGE_RANGE:
	//	if (try_quantize_hdr_luminance_small_range(color0, color1, output, quant_level))
	//	{
	//		retval = FMT_HDR_LUMINANCE_SMALL_RANGE;
	//		break;
	//	}
	//	quantize_hdr_luminance_large_range(color0, color1, output, quant_level);
	//	retval = FMT_HDR_LUMINANCE_LARGE_RANGE;
	//	break;

	case FMT_LUMINANCE:
		quantize_luminance(color0_ldr, color1_ldr, output, quant_level);
		retval = FMT_LUMINANCE;
		break;

	case FMT_LUMINANCE_ALPHA:
		if (quant_level <= 18)
		{
			if (try_quantize_luminance_alpha_delta(color0_ldr, color1_ldr, output, quant_level))
			{
				retval = FMT_LUMINANCE_ALPHA_DELTA;
				break;
			}
		}
		quantize_luminance_alpha(color0_ldr, color1_ldr, output, quant_level);
		retval = FMT_LUMINANCE_ALPHA;
		break;

	//case FMT_HDR_RGB_LDR_ALPHA:
	//	quantize_hdr_rgb_ldr_alpha(color0, color1, output, quant_level);
	//	retval = FMT_HDR_RGB_LDR_ALPHA;
	//	break;

	//case FMT_HDR_RGBA:
	//	quantize_hdr_rgb_alpha(color0, color1, output, quant_level);
	//	retval = FMT_HDR_RGBA;
	//	break;
	}

	return retval;
}

#endif
