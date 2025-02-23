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

/**
 * @brief Functions to generate block size descriptor and decimation tables.
 */

#include "astcenc_internal.h"

/**
 * @brief Decode the properties of an encoded 2D block mode.
 *
 * @param      block_mode      The encoded block mode.
 * @param[out] x_weights       The number of weights in the X dimension.
 * @param[out] y_weights       The number of weights in the Y dimension.
 * @param[out] is_dual_plane   True if this block mode has two weight planes.
 * @param[out] quant_mode      The quantization level for the weights.
 * @param[out] weight_bits     The storage bit count for the weights.
 *
 * @return Returns true if a valid mode, false otherwise.
 */
static bool decode_block_mode_2d(
	unsigned int block_mode,
	unsigned int& x_weights,
	unsigned int& y_weights,
	bool& is_dual_plane,
	unsigned int& quant_mode,
	unsigned int& weight_bits
) {
	unsigned int base_quant_mode = (block_mode >> 4) & 1;
	unsigned int H = (block_mode >> 9) & 1;
	unsigned int D = (block_mode >> 10) & 1;
	unsigned int A = (block_mode >> 5) & 0x3;

	x_weights = 0;
	y_weights = 0;

	if ((block_mode & 3) != 0)
	{
		base_quant_mode |= (block_mode & 3) << 1;
		unsigned int B = (block_mode >> 7) & 3;
		switch ((block_mode >> 2) & 3)
		{
		case 0:
			x_weights = B + 4;
			y_weights = A + 2;
			break;
		case 1:
			x_weights = B + 8;
			y_weights = A + 2;
			break;
		case 2:
			x_weights = A + 2;
			y_weights = B + 8;
			break;
		case 3:
			B &= 1;
			if (block_mode & 0x100)
			{
				x_weights = B + 2;
				y_weights = A + 2;
			}
			else
			{
				x_weights = A + 2;
				y_weights = B + 6;
			}
			break;
		}
	}
	else
	{
		base_quant_mode |= ((block_mode >> 2) & 3) << 1;
		if (((block_mode >> 2) & 3) == 0)
		{
			return false;
		}

		unsigned int B = (block_mode >> 9) & 3;
		switch ((block_mode >> 7) & 3)
		{
		case 0:
			x_weights = 12;
			y_weights = A + 2;
			break;
		case 1:
			x_weights = A + 2;
			y_weights = 12;
			break;
		case 2:
			x_weights = A + 6;
			y_weights = B + 6;
			D = 0;
			H = 0;
			break;
		case 3:
			switch ((block_mode >> 5) & 3)
			{
			case 0:
				x_weights = 6;
				y_weights = 10;
				break;
			case 1:
				x_weights = 10;
				y_weights = 6;
				break;
			case 2:
			case 3:
				return false;
			}
			break;
		}
	}

	unsigned int weight_count = x_weights * y_weights * (D + 1);
	quant_mode = (base_quant_mode - 2) + 6 * H;
	is_dual_plane = D != 0;

	weight_bits = get_ise_sequence_bitcount(weight_count, static_cast<quant_method>(quant_mode));
	return (weight_count <= BLOCK_MAX_WEIGHTS &&
	        weight_bits >= BLOCK_MIN_WEIGHT_BITS &&
	        weight_bits <= BLOCK_MAX_WEIGHT_BITS);
}



/**
 * @brief Create a 2D decimation entry for a block-size and weight-decimation pair.
 *
 * @param      x_texels    The number of texels in the X dimension.
 * @param      y_texels    The number of texels in the Y dimension.
 * @param      x_weights   The number of weights in the X dimension.
 * @param      y_weights   The number of weights in the Y dimension.
 * @param[out] di          The decimation info structure to populate.
 * @param[out] wb          The decimation table init scratch working buffers.
 */
