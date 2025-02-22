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

#include <utility>

/**
 * @brief Functions for color unquantization.
 */

#include "astcenc_internal.h"

/**
 * @brief Un-blue-contract a color.
 *
 * This function reverses any applied blue contraction.
 *
 * @param input   The input color that has been blue-contracted.
 *
 * @return The uncontracted color.
 */
static ASTCENC_SIMD_INLINE vint4 uncontract_color(
	vint4 input
) {
	vmask4 mask(true, true, false, false);
	vint4 bc0 = asr<1>(input + input.lane<2>());
	return select(input, bc0, mask);
}

void rgba_delta_unpack(
	vint4 input0,
	vint4 input1,
	vint4& output0,
	vint4& output1
) {
	// Apply bit transfer
	bit_transfer_signed(input1, input0);

	// Apply blue-uncontraction if needed
	int rgb_sum = hadd_rgb_s(input1);
	input1 = input1 + input0;
	if (rgb_sum < 0)
	{
		input0 = uncontract_color(input0);
		input1 = uncontract_color(input1);
		std::swap(input0, input1);
	}

	output0 = clamp(0, 255, input0);
	output1 = clamp(0, 255, input1);
}

/**
 * @brief Unpack an LDR RGB color that uses delta encoding.
 *
 * Output alpha set to 255.
 *
 * @param      input0    The packed endpoint 0 color.
 * @param      input1    The packed endpoint 1 color deltas.
 * @param[out] output0   The unpacked endpoint 0 color.
 * @param[out] output1   The unpacked endpoint 1 color.
 */
static void rgb_delta_unpack(
	vint4 input0,
	vint4 input1,
	vint4& output0,
	vint4& output1
) {
	rgba_delta_unpack(input0, input1, output0, output1);
	output0.set_lane<3>(255);
	output1.set_lane<3>(255);
}

void rgba_unpack(
	vint4 input0,
	vint4 input1,
	vint4& output0,
	vint4& output1
) {
	// Apply blue-uncontraction if needed
	if (hadd_rgb_s(input0) > hadd_rgb_s(input1))
	{
		input0 = uncontract_color(input0);
		input1 = uncontract_color(input1);
		std::swap(input0, input1);
	}

	output0 = input0;
	output1 = input1;
}

/**
 * @brief Unpack an LDR RGB color that uses direct encoding.
 *
 * Output alpha set to 255.
 *
 * @param      input0    The packed endpoint 0 color.
 * @param      input1    The packed endpoint 1 color.
 * @param[out] output0   The unpacked endpoint 0 color.
 * @param[out] output1   The unpacked endpoint 1 color.
 */
static void rgb_unpack(
	vint4 input0,
	vint4 input1,
	vint4& output0,
	vint4& output1
) {
	rgba_unpack(input0, input1, output0, output1);
	output0.set_lane<3>(255);
	output1.set_lane<3>(255);
}

/**
 * @brief Unpack an LDR RGBA color that uses scaled encoding.
 *
 * Note only the RGB channels use the scaled encoding, alpha uses direct.
 *
 * @param      input0    The packed endpoint 0 color.
 * @param      alpha1    The packed endpoint 1 alpha value.
 * @param      scale     The packed quantized scale.
 * @param[out] output0   The unpacked endpoint 0 color.
 * @param[out] output1   The unpacked endpoint 1 color.
 */
static void rgb_scale_alpha_unpack(
	vint4 input0,
	uint8_t alpha1,
	uint8_t scale,
	vint4& output0,
	vint4& output1
) {
	output1 = input0;
	output1.set_lane<3>(alpha1);

	output0 = asr<8>(input0 * scale);
	output0.set_lane<3>(input0.lane<3>());
}

/**
 * @brief Unpack an LDR RGB color that uses scaled encoding.
 *
 * Output alpha is 255.
 *
 * @param      input0    The packed endpoint 0 color.
 * @param      scale     The packed scale.
 * @param[out] output0   The unpacked endpoint 0 color.
 * @param[out] output1   The unpacked endpoint 1 color.
 */
static void rgb_scale_unpack(
	vint4 input0,
	int scale,
	vint4& output0,
	vint4& output1
) {
	output1 = input0;
	output1.set_lane<3>(255);

	output0 = asr<8>(input0 * scale);
	output0.set_lane<3>(255);
}

/**
 * @brief Unpack an LDR L color that uses direct encoding.
 *
 * Output alpha is 255.
 *
 * @param      input     The packed endpoints.
 * @param[out] output0   The unpacked endpoint 0 color.
 * @param[out] output1   The unpacked endpoint 1 color.
 */
