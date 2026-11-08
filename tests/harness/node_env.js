// Copyright 2026 Axon Works
// SPDX-License-Identifier: Apache-2.0
//
// Emscripten --pre-js for the tests in Node: the program's environment (getenv) mirrors the
// host's, so AX_TEST_IMAGE_DIR, AX_TEST_FAILURE_DIR, AX_UPDATE_GOLDENS and CI reach the tests.
// Emscripten 5 does this with NODE_HOST_ENV; 4.0.10, CI's, gives the program only its defaults.
if (typeof process === 'object' && process.env) {
  Module['preRun'] = [].concat(Module['preRun'] || [], function () {
    // ENV exists only in programs that read their environment.
    if (typeof ENV === 'undefined') {
      return;
    }
    for (var name in process.env) {
      ENV[name] = process.env[name];
    }
  });
}
