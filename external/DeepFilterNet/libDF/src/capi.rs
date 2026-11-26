use std::boxed::Box;
use std::ffi::{c_char, c_float, c_uint, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;

use ndarray::prelude::*;

use crate::tract::*;

pub struct DFState(crate::tract::DfTract);

impl DFState {
    fn new(model_path: &str, channels: usize, atten_lim: f32) -> Self {
        let r_params = RuntimeParams::default_with_ch(channels).with_atten_lim(atten_lim);
        let df_params =
            DfParams::new(PathBuf::from(model_path)).expect("Could not load model from path");
        let m =
            DfTract::new(df_params, &r_params).expect("Could not initialize DeepFilter runtime.");
        DFState(m)
    }
    fn boxed(self) -> Box<DFState> {
        Box::new(self)
    }
}

/// Create a DeepFilterNet Model
///
/// Args:
///     - path: File path to a DeepFilterNet tar.gz onnx model
///     - atten_lim: Attenuation limit in dB.
///
/// Returns:
///     - DF state doing the full processing: stft, DNN noise reduction, istft.
#[no_mangle]
pub unsafe extern "C" fn df_create(path: *const c_char, atten_lim: f32) -> *mut DFState {
    df_create_channels(path, 1, atten_lim)
}

/// Create a DeepFilterNet model with one or two planar audio channels.
#[no_mangle]
pub unsafe extern "C" fn df_create_channels(
    path: *const c_char,
    channels: c_uint,
    atten_lim: f32,
) -> *mut DFState {
    if path.is_null() || !(channels == 1 || channels == 2) {
        return std::ptr::null_mut();
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let c_str = CStr::from_ptr(path);
        let path = c_str.to_str().unwrap();
        let df = DFState::new(path, channels as usize, atten_lim);
        Box::into_raw(df.boxed())
    }));
    result.unwrap_or(std::ptr::null_mut())
}

/// Get DeepFilterNet frame size in samples.
#[no_mangle]
pub unsafe extern "C" fn df_get_frame_length(st: *mut DFState) -> usize {
    let Some(state) = st.as_mut() else {
        return 0;
    };
    state.0.hop_size
}

/// Get the model's signal latency in samples at its native sample rate.
#[no_mangle]
pub unsafe extern "C" fn df_get_latency_samples(st: *mut DFState) -> usize {
    let Some(state) = st.as_ref() else {
        return 0;
    };
    state.0.fft_size - state.0.hop_size + state.0.lookahead * state.0.hop_size
}

/// Reset streaming feature and synthesis state.
#[no_mangle]
pub unsafe extern "C" fn df_reset(st: *mut DFState) -> bool {
    let Some(state) = st.as_mut() else {
        return false;
    };
    catch_unwind(AssertUnwindSafe(|| {
        let nb_df = state.0.nb_df;
        for channel in &mut state.0.df_states {
            channel.reset();
            channel.init_norm_states(nb_df);
        }
        state.0.init().is_ok()
    }))
    .unwrap_or(false)
}

/// Set DeepFilterNet attenuation limit.
///
/// Args:
///     - lim_db: New attenuation limit in dB.
#[no_mangle]
pub unsafe extern "C" fn df_set_atten_lim(st: *mut DFState, lim_db: f32) {
    let state = st.as_mut().expect("Invalid pointer");
    state.0.set_atten_lim(lim_db)
}

/// Set DeepFilterNet post filter beta. A beta of 0 disables the post filter.
///
/// Args:
///     - beta: Post filter attenuation. Suitable range between 0.05 and 0;
#[no_mangle]
pub unsafe extern "C" fn df_set_post_filter_beta(st: *mut DFState, beta: f32) {
    let state = st.as_mut().expect("Invalid pointer");
    state.0.set_pf_beta(beta)
}