static void init_decimation_info_2d(
	unsigned int x_texels,
	unsigned int y_texels,
	unsigned int x_weights,
	unsigned int y_weights,
	decimation_info& di,
	dt_init_working_buffers& wb
) {
	unsigned int texels_per_block = x_texels * y_texels;
	unsigned int weights_per_block = x_weights * y_weights;

	uint8_t max_texel_count_of_weight = 0;

	promise(weights_per_block > 0);
	promise(texels_per_block > 0);
	promise(x_texels > 0);
	promise(y_texels > 0);

	for (unsigned int i = 0; i < weights_per_block; i++)
	{
		wb.texel_count_of_weight[i] = 0;
	}

	for (unsigned int i = 0; i < texels_per_block; i++)
	{
		wb.weight_count_of_texel[i] = 0;
	}

	for (unsigned int y = 0; y < y_texels; y++)
	{
		for (unsigned int x = 0; x < x_texels; x++)
		{
			unsigned int texel = y * x_texels + x;

			unsigned int x_weight = (((1024 + x_texels / 2) / (x_texels - 1)) * x * (x_weights - 1) + 32) >> 6;
			unsigned int y_weight = (((1024 + y_texels / 2) / (y_texels - 1)) * y * (y_weights - 1) + 32) >> 6;

			unsigned int x_weight_frac = x_weight & 0xF;
			unsigned int y_weight_frac = y_weight & 0xF;
			unsigned int x_weight_int = x_weight >> 4;
			unsigned int y_weight_int = y_weight >> 4;

			unsigned int qweight[4];
			qweight[0] = x_weight_int + y_weight_int * x_weights;
			qweight[1] = qweight[0] + 1;
			qweight[2] = qweight[0] + x_weights;
			qweight[3] = qweight[2] + 1;

			// Truncated-precision bilinear interpolation
			unsigned int prod = x_weight_frac * y_weight_frac;

			unsigned int weight[4];
			weight[3] = (prod + 8) >> 4;
			weight[1] = x_weight_frac - weight[3];
			weight[2] = y_weight_frac - weight[3];
			weight[0] = 16 - x_weight_frac - y_weight_frac + weight[3];

			for (unsigned int i = 0; i < 4; i++)
			{
				if (weight[i] != 0)
				{
					wb.grid_weights_of_texel[texel][wb.weight_count_of_texel[texel]] = static_cast<uint8_t>(qweight[i]);
					wb.weights_of_texel[texel][wb.weight_count_of_texel[texel]] = static_cast<uint8_t>(weight[i]);
					wb.weight_count_of_texel[texel]++;
					wb.texels_of_weight[qweight[i]][wb.texel_count_of_weight[qweight[i]]] = static_cast<uint8_t>(texel);
					wb.texel_weights_of_weight[qweight[i]][wb.texel_count_of_weight[qweight[i]]] = static_cast<uint8_t>(weight[i]);
					wb.texel_count_of_weight[qweight[i]]++;
					max_texel_count_of_weight = astc::max(max_texel_count_of_weight, wb.texel_count_of_weight[qweight[i]]);
				}
			}
		}
	}

	uint8_t max_texel_weight_count = 0;
	for (unsigned int i = 0; i < texels_per_block; i++)
	{
		di.texel_weight_count[i] = wb.weight_count_of_texel[i];
		max_texel_weight_count = astc::max(max_texel_weight_count, di.texel_weight_count[i]);

		for (unsigned int j = 0; j < wb.weight_count_of_texel[i]; j++)
		{
			di.texel_weight_contribs_int_tr[j][i] = wb.weights_of_texel[i][j];
			di.texel_weight_contribs_float_tr[j][i] = static_cast<float>(wb.weights_of_texel[i][j]) * (1.0f / WEIGHTS_TEXEL_SUM);
			di.texel_weights_tr[j][i] = wb.grid_weights_of_texel[i][j];
		}

		// Init all 4 entries so we can rely on zeros for vectorization
		for (unsigned int j = wb.weight_count_of_texel[i]; j < 4; j++)
		{
			di.texel_weight_contribs_int_tr[j][i] = 0;
			di.texel_weight_contribs_float_tr[j][i] = 0.0f;
			di.texel_weights_tr[j][i] = 0;
		}
	}

	di.max_texel_weight_count = max_texel_weight_count;

	for (unsigned int i = 0; i < weights_per_block; i++)
	{
		unsigned int texel_count_wt = wb.texel_count_of_weight[i];
		di.weight_texel_count[i] = static_cast<uint8_t>(texel_count_wt);

		for (unsigned int j = 0; j < texel_count_wt; j++)
		{
			uint8_t texel = wb.texels_of_weight[i][j];

			// Create transposed versions of these for better vectorization
			di.weight_texels_tr[j][i] = texel;
			di.weights_texel_contribs_tr[j][i] = static_cast<float>(wb.texel_weights_of_weight[i][j]);

			// Store the per-texel contribution of this weight for each texel it contributes to
			di.texel_contrib_for_weight[j][i] = 0.0f;
			for (unsigned int k = 0; k < 4; k++)
			{
				uint8_t dttw = di.texel_weights_tr[k][texel];
				float dttwf = di.texel_weight_contribs_float_tr[k][texel];
				if (dttw == i && dttwf != 0.0f)
				{
					di.texel_contrib_for_weight[j][i] = di.texel_weight_contribs_float_tr[k][texel];
					break;
				}
			}
		}

		// Initialize array tail so we can over-fetch with SIMD later to avoid loop tails
		// Match last texel in active lane in SIMD group, for better gathers
		uint8_t last_texel = di.weight_texels_tr[texel_count_wt - 1][i];
		for (unsigned int j = texel_count_wt; j < max_texel_count_of_weight; j++)
		{
			di.weight_texels_tr[j][i] = last_texel;
			di.weights_texel_contribs_tr[j][i] = 0.0f;
		}
	}

	// Initialize array tail so we can over-fetch with SIMD later to avoid loop tails
	unsigned int texels_per_block_simd = _RESEARCH_round_up_to_simd_multiple_vla(texels_per_block);
	for (unsigned int i = texels_per_block; i < texels_per_block_simd; i++)
	{
		di.texel_weight_count[i] = 0;

		for (unsigned int j = 0; j < 4; j++)
		{
			di.texel_weight_contribs_float_tr[j][i] = 0;
			di.texel_weights_tr[j][i] = 0;
			di.texel_weight_contribs_int_tr[j][i] = 0;
		}
	}

	// Initialize array tail so we can over-fetch with SIMD later to avoid loop tails
	// Match last texel in active lane in SIMD group, for better gathers
	unsigned int last_texel_count_wt = wb.texel_count_of_weight[weights_per_block - 1];
	uint8_t last_texel = di.weight_texels_tr[last_texel_count_wt - 1][weights_per_block - 1];

	unsigned int weights_per_block_simd = _RESEARCH_round_up_to_simd_multiple_vla(weights_per_block);
	for (unsigned int i = weights_per_block; i < weights_per_block_simd; i++)
	{
		di.weight_texel_count[i] = 0;

		for (unsigned int j = 0; j < max_texel_count_of_weight; j++)
		{
			di.weight_texels_tr[j][i] = last_texel;
			di.weights_texel_contribs_tr[j][i] = 0.0f;
		}
	}

	di.texel_count = static_cast<uint8_t>(texels_per_block);
	di.weight_count = static_cast<uint8_t>(weights_per_block);
	di.weight_x = static_cast<uint8_t>(x_weights);
	di.weight_y = static_cast<uint8_t>(y_weights);
	di.weight_z = 1;
}