static void luminance_unpack(
	const uint8_t input[2],
	vint4& output0,
	vint4& output1
) {
	int lum0 = input[0];
	int lum1 = input[1];
	output0 = vint4(lum0, lum0, lum0, 255);
	output1 = vint4(lum1, lum1, lum1, 255);
}

/**
 * @brief Unpack an LDR L color that uses delta encoding.
 *
 * Output alpha is 255.
 *
 * @param      input     The packed endpoints (L0, L1).
 * @param[out] output0   The unpacked endpoint 0 color.
 * @param[out] output1   The unpacked endpoint 1 color.
 */
static void luminance_delta_unpack(
	const uint8_t input[2],
	vint4& output0,
	vint4& output1
) {
	int v0 = input[0];
	int v1 = input[1];
	int l0 = (v0 >> 2) | (v1 & 0xC0);
	int l1 = l0 + (v1 & 0x3F);

	l1 = astc::min(l1, 255);

	output0 = vint4(l0, l0, l0, 255);
	output1 = vint4(l1, l1, l1, 255);
}

/**
 * @brief Unpack an LDR LA color that uses direct encoding.
 *
 * @param      input     The packed endpoints (L0, L1, A0, A1).
 * @param[out] output0   The unpacked endpoint 0 color.
 * @param[out] output1   The unpacked endpoint 1 color.
 */
static void luminance_alpha_unpack(
	const uint8_t input[4],
	vint4& output0,
	vint4& output1
) {
	int lum0 = input[0];
	int lum1 = input[1];
	int alpha0 = input[2];
	int alpha1 = input[3];
	output0 = vint4(lum0, lum0, lum0, alpha0);
	output1 = vint4(lum1, lum1, lum1, alpha1);
}

/**
 * @brief Unpack an LDR LA color that uses delta encoding.
 *
 * @param      input     The packed endpoints (L0, L1, A0, A1).
 * @param[out] output0   The unpacked endpoint 0 color.
 * @param[out] output1   The unpacked endpoint 1 color.
 */
static void luminance_alpha_delta_unpack(
	const uint8_t input[4],
	vint4& output0,
	vint4& output1
) {
	int lum0 = input[0];
	int lum1 = input[1];
	int alpha0 = input[2];
	int alpha1 = input[3];

	lum0 |= (lum1 & 0x80) << 1;
	alpha0 |= (alpha1 & 0x80) << 1;
	lum1 &= 0x7F;
	alpha1 &= 0x7F;

	if (lum1 & 0x40)
	{
		lum1 -= 0x80;
	}

	if (alpha1 & 0x40)
	{
		alpha1 -= 0x80;
	}

	lum0 >>= 1;
	lum1 >>= 1;
	alpha0 >>= 1;
	alpha1 >>= 1;
	lum1 += lum0;
	alpha1 += alpha0;

	lum1 = astc::clamp(lum1, 0, 255);
	alpha1 = astc::clamp(alpha1, 0, 255);

	output0 = vint4(lum0, lum0, lum0, alpha0);
	output1 = vint4(lum1, lum1, lum1, alpha1);
}







/**
 * @brief Unpack an HDR RGBA direct encoding.
 *
 * @param      input     The packed endpoints (packed and modal).
 * @param[out] output0   The unpacked endpoint 0 color.
 * @param[out] output1   The unpacked endpoint 1 color.
 */