/// Processes a chunk of samples.
///
/// Args:
///     - df_state: Created via df_create()
///     - input: Input buffer of length df_get_frame_length()
///     - output: Output buffer of length df_get_frame_length()
///
/// Returns:
///     - Local SNR of the current frame.
#[no_mangle]
pub unsafe extern "C" fn df_process_frame(
    st: *mut DFState,
    input: *mut c_float,
    output: *mut c_float,
) -> c_float {
    let Some(state) = st.as_mut() else {
        return f32::NAN;
    };
    if input.is_null() || output.is_null() {
        return f32::NAN;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let channels = state.0.ch;
        let frame_size = state.0.hop_size;
        let input = ArrayView2::from_shape_ptr((channels, frame_size), input);
        let output = ArrayViewMut2::from_shape_ptr((channels, frame_size), output);
        state.0.process(input, output).unwrap_or(f32::NAN)
    }))
    .unwrap_or(f32::NAN)
}

/// Processes a filter bank sample and return raw gains and DF coefs.
///
/// Args:
///     - df_state: Created via df_create()
///     - input: Spectrum of shape `[n_freqs, 2]`.
///     - out_gains_p: Output buffer of real-valued ERB gains of shape `[nb_erb]`. This function
///         may set this pointer to NULL if the local SNR is greater 30 dB. No gains need to be
///         applied then.
///     - out_coefs_p: Output buffer of complex-valued DF coefs of shape `[df_order, nb_df_freqs, 2]`.
///         This function may set this pointer to NULL if the local SNR is greater 20 dB. No DF
///         coefficients need to be applied.
///
/// Returns:
///     - Local SNR of the current frame.
#[no_mangle]
pub unsafe extern "C" fn df_process_frame_raw(
    st: *mut DFState,
    input: *mut c_float,
    out_gains_p: *mut *mut c_float,
    out_coefs_p: *mut *mut c_float,
) -> c_float {
    let state = st.as_mut().expect("Invalid pointer");
    let input = ArrayView2::from_shape_ptr((1, state.0.n_freqs), input);
    state
        .0
        .set_spec_buffer(input)
        .expect("Failed to set input spectrum");
    let (lsnr, gains, coefs) = state
        .0
        .process_raw()
        .expect("Failed to process DF spectral frame");
    let mut out_gains = ArrayViewMut2::from_shape_ptr((1, state.0.nb_erb), *out_gains_p);
    let mut out_coefs =
        ArrayViewMut4::from_shape_ptr((1, state.0.df_order, state.0.nb_df, 2), *out_coefs_p);
    if let Some(gains) = gains {
        out_gains.assign(&gains.to_array_view().unwrap());
    } else {
        *out_gains_p = std::ptr::null_mut();
    }
    if let Some(coefs) = coefs {
        out_coefs.assign(&coefs.to_array_view().unwrap());
    } else {
        *out_coefs_p = std::ptr::null_mut();
    }
    lsnr
}

// file.rs
#[repr(C)]
pub struct DynArray {
    array: *mut c_uint,
    length: c_uint,
}

/// Get size of DeepFilter coefficients
pub unsafe extern "C" fn df_coef_size(st: *const DFState) -> DynArray {
    let state = st.as_ref().expect("Invalid pointer");
    let mut shape = vec![
        state.0.ch as u32,
        state.0.df_order as u32,
        state.0.n_freqs as u32,
        2,
    ];
    let ret = DynArray {
        array: shape.as_mut_ptr(),
        length: shape.len() as u32,
    };
    std::mem::forget(shape);
    ret
}
/// Get size ERB gains
pub unsafe extern "C" fn df_gain_size(st: *const DFState) -> DynArray {
    let state = st.as_ref().expect("Invalid pointer");
    let mut shape = vec![state.0.ch as u32, state.0.nb_erb as u32];
    let ret = DynArray {
        array: shape.as_mut_ptr(),
        length: shape.len() as u32,
    };
    std::mem::forget(shape);
    ret
}

/// Free a DeepFilterNet Model
#[no_mangle]
pub unsafe extern "C" fn df_free(model: *mut DFState) {
    let _ = Box::from_raw(model);
}
