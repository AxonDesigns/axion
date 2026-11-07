# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# gpu: the WebGPU layer. Depends on base and, for window surfaces, on platform's internal SDK.
#
# Headers: everything is internal SDK for now (include/axion/gpu/); no stable ABI header exposes
# webgpu.h types until hosted textures arrive.

include(AxionWebGPU)