/* See header for documentation. */
void unpack_color_endpoints(
	astcenc_profile decode_mode,
	int format,
	const uint8_t* input,
	bool& rgb_hdr,
	bool& alpha_hdr,
	vint4& output0,
	vint4& output1
) {
	// Assume no NaNs and LDR endpoints unless set later
	rgb_hdr = false;
	alpha_hdr = false;

	bool alpha_hdr_default = false;

	switch (format)
	{
	case FMT_LUMINANCE:
		luminance_unpack(input, output0, output1);
		break;

	case FMT_LUMINANCE_DELTA:
		luminance_delta_unpack(input, output0, output1);
		break;

	//case FMT_HDR_LUMINANCE_SMALL_RANGE:
	//	rgb_hdr = true;
	//	alpha_hdr_default = true;
	//	hdr_luminance_small_range_unpack(input, output0, output1);
	//	break;

	//case FMT_HDR_LUMINANCE_LARGE_RANGE:
	//	rgb_hdr = true;
	//	alpha_hdr_default = true;
	//	hdr_luminance_large_range_unpack(input, output0, output1);
	//	break;

	case FMT_LUMINANCE_ALPHA:
		luminance_alpha_unpack(input, output0, output1);
		break;

	case FMT_LUMINANCE_ALPHA_DELTA:
		luminance_alpha_delta_unpack(input, output0, output1);
		break;

	case FMT_RGB_SCALE:
		{
			vint4 input0q(input[0], input[1], input[2], 0);
			uint8_t scale = input[3];
			rgb_scale_unpack(input0q, scale, output0, output1);
		}
		break;

	case FMT_RGB_SCALE_ALPHA:
		{
			vint4 input0q(input[0], input[1], input[2], input[4]);
			uint8_t alpha1q = input[5];
			uint8_t scaleq = input[3];
			rgb_scale_alpha_unpack(input0q, alpha1q, scaleq, output0, output1);
		}
		break;

	//case FMT_HDR_RGB_SCALE:
	//	rgb_hdr = true;
	//	alpha_hdr_default = true;
	//	hdr_rgbo_unpack(input, output0, output1);
	//	break;

	case FMT_RGB:
		{
			vint4 input0q(input[0], input[2], input[4], 0);
			vint4 input1q(input[1], input[3], input[5], 0);
			rgb_unpack(input0q, input1q, output0, output1);
		}
		break;

	case FMT_RGB_DELTA:
		{
			vint4 input0q(input[0], input[2], input[4], 0);
			vint4 input1q(input[1], input[3], input[5], 0);
			rgb_delta_unpack(input0q, input1q, output0, output1);
		}
		break;

	//case FMT_HDR_RGB:
	//	rgb_hdr = true;
	//	alpha_hdr_default = true;
	//	hdr_rgb_unpack(input, output0, output1);
	//	break;

	case FMT_RGBA:
		{
			vint4 input0q(input[0], input[2], input[4], input[6]);
			vint4 input1q(input[1], input[3], input[5], input[7]);
			rgba_unpack(input0q, input1q, output0, output1);
		}
		break;

	case FMT_RGBA_DELTA:
		{
			vint4 input0q(input[0], input[2], input[4], input[6]);
			vint4 input1q(input[1], input[3], input[5], input[7]);
			rgba_delta_unpack(input0q, input1q, output0, output1);
		}
		break;

	//case FMT_HDR_RGB_LDR_ALPHA:
	//	rgb_hdr = true;
	//	hdr_rgb_ldr_alpha_unpack(input, output0, output1);
	//	break;

	//case FMT_HDR_RGBA:
	//	rgb_hdr = true;
	//	alpha_hdr = true;
	//	hdr_rgb_hdr_alpha_unpack(input, output0, output1);
	//	break;
	}

	// Assign a correct default alpha
	if (alpha_hdr_default)
	{
		//if (decode_mode == ASTCENC_PRF_HDR)
		//{
		//	output0.set_lane<3>(0x7800);
		//	output1.set_lane<3>(0x7800);
		//	alpha_hdr = true;
		//}
		//else
		{
			output0.set_lane<3>(0x00FF);
			output1.set_lane<3>(0x00FF);
			alpha_hdr = false;
		}
	}

	// Handle endpoint errors and expansion

	// Linear LDR 8-bit endpoints are expanded to 16-bit by replication
	if (decode_mode == ASTCENC_PRF_LDR)
	{
		// Error color - HDR endpoint in an LDR encoding
		if (rgb_hdr || alpha_hdr)
		{
			output0 = vint4(0xFF, 0x00, 0xFF, 0xFF);
			output1 = vint4(0xFF, 0x00, 0xFF, 0xFF);
			rgb_hdr = false;
			alpha_hdr = false;
		}

		output0 = output0 * 257;
		output1 = output1 * 257;
	}
	// sRGB LDR 8-bit endpoints are expanded to 16 bit by:
	//  - RGB = shift left by 8 bits and OR with 0x80
	//  - A = replication
	else if (decode_mode == ASTCENC_PRF_LDR_SRGB)
	{
		// Error color - HDR endpoint in an LDR encoding
		if (rgb_hdr || alpha_hdr)
		{
			output0 = vint4(0xFF, 0x00, 0xFF, 0xFF);
			output1 = vint4(0xFF, 0x00, 0xFF, 0xFF);
			rgb_hdr = false;
			alpha_hdr = false;
		}

		vmask4 mask(true, true, true, false);

		vint4 output0rgb = lsl<8>(output0) | vint4(0x80);
		vint4 output0a = output0 * 257;
		output0 = select(output0a, output0rgb, mask);

		vint4 output1rgb = lsl<8>(output1) | vint4(0x80);
		vint4 output1a = output1 * 257;
		output1 = select(output1a, output1rgb, mask);
	}
	// An HDR profile decode, but may be using linear LDR endpoints
	// Linear LDR 8-bit endpoints are expanded to 16-bit by replication
	// HDR endpoints are already 16-bit
	else
	{
		vmask4 hdr_lanes(rgb_hdr, rgb_hdr, rgb_hdr, alpha_hdr);
		vint4 output_scale = select(vint4(257), vint4(1), hdr_lanes);
		output0 = output0 * output_scale;
		output1 = output1 * output_scale;
	}
}
