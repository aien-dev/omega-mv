// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Drake Stapleton (AIEN / aien-dev)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! AT-0 reference oracle, compute crate. Standard library only; no clock, file,
//! process, thread, network, environment or random facility anywhere below
//! this module (isolation.sh enforces it). All I/O lives in src/main.rs.
#![allow(dead_code)] // several helpers exist for the test suite and calibration, not the binary

pub mod case;
pub mod complex;
pub mod exact;
pub mod fixtures;
pub mod matrix_path;
pub mod rational;
pub mod reference;
pub mod result;
pub mod sha256;

/// Mathematical contract AT0_SPEC.md the hand-table tests and the spec-* fixtures target:
/// aien-architecture pull request 176, merged as this squash commit. Section 13 of it
/// holds the hand-derived tables; re-verified against this commit on 2026-10-09.
pub const SPEC_COMMIT: &str = "68f47e26764a9f0b46d91194bf073824d04333bb";