/**
 * @brief Assign the texels to use for kmeans clustering.
 *
 * The max limit is @c BLOCK_MAX_KMEANS_TEXELS; above this a random selection is used.
 * The @c bsd.texel_count is an input and must be populated beforehand.
 *
 * @param[in,out] bsd   The block size descriptor to populate.
 */
static void assign_kmeans_texels(
	block_size_descriptor& bsd
) {
	// Use all texels for kmeans on a small block
	if (bsd.texel_count <= BLOCK_MAX_KMEANS_TEXELS)
	{
		for (uint8_t i = 0; i < bsd.texel_count; i++)
		{
			bsd.kmeans_texels[i] = i;
		}

		return;
	}

	// Select a random subset of BLOCK_MAX_KMEANS_TEXELS for kmeans on a large block
	uint64_t rng_state[2];
	astc::rand_init(rng_state);

	// Initialize array used for tracking used indices
	bool seen[BLOCK_MAX_TEXELS];
	for (uint8_t i = 0; i < bsd.texel_count; i++)
	{
		seen[i] = false;
	}

	// Assign 64 random indices, retrying if we see repeats
	unsigned int arr_elements_set = 0;
	while (arr_elements_set < BLOCK_MAX_KMEANS_TEXELS)
	{
		uint8_t texel = static_cast<uint8_t>(astc::rand(rng_state));
		texel = texel % bsd.texel_count;
		if (!seen[texel])
		{
			bsd.kmeans_texels[arr_elements_set++] = texel;
			seen[texel] = true;
		}
	}
}

