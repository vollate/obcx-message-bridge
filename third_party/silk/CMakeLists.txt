# Vendored portable decoder only; no downloads or runtime helper executable.
add_library(bridge_silk OBJECT
  src/SKP_Silk_CNG.c
  src/SKP_Silk_LPC_inv_pred_gain.c
  src/SKP_Silk_LPC_synthesis_filter.c
  src/SKP_Silk_LPC_synthesis_order16.c
  src/SKP_Silk_LSF_cos_table.c
  src/SKP_Silk_MA.c
  src/SKP_Silk_NLSF2A.c
  src/SKP_Silk_NLSF2A_stable.c
  src/SKP_Silk_NLSF_MSVQ_decode.c
  src/SKP_Silk_NLSF_stabilize.c
  src/SKP_Silk_PLC.c
  src/SKP_Silk_biquad.c
  src/SKP_Silk_bwexpander.c
  src/SKP_Silk_bwexpander_32.c
  src/SKP_Silk_code_signs.c
  src/SKP_Silk_create_init_destroy.c
  src/SKP_Silk_dec_API.c
  src/SKP_Silk_decode_core.c
  src/SKP_Silk_decode_frame.c
  src/SKP_Silk_decode_parameters.c
  src/SKP_Silk_decode_pitch.c
  src/SKP_Silk_decode_pulses.c
  src/SKP_Silk_decoder_set_fs.c
  src/SKP_Silk_gain_quant.c
  src/SKP_Silk_lin2log.c
  src/SKP_Silk_log2lin.c
  src/SKP_Silk_pitch_est_tables.c
  src/SKP_Silk_range_coder.c
  src/SKP_Silk_resampler.c
  src/SKP_Silk_resampler_down2.c
  src/SKP_Silk_resampler_private_AR2.c
  src/SKP_Silk_resampler_private_ARMA4.c
  src/SKP_Silk_resampler_private_IIR_FIR.c
  src/SKP_Silk_resampler_private_copy.c
  src/SKP_Silk_resampler_private_down4.c
  src/SKP_Silk_resampler_private_down_FIR.c
  src/SKP_Silk_resampler_private_up2_HQ.c
  src/SKP_Silk_resampler_private_up4.c
  src/SKP_Silk_resampler_rom.c
  src/SKP_Silk_resampler_up2.c
  src/SKP_Silk_shell_coder.c
  src/SKP_Silk_sort.c
  src/SKP_Silk_sum_sqr_shift.c
  src/SKP_Silk_tables_LTP.c
  src/SKP_Silk_tables_NLSF_CB0_10.c
  src/SKP_Silk_tables_NLSF_CB0_16.c
  src/SKP_Silk_tables_NLSF_CB1_10.c
  src/SKP_Silk_tables_NLSF_CB1_16.c
  src/SKP_Silk_tables_gain.c
  src/SKP_Silk_tables_other.c
  src/SKP_Silk_tables_pitch_lag.c
  src/SKP_Silk_tables_pulses_per_block.c
  src/SKP_Silk_tables_sign.c
  src/SKP_Silk_tables_type_offset.c
)
set_target_properties(bridge_silk PROPERTIES
  POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
target_include_directories(bridge_silk PRIVATE interface src)
# Fixed-point SDK arithmetic assumes wrapping and aliasing of integer storage.
target_compile_options(bridge_silk PRIVATE -fwrapv -fno-strict-aliasing)
target_compile_definitions(bridge_silk PRIVATE NDEBUG)

obcx_package_target(bridge_silk ROLE implementation)

# The actor SDK is a C++-only workspace. These portable C sources
# also compile as C++; public SDK declarations retain their extern "C" ABI.
get_target_property(_silk_sources bridge_silk SOURCES)
set_source_files_properties(${_silk_sources} PROPERTIES LANGUAGE CXX)
