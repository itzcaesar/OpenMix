DeepFilterNet libDF v0.5.6 from https://github.com/Rikorose/DeepFilterNet/tree/v0.5.6 (MIT or Apache-2.0).

OpenMix builds the libDF C-API as df.dll with the default embedded checkpoint disabled. Local capi.rs changes add stereo frame support, reset/latency queries, and panic containment at the FFI boundary. The ONNX model archive is kept separate and downloaded by the user; it is not redistributed by OpenMix.