/**
 * @brief Allocate a single 2D decimation table entry.
 *
 * @param x_texels    The number of texels in the X dimension.
 * @param y_texels    The number of texels in the Y dimension.
 * @param x_weights   The number of weights in the X dimension.
 * @param y_weights   The number of weights in the Y dimension.
 * @param bsd         The block size descriptor we are populating.
 * @param wb          The decimation table init scratch working buffers.
 * @param index       The packed array index to populate.
 */
static void construct_dt_entry_2d(
	unsigned int x_texels,
	unsigned int y_texels,
	unsigned int x_weights,
	unsigned int y_weights,
	block_size_descriptor& bsd,
	dt_init_working_buffers& wb,
	unsigned int index
) {
	unsigned int weight_count = x_weights * y_weights;
	assert(weight_count <= BLOCK_MAX_WEIGHTS);

	bool try_2planes = (2 * weight_count) <= BLOCK_MAX_WEIGHTS;

	decimation_info& di = bsd.decimation_tables[index];
	init_decimation_info_2d(x_texels, y_texels, x_weights, y_weights, di, wb);

	int maxprec_1plane = -1;
	int maxprec_2planes = -1;
	for (int i = 0; i < 12; i++)
	{
		unsigned int bits_1plane = get_ise_sequence_bitcount(weight_count, static_cast<quant_method>(i));
		if (bits_1plane >= BLOCK_MIN_WEIGHT_BITS && bits_1plane <= BLOCK_MAX_WEIGHT_BITS)
		{
			maxprec_1plane = i;
		}

		if (try_2planes)
		{
			unsigned int bits_2planes = get_ise_sequence_bitcount(2 * weight_count, static_cast<quant_method>(i));
			if (bits_2planes >= BLOCK_MIN_WEIGHT_BITS && bits_2planes <= BLOCK_MAX_WEIGHT_BITS)
			{
				maxprec_2planes = i;
			}
		}
	}

	// At least one of the two should be valid ...
	assert(maxprec_1plane >= 0 || maxprec_2planes >= 0);
	bsd.decimation_modes[index].maxprec_1plane = static_cast<int8_t>(maxprec_1plane);
	bsd.decimation_modes[index].maxprec_2planes = static_cast<int8_t>(maxprec_2planes);
	bsd.decimation_modes[index].refprec_1plane = 0;
	bsd.decimation_modes[index].refprec_2planes = 0;
}

/**
 * @brief Allocate block modes and decimation tables for a single 2D block size.
 *
 * @param      x_texels         The number of texels in the X dimension.
 * @param      y_texels         The number of texels in the Y dimension.
 * @param      can_omit_modes   Can we discard modes that astcenc won't use, even if legal?
 * @param      mode_cutoff      Percentile cutoff in range [0,1]. Low values more likely to be used.
 * @param[out] bsd              The block size descriptor to populate.
 */
