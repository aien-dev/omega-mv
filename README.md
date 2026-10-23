# Recovery archive, 2026-10-23

This branch is a preservation index, not a product implementation or merge proposal.
Original committed work and unreviewed working/staged/conflict snapshots are reachable
through this commit's parent history. The index tree deliberately contains only recovery
records. No active branch, original staging area, service or working file was replaced.

## Contents

- 67 local branch references absent from the published heads/PR histories inspected.
- 108 working-folder snapshots, 0 separate staged snapshots, 0 unresolved conflict alternatives.
- 4 excluded untracked generated/model/nested-repository entries; see working-snapshots.json.
- 308 previously unreferenced commits, including the new recovery snapshots.
- Published main at capture: a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a.

The capture covered discovered workspace clones and their registered worktrees for
Sovereign Core, Omega and CUDApp, plus their existing omega-bridge migration repository.
Ignored files, arbitrary reflog-only objects and unrelated repositories were not swept.
Six missing registered worktree directories were recorded in the cross-repository summary.
Known compiler/device owners retain implementation ownership. No test, quality, hardware,
license-transition or release approval is implied by publication. Historical Rust/C/.osc
is retained here as recovery history, not adopted as the final first-party implementation.

## Recover saved work

Use the commit links below, or fetch this branch and create a new local branch at the
desired commit. Do not reset an active working folder. Working snapshots capture eligible
on-disk files; staged and conflict snapshots preserve additional index alternatives.
Generated/model artifacts and nested repositories are excluded explicitly.

### Previously unpublished branch tips