static void _RESEARCH_construct_block_size_descriptor_2d(
	unsigned int x_texels,
	unsigned int y_texels,
	bool can_omit_modes,
	float mode_cutoff,
	block_size_descriptor& bsd
) {
	// Store a remap table for storing packed decimation modes.
	// Indexing uses [Y * 16 + X] and max size for each axis is 12.
	static const unsigned int MAX_DMI = 12 * 16 + 12;
	int decimation_mode_index[MAX_DMI];

	dt_init_working_buffers* wb = new dt_init_working_buffers;

	bsd.xdim = static_cast<uint8_t>(x_texels);
	bsd.ydim = static_cast<uint8_t>(y_texels);
	bsd.zdim = 1;
	bsd.texel_count = static_cast<uint8_t>(x_texels * y_texels);

	for (unsigned int i = 0; i < MAX_DMI; i++)
	{
		decimation_mode_index[i] = -1;
	}

	// Gather all the decimation grids that can be used with the current block
#if !defined(ASTCENC_DECOMPRESS_ONLY)
	const float *percentiles = get_2d_percentile_table(x_texels, y_texels);
	float always_cutoff = 0.0f;
#else
	// Unused in decompress-only builds
	(void)can_omit_modes;
	(void)mode_cutoff;
#endif

	// Construct the list of block formats referencing the decimation tables
	unsigned int packed_bm_idx = 0;
	unsigned int packed_dm_idx = 0;

	// Trackers
	unsigned int bm_counts[4] { 0 };
	unsigned int dm_counts[4] { 0 };

	// Clear the list to a known-bad value
	for (unsigned int i = 0; i < WEIGHTS_MAX_BLOCK_MODES; i++)
	{
		bsd.block_mode_packed_index[i] = BLOCK_BAD_BLOCK_MODE;
	}

	// Iterate four times to build a usefully ordered list:
	//   - Pass 0 - keep selected single plane "always" block modes
	//   - Pass 1 - keep selected single plane "non-always" block modes
	//   - Pass 2 - keep select dual plane block modes
	//   - Pass 3 - keep everything else that's legal
	unsigned int limit = can_omit_modes ? 3 : 4;
	for (unsigned int j = 0; j < limit; j ++)
	{
		for (unsigned int i = 0; i < WEIGHTS_MAX_BLOCK_MODES; i++)
		{
			// Skip modes we've already included in a previous pass
			if (bsd.block_mode_packed_index[i] != BLOCK_BAD_BLOCK_MODE)
			{
				continue;
			}

			// Decode parameters
			unsigned int x_weights;
			unsigned int y_weights;
			bool is_dual_plane;
			unsigned int quant_mode;
			unsigned int weight_bits;
			bool valid = decode_block_mode_2d(i, x_weights, y_weights, is_dual_plane, quant_mode, weight_bits);

			// Always skip invalid encodings for the current block size
			if (!valid || (x_weights > x_texels) || (y_weights > y_texels))
			{
				continue;
			}

			// Selectively skip dual plane encodings
			if (((j <= 1) && is_dual_plane) || (j == 2 && !is_dual_plane))
			{
				continue;
			}

			// Always skip encodings we can't physically encode based on
			// generic encoding bit availability
			if (is_dual_plane)
			{
				 // This is the only check we need as only support 1 partition
				 if ((109 - weight_bits) <= 0)
				 {
					continue;
				 }
			}
			else
			{
				// This is conservative - fewer bits may be available for > 1 partition
				 if ((111 - weight_bits) <= 0)
				 {
					continue;
				 }
			}

			// Selectively skip encodings based on percentile
			bool percentile_hit = false;
	#if !defined(ASTCENC_DECOMPRESS_ONLY)
			if (j == 0)
			{
				percentile_hit = percentiles[i] <= always_cutoff;
			}
			else
			{
				percentile_hit = percentiles[i] <= mode_cutoff;
			}
	#endif

			if (j != 3 && !percentile_hit)
			{
				continue;
			}

			// Allocate and initialize the decimation table entry if we've not used it yet
			int decimation_mode = decimation_mode_index[y_weights * 16 + x_weights];
			if (decimation_mode < 0)
			{
				construct_dt_entry_2d(x_texels, y_texels, x_weights, y_weights, bsd, *wb, packed_dm_idx);
				decimation_mode_index[y_weights * 16 + x_weights] = packed_dm_idx;
				decimation_mode = packed_dm_idx;

				dm_counts[j]++;
				packed_dm_idx++;
			}

			auto& bm = bsd.block_modes[packed_bm_idx];

			bm.decimation_mode = static_cast<uint8_t>(decimation_mode);
			bm.quant_mode = static_cast<uint8_t>(quant_mode);
			bm.is_dual_plane = static_cast<uint8_t>(is_dual_plane);
			bm.weight_bits = static_cast<uint8_t>(weight_bits);
			bm.mode_index = static_cast<uint16_t>(i);

			auto& dm = bsd.decimation_modes[decimation_mode];

			if (is_dual_plane)
			{
				dm.set_ref_2plane(bm.get_weight_quant_mode());
			}
			else
			{
				dm.set_ref_1plane(bm.get_weight_quant_mode());
			}

			bsd.block_mode_packed_index[i] = static_cast<uint16_t>(packed_bm_idx);

			packed_bm_idx++;
			bm_counts[j]++;
		}
	}

	bsd.block_mode_count_1plane_always = bm_counts[0];
	bsd.block_mode_count_1plane_selected = bm_counts[0] + bm_counts[1];
	bsd.block_mode_count_1plane_2plane_selected = bm_counts[0] + bm_counts[1] + bm_counts[2];
	bsd.block_mode_count_all = bm_counts[0] + bm_counts[1] + bm_counts[2] + bm_counts[3];

	bsd.decimation_mode_count_always = dm_counts[0];
	bsd.decimation_mode_count_selected = dm_counts[0] + dm_counts[1] + dm_counts[2];
	bsd.decimation_mode_count_all = dm_counts[0] + dm_counts[1] + dm_counts[2] + dm_counts[3];

#if !defined(ASTCENC_DECOMPRESS_ONLY)
	assert(bsd.block_mode_count_1plane_always > 0);
	assert(bsd.decimation_mode_count_always > 0);

	delete[] percentiles;
#endif

	// Ensure the end of the array contains valid data (should never get read)
	for (unsigned int i = bsd.decimation_mode_count_all; i < WEIGHTS_MAX_DECIMATION_MODES; i++)
	{
		bsd.decimation_modes[i].maxprec_1plane = -1;
		bsd.decimation_modes[i].maxprec_2planes = -1;
		bsd.decimation_modes[i].refprec_1plane = 0;
		bsd.decimation_modes[i].refprec_2planes = 0;
	}

	// Determine the texels to use for kmeans clustering.
	assign_kmeans_texels(bsd);

	delete wb;
}



/* See header for documentation. */
void _RESEARCH_init_block_size_descriptor(
	unsigned int x_texels,
	unsigned int y_texels,
	unsigned int z_texels,
	bool can_omit_modes,
	unsigned int partition_count_cutoff,
	float mode_cutoff,
	block_size_descriptor& bsd
) {
	//if (z_texels > 1)
	//{
	//	construct_block_size_descriptor_3d(x_texels, y_texels, z_texels, bsd);
	//}
	//else
	{
		_RESEARCH_construct_block_size_descriptor_2d(x_texels, y_texels, can_omit_modes, mode_cutoff, bsd);
	}

	_RESEARCH_init_partition_tables(bsd, can_omit_modes, partition_count_cutoff);
}