| Original local branch | Saved commit |
|---|---|
| backup/e1-gap-close-a8714df | [a8714df3c37e](https://github.com/aien-dev/omega-mv/commit/a8714df3c37e608a5186bb7b82812e40fedd49d9) |
| c3-fix-acq | [44e12ec1fda6](https://github.com/aien-dev/omega-mv/commit/44e12ec1fda6b08ce453610ce42963b6bef1a057) |
| c343-local | [53275b557741](https://github.com/aien-dev/omega-mv/commit/53275b557741ae46369e80e57593137b68a60c98) |
| c3rdx-v1 | [a8714df3c37e](https://github.com/aien-dev/omega-mv/commit/a8714df3c37e608a5186bb7b82812e40fedd49d9) |
| cand3/r15-energy-residency | [ef9076d60a0e](https://github.com/aien-dev/omega-mv/commit/ef9076d60a0e207faf7ee8d82088260fc44cad86) |
| e1-gap-close | [b48d1322b9a1](https://github.com/aien-dev/omega-mv/commit/b48d1322b9a1cbc75768adfd30337b79b45bd460) |
| e1-gap-close-2 | [5765db211b17](https://github.com/aien-dev/omega-mv/commit/5765db211b17009223032d6b03ea4eb52e512e49) |
| fastload/fast-nan-scan | [c9c326bfa863](https://github.com/aien-dev/omega-mv/commit/c9c326bfa8631977edaf2c69c5e3c419525672ba) |
| feat/bf16-warps-any-k | [c759af8a58e5](https://github.com/aien-dev/omega-mv/commit/c759af8a58e534ab8b0fadcd80c7c21313b1121b) |
| feat/forge-v1-numeric | [25a0c5f1d2d6](https://github.com/aien-dev/omega-mv/commit/25a0c5f1d2d677adbc6c2ba229ee2776d360dfc7) |
| feat/path-trajectory | [31b2fcdebb70](https://github.com/aien-dev/omega-mv/commit/31b2fcdebb70c4f331c6df458dee504f4ae43d48) |
| fix/astra-world-command-visibility | [1a853f98fbe9](https://github.com/aien-dev/omega-mv/commit/1a853f98fbe9bd267cd134e8d1c2a9278f88f406) |
| fix/mufu-ftz-462 | [cfadf764a394](https://github.com/aien-dev/omega-mv/commit/cfadf764a394c2741015e4eb888ab2fc8525b3d9) |
| fix/omega-gpu-engine-first-cut | [21392eea5395](https://github.com/aien-dev/omega-mv/commit/21392eea539571ae666924a8513ea1d0c35caf57) |
| fix/visor-realization-program | [3ad83b482f7c](https://github.com/aien-dev/omega-mv/commit/3ad83b482f7c15870f73928313698454d5ee83ba) |
| hive/CHIPWAIT-1 | [6fa4d971ca4d](https://github.com/aien-dev/omega-mv/commit/6fa4d971ca4d6317a13ddd2211bbe7277b8f6966) |
| hive/CHIPWAIT-1-mutants | [834b53578a71](https://github.com/aien-dev/omega-mv/commit/834b53578a71f60ffa571e3e6280576ea667d1bb) |
| hive/CHIPWAIT-1c | [1a853f98fbe9](https://github.com/aien-dev/omega-mv/commit/1a853f98fbe9bd267cd134e8d1c2a9278f88f406) |
| hive/CHIPWAIT-2-omega | [c80111b5018a](https://github.com/aien-dev/omega-mv/commit/c80111b5018ac7b79ed073bdf90739a9c307ab0d) |
| hive/E1B-LDST-DEVERR | [90891f939063](https://github.com/aien-dev/omega-mv/commit/90891f939063fce0e4c5118c4830e83695ab359f) |
| lane22/osc1-prerebase-backup | [f4a49b4c0d2d](https://github.com/aien-dev/omega-mv/commit/f4a49b4c0d2dacba442ba8aa4fdfa0ecff9edc1d) |
| local/t2-transc | [01615e087645](https://github.com/aien-dev/omega-mv/commit/01615e087645473bc09d2217289c48ee2254b1a4) |
| lt/cw1-mut-rebased | [065d4a621926](https://github.com/aien-dev/omega-mv/commit/065d4a6219269b3bb92a36aa7052e35a5ece85b4) |
| lt/cw1-rebased | [29c90d2a3979](https://github.com/aien-dev/omega-mv/commit/29c90d2a397979452e2a47f9e0321a4f431030f5) |
| lt/cw1c-camp | [1d0c7f940eb8](https://github.com/aien-dev/omega-mv/commit/1d0c7f940eb8ed479c9cb8a7dd83d82a9a255c38) |
| lt/cw1c-diag | [dd16e45f952f](https://github.com/aien-dev/omega-mv/commit/dd16e45f952f9b1b460b003c0388053c6a76b23e) |
| lt/cw2-rebased | [dfc424c70c6f](https://github.com/aien-dev/omega-mv/commit/dfc424c70c6faebf8a81b4b93fe3f76ed0d210b8) |
| own30b/a16 | [e69811486fcd](https://github.com/aien-dev/omega-mv/commit/e69811486fcd18212830b440b65df5dbf1aff666) |
| own30b/a16b | [848a999baf20](https://github.com/aien-dev/omega-mv/commit/848a999baf208ffc8c14872803cd56da6fb829d4) |
| own30b/decode-mega | [d9b9b6a2564e](https://github.com/aien-dev/omega-mv/commit/d9b9b6a2564ee93f1b30b71b0427b7f9ae685024) |
| own30b/fp4 | [f38652b93899](https://github.com/aien-dev/omega-mv/commit/f38652b93899fbb881e98b4586270fb4c65bb697) |
| own30b/rc2 | [848a999baf20](https://github.com/aien-dev/omega-mv/commit/848a999baf208ffc8c14872803cd56da6fb829d4) |
| own30b/rc3 | [4f3d45e7f370](https://github.com/aien-dev/omega-mv/commit/4f3d45e7f370c22169865c97445c6cadbd241342) |
| own30b/resident | [802a0101ebf1](https://github.com/aien-dev/omega-mv/commit/802a0101ebf13944afeb8be8e4c8f71be2348397) |
| own30b/spec-batch | [063e6d12fac1](https://github.com/aien-dev/omega-mv/commit/063e6d12fac1bb49ac4d88bfb6f5ea3eff2939ff) |
| own30b/spec-exact | [b9ae009620e2](https://github.com/aien-dev/omega-mv/commit/b9ae009620e26acb897c5350f72449c9803086fa) |
| polyglot/asm | [76da69486fb1](https://github.com/aien-dev/omega-mv/commit/76da69486fb19686963f29b3e3edffd1312a58a6) |
| polyglot/audit | [570d51263a7e](https://github.com/aien-dev/omega-mv/commit/570d51263a7e04471981ac68c8cffdaf9be9ab8f) |
| polyglot/encoder | [4c62e4b73bba](https://github.com/aien-dev/omega-mv/commit/4c62e4b73bbae4bf56ebc8ee412cc7fa6fdd57c2) |
| polyglot/harness | [8b0befeb13eb](https://github.com/aien-dev/omega-mv/commit/8b0befeb13ebd4f3789417633e1f9932f6a5bf7f) |
| polyglot/mojo | [94a464e7fe3c](https://github.com/aien-dev/omega-mv/commit/94a464e7fe3cecb8f855e3f496192b0ea38f5f53) |
| polyglot/review | [def197d19de5](https://github.com/aien-dev/omega-mv/commit/def197d19de53b6564b1fb4bffc932d8a001f2a8) |
| pr309 | [fa18e58a86a7](https://github.com/aien-dev/omega-mv/commit/fa18e58a86a7402de785e07044f5f444a1dcca3c) |
| pr309b | [fa18e58a86a7](https://github.com/aien-dev/omega-mv/commit/fa18e58a86a7402de785e07044f5f444a1dcca3c) |
| seed/c2om | [2e175d60397f](https://github.com/aien-dev/omega-mv/commit/2e175d60397f66e9fbd4820572190b1c71eb9d13) |
| serve/q36-prefill-v2 | [9a0d533daf81](https://github.com/aien-dev/omega-mv/commit/9a0d533daf81292b8a8e1f6e2d3f5e1945aab77d) |
| speed3/wm-combined | [06e550f46012](https://github.com/aien-dev/omega-mv/commit/06e550f460128d1aa63dca6e1aa30810fc13df59) |
| fix/471-fp8-libm-free | [45133c2a7fdc](https://github.com/aien-dev/omega-mv/commit/45133c2a7fdc45fc9447d604e0ad8b5285fd42d8) |
| fix/492-imad-hi-rc-64bit | [9ba5b614e4a8](https://github.com/aien-dev/omega-mv/commit/9ba5b614e4a8548929c166292ed6d95382374d92) |
| fix/mufu-ftz-462 | [cfadf764a394](https://github.com/aien-dev/omega-mv/commit/cfadf764a394c2741015e4eb888ab2fc8525b3d9) |
| g1/glyph-old | [fc39434a3f36](https://github.com/aien-dev/omega-mv/commit/fc39434a3f36643429439bfd753ff2c913a9dd06) |
| gen/abstract | [856cc9772f5f](https://github.com/aien-dev/omega-mv/commit/856cc9772f5fbdddb1590f3572a271058cd7bba6) |
| gen/protocol | [23c9c7e93040](https://github.com/aien-dev/omega-mv/commit/23c9c7e93040f6a71cd51e17f8b9c5ef8d475ad4) |
| gen/retrieval | [4a48b6132586](https://github.com/aien-dev/omega-mv/commit/4a48b61325865b1edc2d021d621a51f0f515963d) |
| one-layer/glyph | [6cf6ea382663](https://github.com/aien-dev/omega-mv/commit/6cf6ea3826632d90bafd719db5b9c31f3a0231c3) |
| tern/kt1-speed | [213c7b7a899d](https://github.com/aien-dev/omega-mv/commit/213c7b7a899db43598d8cdeb620cf55067f47860) |
| train/encoder-r5 | [7c165d86390c](https://github.com/aien-dev/omega-mv/commit/7c165d86390c6e817d2c70e2b97959c51891a636) |
| train/tgemm-cut1 | [e9a0d05ac3d3](https://github.com/aien-dev/omega-mv/commit/e9a0d05ac3d3a1efe0cefb132901f0e80f80dc77) |
| train/tgemm-cut2 | [b34345880a86](https://github.com/aien-dev/omega-mv/commit/b34345880a8610e96a0ccd36bda4479290c23341) |
| fix/492-imad-hi-rc-64bit | [950083197176](https://github.com/aien-dev/omega-mv/commit/950083197176978e94228d8a8abb12dbe5e8ea32) |
| prep-498 | [c91dfc095235](https://github.com/aien-dev/omega-mv/commit/c91dfc095235db058afedc52e0b7ca5df824606f) |
| prep-509 | [3ae95f324426](https://github.com/aien-dev/omega-mv/commit/3ae95f3244265ba4039581dcfefc657343981d4f) |
| prep-513 | [7908e9883aff](https://github.com/aien-dev/omega-mv/commit/7908e9883aff7410d43918a7089d061cc3928e59) |
| prep-514 | [f827829c4e5b](https://github.com/aien-dev/omega-mv/commit/f827829c4e5b03d85a078c6c3b45ac23a4ff7d92) |
| bench/prime-race-gb10 | [c7086da992ff](https://github.com/aien-dev/omega-mv/commit/c7086da992ff37e4cc955cbdc9c7f042e49d6f27) |
| bench/prime-race-opt | [9dee7b157ec7](https://github.com/aien-dev/omega-mv/commit/9dee7b157ec769fae745f63b4022a1d17eb06adc) |
| feat/gpu-reconvergence-308 | [1cc9b6af85ba](https://github.com/aien-dev/omega-mv/commit/1cc9b6af85baf29ca2cb79c22133f1aa06eb2e0d) |

### Unfinished working-folder snapshots

| Source folder | Saved commit |
|---|---|
| omega | [5bd8ef139136](https://github.com/aien-dev/omega-mv/commit/5bd8ef139136acca827f2e314fec0b850ff9185d) |
| D | [430f5ce3c5c5](https://github.com/aien-dev/omega-mv/commit/430f5ce3c5c547a756597b8286e8f929a7ff14e0) |
| wt-CAND1-omega | [4cfe72836428](https://github.com/aien-dev/omega-mv/commit/4cfe7283642891b01f24f9455d562c33faa1e8ed) |
| wt-cand1-overlay | [aee1f2b46655](https://github.com/aien-dev/omega-mv/commit/aee1f2b4665516c5ce55d7336c2539b1967a3878) |
| wt-omega-chip | [4719c1da9f6a](https://github.com/aien-dev/omega-mv/commit/4719c1da9f6ab6d45bd91d129d1ea9791327f876) |
| fix-physics-lock-make | [8e41f6e3ccce](https://github.com/aien-dev/omega-mv/commit/8e41f6e3cccec77fdb43c719e21c0bff70286f5e) |
| fix-r11-living-run | [368dcb7a19fe](https://github.com/aien-dev/omega-mv/commit/368dcb7a19fe240b681fe681809df27c7b50cffa) |
| fix-tv-inserver-base | [1a8cacbab79b](https://github.com/aien-dev/omega-mv/commit/1a8cacbab79b00a00ffa8f90fe33f74317eb2ffc) |
| crumbnotes-omega | [a91ff6b10684](https://github.com/aien-dev/omega-mv/commit/a91ff6b10684f9e120c0e06ee353430f5da30069) |
| omega-crypto639 | [54425bc4558a](https://github.com/aien-dev/omega-mv/commit/54425bc4558a9f67fc1b3ed05be9a1b07ad35c53) |
| codex-turing-energy | [56b72b0ae951](https://github.com/aien-dev/omega-mv/commit/56b72b0ae951134da99715970df62b11d48e7510) |
| omega-astra-visibility | [2818c92c023f](https://github.com/aien-dev/omega-mv/commit/2818c92c023fbe4ad8234a920313232918baac08) |
| omega-lane17-comp-hygiene | [114899e9256b](https://github.com/aien-dev/omega-mv/commit/114899e9256b71c958511348cbc380d2666df14f) |
| omega-ev-gate14-8024e9a | [4d728473fc9b](https://github.com/aien-dev/omega-mv/commit/4d728473fc9bf8b1c8f19fb598d93fe5d64e13ce) |
| omega-stalegen-diag | [60491fdb5ae0](https://github.com/aien-dev/omega-mv/commit/60491fdb5ae02f9a051894dac73280fa054ed50b) |
| omega-argus | [300c2a2fca8b](https://github.com/aien-dev/omega-mv/commit/300c2a2fca8bc96fc2f94d1d82cc7198a9901131) |
| omega-benchmarks-native | [9836beb0822b](https://github.com/aien-dev/omega-mv/commit/9836beb0822b21290a69258d7775da3ed893bf39) |
| omega-c2om | [13f82755b398](https://github.com/aien-dev/omega-mv/commit/13f82755b398e9a5f0d5a644e131c7fd980d237c) |
| omega-c3-scan | [c11f352f4d53](https://github.com/aien-dev/omega-mv/commit/c11f352f4d53d4e190e5b5dced291736aff2fefb) |
| omega-crumb-gen64 | [c9387820e16b](https://github.com/aien-dev/omega-mv/commit/c9387820e16bb9ffbcb5898c94dfd2de0d5cbc42) |
| omega-c5 | [ed3e42b99b4c](https://github.com/aien-dev/omega-mv/commit/ed3e42b99b4cdeb9d9a1644113556c79ab8b45ed) |
| omega-decode-onchip-megakernel | [d950afa6a8d0](https://github.com/aien-dev/omega-mv/commit/d950afa6a8d05c90d8304743c7ab6a99e8531e91) |
| omega-e03-246 | [83ef14c72c6e](https://github.com/aien-dev/omega-mv/commit/83ef14c72c6e2435e563fbc8c3cc48bff37f6e7f) |
| omega-effect-cap64 | [d8d6c6ea3bc3](https://github.com/aien-dev/omega-mv/commit/d8d6c6ea3bc36dd4ee3344be567d7cb8b05eae10) |
| omega-empirical | [d01b50d88a55](https://github.com/aien-dev/omega-mv/commit/d01b50d88a55928aaad0d7c6acc03903e548aa56) |
| omega-gdn | [ec2615047d43](https://github.com/aien-dev/omega-mv/commit/ec2615047d436d81e5d9d2e8d35491b2eb2cc4c9) |
| omega-gdnpf | [f1a293b08457](https://github.com/aien-dev/omega-mv/commit/f1a293b084573c6cccd3b67bdf67f114eca77192) |
| omega-golden | [f1751d287aaa](https://github.com/aien-dev/omega-mv/commit/f1751d287aaa55c4704d24b0f15db96e9294c544) |
| omega-gpu-engine | [0ead5e93119c](https://github.com/aien-dev/omega-mv/commit/0ead5e93119c798ebf8f062568138e2317bbef49) |
| omega-lane2-evid | [cf4a44c99d5f](https://github.com/aien-dev/omega-mv/commit/cf4a44c99d5f1356e62517bdd05f7653b2d8ab4c) |
| omega-in-omega | [f515e93964d8](https://github.com/aien-dev/omega-mv/commit/f515e93964d845f205ad92e7f62830bb7ca84d08) |
| omega-l356-base | [f746d07b730b](https://github.com/aien-dev/omega-mv/commit/f746d07b730b4a9e606908a64c49f04c6a555a26) |
| omega-ma-turing | [f5d71656b3fe](https://github.com/aien-dev/omega-mv/commit/f5d71656b3fedd934f53aba1b2da439e2e8dcdd2) |
| omega-levers-stroke | [22b8c5e4f7d2](https://github.com/aien-dev/omega-mv/commit/22b8c5e4f7d2e438dc29a6e653c9a1dff04b5936) |
| omega-mixed-algebra | [4ab66ef10558](https://github.com/aien-dev/omega-mv/commit/4ab66ef1055812602f312b27856d8333c5f13c32) |
| omega-mixed-algebra-gpu | [419f3c5cbd25](https://github.com/aien-dev/omega-mv/commit/419f3c5cbd25385b4e801816a170058066346e86) |
| omega-modules | [8bbe990953df](https://github.com/aien-dev/omega-mv/commit/8bbe990953df49dbdfe218266a1c7dee97b809f2) |
| omega-no-words | [155bd61391af](https://github.com/aien-dev/omega-mv/commit/155bd61391af8a03b51b631f9ea285b52622706c) |
| omega-omegese-gpu | [f753bd76311d](https://github.com/aien-dev/omega-mv/commit/f753bd76311d4409d2b0fd04673ab5e3bc0267ae) |
| omega-omegese-gpu-front | [744d6a0ac07d](https://github.com/aien-dev/omega-mv/commit/744d6a0ac07d5a88e316e65bffcfb3ae807ebdd8) |
| omega-omegese-mask | [8192038e7aa8](https://github.com/aien-dev/omega-mv/commit/8192038e7aa863ef4557f8a18eae48e2f898a1c2) |
| omega-phase-twin | [e8c46ac067e2](https://github.com/aien-dev/omega-mv/commit/e8c46ac067e2e925b766f861187f490b8f479285) |
| omega-polyglot | [2b6d4280d263](https://github.com/aien-dev/omega-mv/commit/2b6d4280d263ef4c3d547dd4ca86053d1fd85084) |
| omega-polyglot-asm | [74a7816c1a13](https://github.com/aien-dev/omega-mv/commit/74a7816c1a1323bdb1f4ee6ca10ce2d475fc9251) |
| omega-polyglot-audit | [fadabcf4b3ca](https://github.com/aien-dev/omega-mv/commit/fadabcf4b3ca6ce89775da6bdf47a8c61cc068b6) |
| omega-polyglot-encoder | [6828e6919af7](https://github.com/aien-dev/omega-mv/commit/6828e6919af77895f7de41ee84c3a2984ba8b643) |
| omega-polyglot-harness | [2b165cda2aeb](https://github.com/aien-dev/omega-mv/commit/2b165cda2aeb3cc6612759592a7aadebed9bd829) |
| omega-polyglot-review | [e0a24b604b03](https://github.com/aien-dev/omega-mv/commit/e0a24b604b0322d1f942ebada5407983d5b102bc) |
| omega-polyglot-mojo | [5f4c77ded29b](https://github.com/aien-dev/omega-mv/commit/5f4c77ded29b47ff73f2e8500b4598a992527c14) |
| omega-program-id | [df03dc1590ad](https://github.com/aien-dev/omega-mv/commit/df03dc1590ad2fb5b5f429932667a0df0d46740b) |
| omega-q38-speed | [b59950bb86c3](https://github.com/aien-dev/omega-mv/commit/b59950bb86c384b1fd7d41c690a93205661fd17b) |
| omega-prefill | [f06b654cf082](https://github.com/aien-dev/omega-mv/commit/f06b654cf08269e32f56cf0519324d5ef3dcd940) |
| omega-r12c | [11c82bf9e377](https://github.com/aien-dev/omega-mv/commit/11c82bf9e377d1adf436a606da17d205b2f125a2) |
| omega-r13 | [1b977a19ac85](https://github.com/aien-dev/omega-mv/commit/1b977a19ac85680e7a623fb46f0bbb2987ffc9c8) |
| omega-r14 | [12c36d8a888d](https://github.com/aien-dev/omega-mv/commit/12c36d8a888d2abf25a7d3d7b8718f1d3a54da28) |
| omega-r15 | [11a33b4396cb](https://github.com/aien-dev/omega-mv/commit/11a33b4396cb9cc832dd1b7b299281728091b0c7) |
| omega-r16 | [345a289426fd](https://github.com/aien-dev/omega-mv/commit/345a289426fddb85772a4e07058f85919f090015) |
| omega-r7 | [e1917f3dd397](https://github.com/aien-dev/omega-mv/commit/e1917f3dd3972f1f9588b03ec5dc87d4cc0d8c95) |
| omega-r9 | [9cd8a940aa74](https://github.com/aien-dev/omega-mv/commit/9cd8a940aa74ab4bbf8fc8c2e2f1d8a478ff1395) |
| omega-rig-gb20x | [8d5c7ba3416a](https://github.com/aien-dev/omega-mv/commit/8d5c7ba3416a43b2cc94d6c5e2a3caf0986d6e0b) |
| omega-rx-authority | [2611f70bd7c7](https://github.com/aien-dev/omega-mv/commit/2611f70bd7c70192047119f02056d6a2482e82a8) |
| omega-rx-body | [bd9d26b67727](https://github.com/aien-dev/omega-mv/commit/bd9d26b67727b8a2c65a514278837d13820fb987) |
| omega-runtime-r2-hello | [cc41e1113a43](https://github.com/aien-dev/omega-mv/commit/cc41e1113a437f2295451478c2fe291684e43ffa) |
| omega-rx-harden | [0a4916cdc1c2](https://github.com/aien-dev/omega-mv/commit/0a4916cdc1c24fb9505d67aab477a97bbd984a6e) |
| omega-security-1 | [f4d832d1752c](https://github.com/aien-dev/omega-mv/commit/f4d832d1752cbb5d083b2b558f98de9ec5ca19c3) |
| omega-security-2 | [b54c8c91e4b2](https://github.com/aien-dev/omega-mv/commit/b54c8c91e4b2059fe5dd1775706af1ddf4b50955) |
| omega-seed56 | [17d701033403](https://github.com/aien-dev/omega-mv/commit/17d7010334038c2691efa27333deccea5a53c8dc) |
| omega-speed7-fp8kv | [3c0a0c0ea699](https://github.com/aien-dev/omega-mv/commit/3c0a0c0ea699cc96edacc75e7449d8cfa9e68504) |
| omega-toc | [26e19c27e713](https://github.com/aien-dev/omega-mv/commit/26e19c27e713b86c506ac7451c70da49d915d0b8) |
| omega-turing | [be2bad456f89](https://github.com/aien-dev/omega-mv/commit/be2bad456f8970c3fe1fdfef0941afb47def9433) |
| omega-two-pipelines | [47d5895cfe87](https://github.com/aien-dev/omega-mv/commit/47d5895cfe8758f2226c09edcc537c6b8c88fceb) |
| omega-ty12 | [b7aec7699fea](https://github.com/aien-dev/omega-mv/commit/b7aec7699fea36f6e099e2351c116a7ee43a9051) |
| omega-ty45 | [9346fe14ccf7](https://github.com/aien-dev/omega-mv/commit/9346fe14ccf7fd8b470280bf62c7a37ba8afb406) |
| omega-typewriter | [ed88d399af1f](https://github.com/aien-dev/omega-mv/commit/ed88d399af1faaada6944598b66911ba99aa35ca) |
| omega-visor-qualify | [09a0def49623](https://github.com/aien-dev/omega-mv/commit/09a0def496232bbdbe1a4d66926d549dc247acba) |
| omega-visor-realize | [2645350eea72](https://github.com/aien-dev/omega-mv/commit/2645350eea7256632cee035fb8c4de3242fcf837) |
| omega-visor-review | [d4094fa74936](https://github.com/aien-dev/omega-mv/commit/d4094fa74936de670a8e996b72fd1ca52f836649) |
| omega-verification-614-618-20261022 | [60a0d7d2e1fb](https://github.com/aien-dev/omega-mv/commit/60a0d7d2e1fb3fd5390cd7e15dbd90c6fb7498e5) |
| action-graph | [c6fc156aa4df](https://github.com/aien-dev/omega-mv/commit/c6fc156aa4dfb02a4884980a995f78db007853e2) |
| branch-reuse | [56b6e38f715b](https://github.com/aien-dev/omega-mv/commit/56b6e38f715bc6d138d8e67958f1a17d80431eaa) |
| cap-gen-64 | [23bbfe658453](https://github.com/aien-dev/omega-mv/commit/23bbfe6584536424536e832cbf2d6901530f3ead) |
| capability-query | [56b200f693b6](https://github.com/aien-dev/omega-mv/commit/56b200f693b6459b1f8b3ce56cc66284e8c22107) |
| cognitive-routing | [070833501bab](https://github.com/aien-dev/omega-mv/commit/070833501babdb88e5992db9429fb3221d11c4d1) |
| evidence-digest | [0c12e52f0e56](https://github.com/aien-dev/omega-mv/commit/0c12e52f0e562edaf62faeccfcc75ed6e03a2f81) |
| incremental-semantics | [15ea6360f3ed](https://github.com/aien-dev/omega-mv/commit/15ea6360f3ed4fde15c0cdf15fa0f8782e4c7f79) |
| m18-receipt | [6c144b4fae5e](https://github.com/aien-dev/omega-mv/commit/6c144b4fae5efa0a6b45b4ac6c91794e4b91535e) |
| merge-55 | [4f1c90c9f7e4](https://github.com/aien-dev/omega-mv/commit/4f1c90c9f7e419368804889dc6adb91591560ed9) |
| r11-miscount | [a5eb93ea0ca5](https://github.com/aien-dev/omega-mv/commit/a5eb93ea0ca5b09e717b4910b51a02a760e128d8) |
| r14-diag | [e404b9cf3901](https://github.com/aien-dev/omega-mv/commit/e404b9cf3901ab55c531796d35e65dcfe9769fe0) |
| r14-exact | [cff3c8a0aa4d](https://github.com/aien-dev/omega-mv/commit/cff3c8a0aa4d5dcf0fbdf254fb823fe3b7bdc3fb) |
| r15-base | [2bb4555608ec](https://github.com/aien-dev/omega-mv/commit/2bb4555608ec3fc87aa62d726935ce5affac6ff7) |
| r15-g7 | [8d85c3682bb0](https://github.com/aien-dev/omega-mv/commit/8d85c3682bb0991e4c3a384734313db3fe978f0c) |
| r15-integrate | [19343794a7bb](https://github.com/aien-dev/omega-mv/commit/19343794a7bbb852e912e736142421f4feef5aed) |
| r15-rework | [9058d321f026](https://github.com/aien-dev/omega-mv/commit/9058d321f026173ca17c3ea57eaf9ddd2e2b57a8) |
| r16-spec | [ac02b89ae1e6](https://github.com/aien-dev/omega-mv/commit/ac02b89ae1e61654c242bdaf066c6a5851e84eee) |
| reaction-heartbeat | [ceba40459c0a](https://github.com/aien-dev/omega-mv/commit/ceba40459c0acc789d2c2c3c4c715494bd4a99fd) |
| seat-stop | [9f3546f30bb4](https://github.com/aien-dev/omega-mv/commit/9f3546f30bb4d3d44cd2f7f4b7db2315cba35dab) |
| workflow-fusion | [a25d30aa4b64](https://github.com/aien-dev/omega-mv/commit/a25d30aa4b640697b1abd0ac91d45148e718ebcf) |
| world-faulted | [bb8b0d9ac369](https://github.com/aien-dev/omega-mv/commit/bb8b0d9ac369f299bf8f8bbcef7760303653d939) |
| wt-CAND1-omega-chip | [736851150dda](https://github.com/aien-dev/omega-mv/commit/736851150dda5f33cd971baf40716480e1daf3f8) |
| l396-C | [025a28762782](https://github.com/aien-dev/omega-mv/commit/025a2876278255d57106acafc09324f7a8b72177) |
| one-layer-exp445-tools | [77d3b022eb77](https://github.com/aien-dev/omega-mv/commit/77d3b022eb77bc374abb9ac7a5d8502aff5d5126) |
| abstract | [ba78967bc1ca](https://github.com/aien-dev/omega-mv/commit/ba78967bc1ca40d45a8a1ee9367ec30817f65d53) |
| omega-glyph | [60afe8a87c04](https://github.com/aien-dev/omega-mv/commit/60afe8a87c04f59a3440a7ebfd25dcf1f0418eb0) |
| omega-mm-e2 | [2ff6659bf1df](https://github.com/aien-dev/omega-mv/commit/2ff6659bf1dfb6334f8abeadbc1d3048fb5f3396) |
| omega-tgemm-e1-c2 | [04afc625b3d4](https://github.com/aien-dev/omega-mv/commit/04afc625b3d418a9aed376c473bbcca0db597d63) |
| omega-num | [ba92503aed12](https://github.com/aien-dev/omega-mv/commit/ba92503aed121a80dbb65160a8f4cac2ef68a93d) |
| omega-osc0 | [7d7e50debd61](https://github.com/aien-dev/omega-mv/commit/7d7e50debd61fb0b3d123fa97b3e48b20cecdfbc) |

## Verification and limits

Unpublished blobs were read and checked for credential filenames, common token/private-key
patterns and oversized objects. Two CUDApp audit-selftest blobs contain intentional fake
secret negative controls; those exact blobs were reviewed. Pattern screening is not a
formal security audit. Original source tests were not rerun for recovery publication.
Working files selected for capture were reread and compared before creating snapshots.
Root publication and reachability are verified separately after upload.
