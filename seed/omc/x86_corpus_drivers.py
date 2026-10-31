#!/usr/bin/env python3
"""Read-only, content-pinned FR-9 callers and scratch drivers.

This is a verification oracle, never a compiler/backend dependency. Bare images
are embedded verbatim and CALLed through the compiler's documented private ABI;
no appended main substitutes for the image being qualified. Return observations
include all 64 bits and every caller-declared observable array (including edits).
Kernel exit codes 100+trap retain their meaning. A corrupt-result wrapper is the negative control.
No candidate is executed here. The only subprocess is read-only git object lookup
for explicitly pinned historical resources; no device or service is accessed.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
import re
import shutil
import struct

REVIEW_REVISION = "becf5c817"
# Exact raw/composed/frozen source bytes reviewed against the original conductors.
# A changed source does not inherit execution authority from its filename.
REVIEWED_SOURCES = dict(row.split(" ", 1) for row in """
5e8a16d6c4fa8bf8581986ad4c3e35091a5a5574a6cada882b078f6c00f12159 seed/omc/contracts/bridge.om
0fbd9c605528135bf7edd6cc6642655c2b99fc2847049a1860acaf9da25df28c seed/omc/contracts/parity.om
88671904983dda8dc483dda9f98683e86d4448494d79c9cf1020e3d12f8aa7bf seed/omc/contracts/parity.omegese
e1109db40b1ab3f8b7c5b847bb9032810fc3dcf836ec58a0caf5459033636764 seed/omc/om2/s3/a-kept.om
e6e04d5f60f921be058b89ab369735e6b8aa9d398d836f713bbc8c688c95c513 seed/omc/om2/s3/a-ok.om
92c7813513f2bb511bbacc53e1a351a417cabc10139134e477bfb61483005c92 seed/omc/om2/s3/a-om1.om
1de96237beb1b2d7d76fb226cd930036d8cc1d8c82faf4114f3309a1396365a5 seed/omc/om2/s4/o-out.om
400babb171c1703b3abf1676529b483d8ba2f89fb76d6fbc0acc16b181cc5565 seed/omc/om2/s4/o-slots6.om
e2661535df50624a00f801322c397bf9f4699096fa90f2c556127c0f4fd13c0f seed/omc/om2/s4/v-fm-entry.om
3490824f6e8cb82e661c7a408e8e4264790875e9e0147914ad4b0571dde0fc79 seed/omc/om2/s4/v-slots6.om
f49369293ec1b01421084328e0629077f819d541422b59330b8c2d8e9fd16e13 seed/omc/om2/s4/x-two.om
e240ea92917b12b114204020a1e247c1cec43be423745f4c2a48e0bd7ca6fc95 seed/omc/om2/s4/x-one.om
24f7a41a210eb140669e957cd7f4f7c895e2637907a6e139888b384edc4fc0de seed/omc/om2/s4/x-noexport.om
1ca53826b7c4c2e544ea0954847c48f45fd171247b96cf80919c3391712e5010 seed/omc/om2/s4/x-32.om
3975d71de54566c04bf0331b230f598b9f3cf794cb37bae1a2d874bfea49adb7 seed/omc/om2/s4/x-abs-private.om
2fe76d4c44b3e3faf6bfd250ebec794ba5b3d11b6b9199366549b664d219cc38 seed/qualification/om2/baseline/slice-witness.om
1552ce25751cc7bcff786cc85022c31eeafc62451701248b56e32fb6b4cff46c seed/qualification/om2/leak/fixtures/s1-rot.om
22320be74b7ee939c78374cb4fe0b8e4f91d7ca266cc3b1e2dcb3477295991bd seed/qualification/om2/leak/fixtures/call-data.om
dc183284c582312a57530b2ae42090195a3e5c209e7d34c1f414887c65e7fa47 seed/qualification/om2/leak/fixtures/call-public.om
cfa1fad50465318fd6277cd87f78291914f02d3ef2eb6af16fa02c200cc05356 seed/qualification/om2/leak/fixtures/data-branch.om
bd318c0eecc7c01e29fa7e90113829937e509640cd4812a3fa509a960a572282 seed/qualification/om2/leak/fixtures/data-index.om
42d7e3ddc4be8d482cb625152b210f8c2cb002dfe8f02e23bc984d0c4d547e67 seed/qualification/om2/leak/fixtures/flags.om
24f1d2131c2d36cc810623d68c8ea2f8cdcccef29c111a71231b63a08eca1f2b seed/qualification/om2/leak/fixtures/len-loop.om
a73a75ff3bb10b96af698293190a96c804ca332461f37a067cb3c77fd43b5cfe seed/qualification/om2/leak/fixtures/local.om
c48dffbf50d65691e3d1d624c3e6944c225b2335c6b1fbb595c1b05e8e4452dc seed/qualification/om2/leak/fixtures/pub-unknown.om
f3b813530dec10b0d99f17ae29c981620c28e9d3f3dea4b16d97010a2247d830 seed/qualification/om2/leak/fixtures/public-in.om
18e969dffb1c8fcf826c1bb6078df00bd81e49d8403f1bf664a2a964be72ef51 seed/qualification/om2/leak/fixtures/public-out.om
21e366be6ad718395e5018186a3915d57ea02147a54b247ef1f69f001960f642 seed/qualification/om2/leak/fixtures/reachable-trap.om
d65dd8eefd07ecb2e6fa2498fbedb1ad4919681632e2bf99b5b0fffe6f3c56b5 seed/qualification/om2/leak/fixtures/svc.om
27423284f3ce99b7f00a5497456a697d83445427c4f5c5ba639db8d06165bfe5 seed/qualification/om2/leak/fixtures/trap-check.om
7bce001ac35fee18b0205df59aeeaea62b1d2eea68e336c0262ad3741f40028e pack:seed/qualification/om2/c2/corpus.pack:57323:784
124c19e6343133d45146e565eeb9307bbe7350a07fca01a736d6da8c3e5d4863 pack:seed/qualification/om2/c2/corpus.pack:96:6318
79857ee82002d11d3df4c0c2a99523aa3419d18c5fc188a41efc778887b4baa3 pack:seed/qualification/om2/c2/corpus.pack:6510:5933
7f034814303a8a40b4bc56383ddd55ab2ae8460291f80793340bc3b0ac024e21 pack:seed/qualification/om2/c2/corpus.pack:12540:35333
459a60da8e660ee1f433a9679bb41467074fda45997e5168e23e92ebad0d570b pack:seed/qualification/om2/c2/corpus.pack:195375:291
07bc11a69f0d629e0c262056061789114d8710505a308e3e2a5ac42ae73dee94 pack:seed/qualification/om2/c2/corpus.pack:195762:7593
5ff3b6cddb9f66971ecb7b58da2d2f07e3df9e7176dd1ba50a2c3a36f2447e81 pack:seed/qualification/om2/c2/corpus.pack:206091:80
02b9d845454e7379d4d55f3c78af7e9ddd9f87f15d196765b338ee5ae6416965 pack:seed/qualification/om2/c2/corpus.pack:206265:66
3323f23ff905d4915ced5c83d97d772c5b4e4e1347ba738ae3752ef74d99cc0e pack:seed/qualification/om2/c2/corpus.pack:206425:44
06149063fb825604227f54d226fbce04afe48b755f6ccbe2690ef35afe1c5a65 pack:seed/qualification/om2/c2/corpus.pack:206563:68
3b0acd8ea8b0a46d3131378ae447ffd337e15e0b92da1c45ba2c1a2383dc4b54 pack:seed/qualification/om2/c2/corpus.pack:206726:120
cf0a8200ec4341d1c4634bceb6bab5e7cca15bae510ca4d6f41982fe1ad70a9c pack:seed/qualification/om2/c2/corpus.pack:206940:72
e9548779048e3d97b48f7d9904fa832aa47c8a6158c9355d2b88eefa50c87589 pack:seed/qualification/om2/c2/corpus.pack:207565:82
6c703b6f9362707125bf2304ef2aa2688dc82fce304d36ad7ee8876528c233a6 pack:seed/qualification/om2/c2/corpus.pack:208183:80
f744e3ec012deac392e62d6a8d4133efb7681887d31675d6ff516d4aead07e64 pack:seed/qualification/om2/c2/corpus.pack:212407:116
fe772bcf3433b1ea0daa0fda78b877790bbf2eacf251ce05f64f0f77dede0e51 pack:seed/qualification/om2/c2/corpus.pack:212617:99
e0382bdae6aabd733c7eb4ba314c9feca55242a8fa25360a6442cde40820a8f0 pack:seed/qualification/om2/c2/corpus.pack:212811:119
0481442ea45287b9a1a2c5b9116aea6b36dafd0a2d7d89019aa923b3a7d4a356 pack:seed/qualification/om2/c2/corpus.pack:213024:90
9b6e72361efce2dbdf209e05a74b89d3a0bf2491818db684d6aa110f3159243c pack:seed/qualification/om2/c2/corpus.pack:213208:56
5b09682d993bbb9afb96fdbc0f7a75c8e2cf14fc094b5b6c6e1ecf7fcd6ee374 pack:seed/qualification/om2/c2/corpus.pack:213358:50
93d8728c52bfad927150d0e08d74a1eca29256489dd3f3acc48c1ad4034f6823 pack:seed/qualification/om2/c2/corpus.pack:213503:158
3c78cf83258748b829d8248670f7b46d69351f8e389b9ddecb3ccbfb6548e1da pack:seed/qualification/om2/c2/corpus.pack:213756:188
93fa5e1cce0ea59252fbb8203d1a7fe15b40ebcb82cf7d4090d62fa1abe4d477 pack:seed/qualification/om2/c2/corpus.pack:214039:114
2efe0c6e3663a40562589b98f4229f0ac2a43ab264c53b4fbca40d11c76edbfd pack:seed/qualification/om2/c2/corpus.pack:214247:38
b078ae26e824ad0cb671108a896862ee033c90c6f4394637684b15bd90e95322 pack:seed/qualification/om2/c2/corpus.pack:214379:87
52a9a88352cc3e9c60832187586fafe0d05748693ce55141844a9d5030229a6e pack:seed/qualification/om2/c2/corpus.pack:214561:105
ac0eb5306b4233a8c49dc9db8d503ec343b76c0bac0b3681c22b3d0d866a99f3 pack:seed/qualification/om2/c2/corpus.pack:214761:120
aca09a84add052ccf4416bc8f7b0d01a71d9fe0b6de8957322b90a88a3597f88 pack:seed/qualification/om2/c2/corpus.pack:214976:132
4260833a70ad7a0a858a0b7910b0364a426580e9aba1c622565fc140a42b58ae pack:seed/qualification/om2/c2/corpus.pack:215203:176
38d8b37e5878695b29d5308a8b43ac2dc9877908106528515104423027cff52b pack:seed/qualification/om2/c2/corpus.pack:215473:97
55b567b2c43c115ec5cfafd9ec7d0085feef7ca688da5a63c34203224b27e35e pack:seed/qualification/om2/c2/corpus.pack:215665:113
6a56da67212db3956af91e36a2b680b9c78f98bb8d9f0e76a49eaac7ee8cffd1 pack:seed/qualification/om2/c2/corpus.pack:215873:136
184e9efad7fccc9b147f4813b2434ae1a5cb9122a0ce423b3b7356be18849874 pack:seed/qualification/om2/c2/corpus.pack:216104:129
4098c21c8e3ef82a9d6bc61012537a3b30ef89568cb1bbe3b2cf3f33cad6595a pack:seed/qualification/om2/c2/corpus.pack:216523:184
895c35cbfdfc9d94103790214e47f5409af1b0d87788b4925b25fccb59d4e02c pack:seed/qualification/om2/c2/corpus.pack:216802:109
b5624972ed3b4394027eca96d94b670f7a8846af51d3083940e63c079c8649e4 pack:seed/qualification/om2/c2/corpus.pack:217005:71
14439646d9db8ec1c40b7646b3ec177d21ce8178a1e7c6c001134339cd8fab76 pack:seed/qualification/om2/c2/corpus.pack:217520:107
95a41a420eb97cac77023a066936cc4c9673e3224489d118834edd076232ab59 pack:seed/qualification/om2/c2/corpus.pack:218423:145
bd88c1daeb2c9f2bde005d46172b7b7e0b4ed2c95081ea106dd62f8caafb16a0 pack:seed/qualification/om2/c2/corpus.pack:218663:157
fe9f95b097d0137a3ddb4a62246c5e6875fbc19aded228c2f23c4679b84c2ccf pack:seed/qualification/om2/c2/corpus.pack:218914:64
4335f62c4d839f7a7d0addd992208b6107249d7620d840080a81076b3800bc26 pack:seed/qualification/om2/c2/corpus.pack:219072:59
23026d2191616675acd10e798835bcb290ecd259c1e17935019bb2f97021d9fd pack:seed/qualification/om2/c2/corpus.pack:219226:124
a55747b9894b8a8c07f33e6678b8f5482eee35096c97ff686be7ec7b7784d9c1 pack:seed/qualification/om2/c2/corpus.pack:219445:258
d8196e4bdfe690afeed43f7a52aefbb76f13ca42a6caa51fc61e70a585165213 pack:seed/qualification/om2/c2/corpus.pack:219797:99
9acda5f3fd9ddc1dc01923fa76a30a4667f2d0e8d460ba2a60c5d6d5fc27807c pack:seed/qualification/om2/c2/corpus.pack:219991:123
17b76ece94d8d691b3339b4d3f58443814c81d897a6e3f746981263674d32257 pack:seed/qualification/om2/c2/corpus.pack:220208:89
8ecfa30de1ec78883a3c4c40fc5a11aeb2646a38df403a8bf61da47d99017e1d pack:seed/qualification/om2/c2/corpus.pack:220392:230
a50c02817f492a2390f613aa5f7554ef6ef08b9a41d6ce73fa1eb1fa53191af1 pack:seed/qualification/om2/c2/corpus.pack:220717:341
efe5b555293ac23f0f3a6ece186f26a1cf1e85e0e007051dc7bff6d4ff9efa25 pack:seed/qualification/om2/c2/corpus.pack:304924:132
ab8ac8a7cd6129c802a46e93d9d74ff3998b5673e61929b2be23f945ec80cb87 pack:seed/qualification/om2/c2/corpus.pack:305151:194
38a27f10bed8c3efcbb1b5cf7f5b8aadcef92d3bc4b472655cffe513f5254886 pack:seed/qualification/om2/c2/corpus.pack:305440:402
bdf0f55318ab9b9e85a93c1a4d0722912542233b40fdb930cbd2848d5be92a51 pack:seed/qualification/om2/c2/corpus.pack:305937:136
f0f644177343e1a62312ce5c1fdb4ad5cfb2f76cd97650306661ffc7a68f6702 pack:seed/qualification/om2/c2/corpus.pack:306168:158
eb0aad938117ea324c5a90362261d08b5d325822ff71ad2274ff9ee9bb523038 pack:seed/qualification/om2/c2/corpus.pack:306421:123
472eddef7d2520b8b12f67468ca98e0ab01f0dc9468a8f9d767b54980da12f27 pack:seed/qualification/om2/c2/corpus.pack:306639:159
facc6323098519224f4f6b86fa26e86a4859097dd23e111d56d084fc81b4e083 pack:seed/qualification/om2/c2/corpus.pack:535426:187
67eaf5747e821c7c86c4d1bdaa4eeebaf3b41d70d8a998bc6a19509bc7d4b91e pack:seed/qualification/om2/c2/corpus.pack:535707:69
c6a5d45a10812d40dc81227e9afe52d25547f7fca8d4b7193e6f2016214251f5 pack:seed/qualification/om2/c2/corpus.pack:831934:490
5879c65c06c7099c95dc3d05ab0c3fc87b9506c5b868b577a23355d1fea58216 pack:seed/qualification/om2/c2/corpus.pack:844746:489
ffbe6e67cb3288050ebb8bc7bdff12cb08748b30f32adc27aee9a838f6c14fc4 pack:seed/qualification/om2/c2/corpus.pack:854503:211
71d7b9655470cddd00e2ea1d6607a3693ac8add4291d1b31274bad80953414d4 pack:seed/qualification/om2/c2/corpus.pack:861552:284
b706f5af251818d95bdaccfd29f754a85bd426c607cf654cd9438cdde5848b34 pack:seed/qualification/om2/c2/corpus.pack:874651:488
f49cd869b69efaff7eba8f483ad6e80b5cac2b964944cad5ae4300b6dd345d1b pack:seed/qualification/om2/c2/corpus.pack:881684:488
f175a5f86d183fa1fbc1d0952fc206f2caaec554cdec7c6047158b1baefdb831 pack:seed/qualification/om2/c2/corpus.pack:882851:492
114342a696e777f694ec56aa34f6bef277972b955e16257e7b3ca77b52706e01 pack:seed/qualification/om2/c2/corpus.pack:883438:491
f953762234d6f6d0755da130cb67202bb4c77515b58a4f9b02876a7fe24068af pack:seed/qualification/om2/c2/corpus.pack:891049:490
01ab9d2d4bd4b40ecbb184451713ba4b362491cb6ac1cefd6e46cd607a0d3103 pack:seed/qualification/om2/c2/corpus.pack:900992:492
3c3fece47ba700829719ca925f78c403ceecdadbf1205828f37b377f83e26243 pack:seed/qualification/om2/c2/corpus.pack:901579:491
4e0734c842150045b235814e0092da2359b4915ffbffc38fe7b905bc7db224f3 pack:seed/qualification/om2/c2/corpus.pack:909779:492
67a72da083033c035e9aafcfb4ce990a35e23491708d3c3f32962fb345314ccd pack:seed/qualification/om2/c2/corpus.pack:910366:492
0d3dfc65d5cdc76b7bdc3f8b1959ce7b7a6eccacab087e52a51040c75c7e5c41 pack:seed/qualification/om2/c2/corpus.pack:913293:490
e4847e6a337e7e28e95c871f2c5dfe60676a042b5ff62305ad76f457bb899d2a pack:seed/qualification/om2/c2/corpus.pack:932052:491
681fde125c39a896a431f9f4811c8e068d96b68bb4175dd6cfc73ebc6126d2a8 pack:seed/qualification/om2/c2/corpus.pack:944331:489
ce66b95e83083633c167acfa5221033ce3b864ba08ec8c56870611bcd4b28d41 pack:seed/qualification/om2/c2/corpus.pack:1543668:245
ecf2b472725535c92c07ebe753015bc37babd7f400c08ff32542f826b37ab296 pack:seed/qualification/om2/c2/corpus.pack:1546871:98
9985b767f544272064a1f9a4eb5c15a0748f523778211cace20924150801755d pack:seed/qualification/om2/c2/corpus.pack:1557428:244
82884e201f63f38bee3f590cbca258ef14bb1179664f288d1073b39a0e3c1497 pack:seed/qualification/om2/c2/corpus.pack:1558811:245
2e73fba9a6ef545a77c1f59d38e9df8ae5abf7713045fae9b363f2421c26c5c4 pack:seed/qualification/om2/c2/corpus.pack:1563255:244
e8988da4e074161ec2b337679da602be58562ff944649c52356f7d26a57874c7 pack:seed/qualification/om2/c2/corpus.pack:1564272:247
d1083e1cde3fc7424125a610da7262677cf66c0a1a3e4a75263defa378aae783 pack:seed/qualification/om2/c2/corpus.pack:1564614:247
7542ab65bc9c66f1d67ef7994bc0df545030918e9c530d2fc90406dc3dc3fa36 pack:seed/qualification/om2/c2/corpus.pack:1573817:243
defcde4c8039988786db6a0f9fce9d8fed93ae630cbe35418bae961dc6e70972 pack:seed/qualification/om2/c2/corpus.pack:1574833:263
21f1e8809792a8fc935cebed2ef42614df8bfaa142e175384ff6f285a84fb8aa pack:seed/qualification/om2/c2/corpus.pack:1575191:247
933054ca6f67bcfb407bd9771ed90820aac96ef7d8e4b31b738a72c67ad885ab pack:seed/qualification/om2/c2/corpus.pack:1575873:246
1379a69ab169b6fe9740e19771483ea2255ec3184b37dc26c6fc87607242592c pack:seed/qualification/om2/c2/corpus.pack:1576214:247
4b0d702b91a70d8c0b2f3939034083bae6a1fd8bd215331f345a09e803741252 pack:seed/qualification/om2/c2/corpus.pack:2282626:583
1cc53eca436923c42a5c1b5d9653cf955ed110e65c52015db5b7ee820588b419 pack:seed/qualification/om2/c2/corpus.pack:2312908:582
99f14641d129ff058e073ebb5860b1878e40b52c6d686b2f5509c65c830ef183 pack:seed/qualification/om2/c2/corpus.pack:2337699:361
a47b08518f60139ad7c6227a1f4b8a2121317e4d62f948048ae51f06f07d510b pack:seed/qualification/om2/c2/corpus.pack:2341160:585
f332321ea24ce957337cba446bdf451ef110e8cd9dd3bf83db0075f40bf2f626 pack:seed/qualification/om2/c2/corpus.pack:2348629:584
59972aab2ad6d3c7f924410e1f126ba9a5b7f312064cfa15a9ac1b696039f501 pack:seed/qualification/om2/c2/corpus.pack:2352021:586
46e33117fe665460bfa0b3988d069c982796a93a2b792824f43c24e03ad00b4c pack:seed/qualification/om2/c2/corpus.pack:2357446:582
d1fcf5dfb660d17797084f0fd4c3f085622cc75f60598b23648cb73f0418df3d pack:seed/qualification/om2/c2/corpus.pack:2403570:579
ae6c5d488d187b032ec176451502a54a815596ea50e90bf63fe1b034464b2bb1 pack:seed/qualification/om2/c2/corpus.pack:2407627:585
6c60cd43bd3d78530e0b769804155804ab46c0a5da50bf1fca6ecac4d104ac3d pack:seed/qualification/om2/c2/corpus.pack:2408307:586
3db75feb105dcea58d4280d31af836fb321ffc186b3cf938c4a5a8fb1c114708 pack:seed/qualification/om2/c2/corpus.pack:2417122:586
7b1110417f21b3f750322a85f3a40ece1f6ce1a891858b8ac527649ca223b42e pack:seed/qualification/om2/c2/corpus.pack:2417803:585
99690402fb3c371085b0a8a5ea8e77a0b534ddc9d9ab692c0e811fba3bc03c93 pack:seed/qualification/om2/c2/corpus.pack:2421872:584
63fc2697e34dea011d57320a5771f70185e47dfe6f19c0ec3dcdd133e2400034 pack:seed/qualification/om2/c2/corpus.pack:2422551:584
df520f8724c914e39b8f418e3100963dc1738c9a68a73047a1c16a4ae94e3124 pack:seed/qualification/om2/c2/corpus.pack:2440904:584
3c446741e2cbd9febeb3506588f4eb54d70de218c67f9b8496191f8f58d75c0a pack:seed/qualification/om2/c2/corpus.pack:2442261:568
959b6da3977b08db23cc1dc6a0494a8f620d763754194aeb7ecd96bdec1ed4e7 pack:seed/qualification/om2/c2/corpus.pack:2457530:499
49fc5ac7807eb14571db4017268d976de25aae1d26e0e3531a11d9095ec9f5ee pack:seed/qualification/om2/c2/corpus.pack:2468439:567
9475ad4fbee4ec2d66015d166240d549fc32c933c6566c4973e6795e635770cc pack:seed/qualification/om2/c2/corpus.pack:2483728:280
b89e46e462d7c21fd5d1323e279d3c08aa25fafcac0f744e95349905c01e4bf7 pack:seed/qualification/om2/c2/corpus.pack:2519558:568
ddf75f3347af715f8c67a7ed6c6fa6e2bf1b11c149d80a884969709023d054f2 pack:seed/qualification/om2/c2/corpus.pack:2521545:571
d6e4761d4135360016a816f33d7957227c60eecbdf2117b017bc55a5b8f1a9e5 pack:seed/qualification/om2/c2/corpus.pack:2522211:570
72c537b7bfea6a6d6c0c74537592e391d88358507faecd5d16c5aac7e90bff97 pack:seed/qualification/om2/c2/corpus.pack:2528857:586
b04e99e05e863709d15a802270eed60544476379c822dd7b48dc4455413a12f7 pack:seed/qualification/om2/c2/corpus.pack:2534217:568
65db3df333e9c9653977e2f958ee2a92356f9bd1c175bfa1a05ed5483de8017a pack:seed/qualification/om2/c2/corpus.pack:2542193:569
64b9b4c9a5a931084813bb3a00e4ddee88f3669c00fb50ad7865aa72b307e7aa pack:seed/qualification/om2/c2/corpus.pack:2542857:567
8d61ecf860da3472aba1fc1718354799463f0b9af5fdffd488370c68e3a92e35 pack:seed/qualification/om2/c2/corpus.pack:2733481:208
7e3b46c25031f0304f8fa769df461e191264185ac8f4a8242d302b67d74127ec pack:seed/qualification/om2/c2/corpus.pack:2735987:94
a2a7bc6c11af6728dd4220b8787ea49625e486dd435435f8cb95840a931a81e4 pack:seed/qualification/om2/c2/corpus.pack:2743364:207
9e660c3db31e9199730624b17cab807cbc71b078cd7cebac92cc91c063f584ca pack:seed/qualification/om2/c2/corpus.pack:2746275:210
d0304b0da3c4768d710b88e640e5b325ced4d6d909e5b24d2df9b00115dce928 pack:seed/qualification/om2/c2/corpus.pack:2746580:213
944339d8548b4cec73ed0da6b9a731e70aa9fcbdd955cb46fc8e14d7750a2d4d pack:seed/qualification/om2/c2/corpus.pack:2753538:213
b0bcc6c32c0a0aa3d7be755583f7ef4032289d9ec420632aefab95a87781e856 pack:seed/qualification/om2/c2/corpus.pack:2767127:152
981347dac653a9cab7c66fd8f1c6a283184658e2e78b563421ddec7d50db6dca pack:seed/qualification/om2/c2/corpus.pack:3120057:30
250361be99ced1246405b2e4a2e2de71373a90d0a0569fb43821ed8b106e045f pack:seed/qualification/om2/c2/corpus.pack:3386857:36494
4e8d5d5c536c2703abb0b85fd086fedca6841d53d4592a8c2c7c5491dd1697c1 pack:seed/qualification/om2/c2/corpus.pack:3490048:339
982fd12d6376e8d60ad1e8d2b2c2abc51a47a598543b058ecced8522a756a0d9 pack:seed/qualification/om2/c2/corpus.pack:3490482:356
3da4a848e9d1c1665c2f8a5047a731a60b48533253253b5cfc56cb562381dc06 pack:seed/qualification/om2/c2/corpus.pack:3492055:390
5867b20666409f323c83d9017cea126bc9ddaa0a4f4d5411e7553e90d40e710c pack:seed/qualification/om2/c2/corpus.pack:3492540:574
28932d16844b094298e10b2ddbd3e21499668ec5877d67dca19339100a04363f pack:seed/qualification/om2/c2/corpus.pack:3493732:311
cfccbeb34d3bfb051e01977b9acb2b6166c41bbfaba85a26c85c123643e8012a pack:seed/qualification/om2/c2/corpus.pack:3495087:1831
e7ea21711b45c957a6264e373e66f597c091b67809bd0c785113d2ea601e71d7 pack:seed/qualification/om2/c2/corpus.pack:3497014:1462
313b425a4e6b779633e06c0ed3dd6906730f39d230834b2921e4abffe9428a41 pack:seed/qualification/om2/c2/corpus.pack:3515537:428
2e011318d020bd5e0283bde413b74335b4e4b3c5b417e34b03caa832f5efcf66 pack:seed/qualification/om2/c2/corpus.pack:3516061:1403
63eab87101f8c71645b26023d422fb910c8d6ce603504608bfb395fc5a7a0cb0 pack:seed/qualification/om2/c2/corpus.pack:3517560:1034
4a6798fec8b13ca31dc5a7f6223956297e7285b94cd0d4757c269a120efe0bcb pack:seed/qualification/om2/c2/corpus.pack:3558924:366
28883f52212233aa4bc76182dec3733ad550f209a109a7bd57f5c4227a373e10 pack:seed/qualification/om2/c2/corpus.pack:3573443:533
767fc04dd38d62a2f1f700366a641a2a7ea7b5429e0341dcffb97bcc8558029b pack:seed/qualification/om2/c2/corpus.pack:3596649:1500
af68bd088bc58ce973cf53fcc96743dba41a489fe56a4a9dd4f3ecc3c0e3aea0 pack:seed/qualification/om2/c2/corpus.pack:3605118:230
4ae68017b93e47a9ddc638ef9d0f3e950c9639346102c510c36909e728f28dec pack:seed/qualification/om2/c2/corpus.pack:3609196:824
01fb867723854f43838a814a8571107e6fea2afba12444f57f35c9012741111d pack:seed/qualification/om2/c2/corpus.pack:3616121:145
716c1dc3004d55178e09e7c40fa0995f993eea8611e09a70a1f74b9c7a0cf644 pack:seed/qualification/om2/c2/corpus.pack:3616361:146
31ead9b2a44ca4b10fd637b0fd9ee339005d33b48a731795e71f57d8189628a5 seed/qualification/om2/sec/s3s4/slots-3v.om
175ca1647cb649ddb91bcb47fae4bd6823a86d43d3d22c5235d642de8457c423 seed/qualification/om2/sec/s3s4/slots-2v2s.om
b0bf509e7383e3532db4a548406f37bc6c8000e4ba0023c3f239eca724eea032 seed/qualification/om2/sec/s3s4/slots-1v4x.om
729054dd66a64449b34d9db594b7e2babc2c396d982a0da8a9cc6af03dc96668 seed/qualification/om2/sec/s3s4/slots-6s.om
278556501859427e66202c6a685f4af8d737aa395932463f633eb564193d83b3 seed/qualification/om2/sec/s3s4/slots-out6.om
f578c1f645e8a7e6112b2f45028fa8a018f73fad2149d0de16141607b0969ccc seed/qualification/om2/sec/s3s4/diag-site-plain.om
1ab250b1d4e7d2f437ee680cce5a8918752cf0ef8efe1d89b490270005ceb294 seed/qualification/om2/sec/s3s4/diag-site.om
06c137ece7a94b3e6f5e19043bd6045fbe5208019b4366a43de9cb7558cee587 seed/qualification/om2/sec/s3s4/diag-site-rolled.om
035d21b803806853451dc2f408cc29c3490714b7b73081dd8cfe4b283abb2f85 seed/qualification/om2/sec/s3s4/diag-site-unroll.om
f4c545da9cfed4a6884b39634a371c09353139992f0be377dcea2f4e65e9187d seed/qualification/om2/sec/s3s4/diag-site-nest.om
781ba1bf2768974166c10a3724acbf766080db732bee8d195fa3d872a5ab05c9 seed/qualification/om2/sec/s3s4/diag-site-rolled-plain.om
050c286588e3066c396697c397c999a6ac8aa98334cc9c3bb3fcb43a6d8c084f seed/qualification/om2/sec/s3s4/diag-site-unroll-plain.om
432b642f9b3f5a0502705ef06d61502f4312e82c85adb58b64e5791a5d553e49 seed/qualification/om2/sec/s3s4/diag-site-nest-plain.om
3bd0be5b72fc92825742422dc7157196d85d57891b81c8ce89a6df8969584a82 seed/qualification/om2/sec/s5s7/a5-allops.om
9cb0393b2c6c8f99ad93e5af2db0ae4bbf17de884767de162f9737c20717f369 seed/qualification/om2/sec/s5s7/a5-exact.om
0ec2b5eb427d759eb7563f9dbf8cd1a921a23b9e80c9d7f779315490b23f274c seed/qualification/om2/sec/s5s7/a5-kept.om
1e0a5f4c22cee6170a7f6787fb6c1cc215e1ff9aa0e997bc8f7450c06b3a072b seed/qualification/om2/sec/s5s7/a5-rot64.om
373509e26a12a1c579bf4021d1c275929da7c534812ecdf62c9d0486f11d3eff seed/qualification/om2/sec/s5s7/a5-swap.om
ef1b3dc9f12deef3e2536b6689e79f529f216bff87c8c7b8dd5d5f259b7f3703 seed/qualification/om2/sec/s5s7/abi/abiv.om
743fb07c03061177ce5133926dc442d21d3ba1b74d133f661a2de9ccec1ace92 seed/qualification/om2/sec/s5s7/abi/abiw.om
b21d8c82a5a3648011325261bac9481e9e3147dd29e17b491e04008bda289295 seed/qualification/om2/sec/s5s7/a5-exact.om+seed/qualification/om2/sec/s5s7/pr.om
56fbd35bc5e262553585cdd18a54524622c638fff89d9bf0f84d9c42884c8366 seed/qualification/om2/sec/s5s7/a5-allops.om+seed/qualification/om2/sec/s5s7/pr.om
d7df44a4f803645594ec1e758201d8e1c672c4ebe94c553ed9b4cdca98bb4607 seed/qualification/om2/sec/s5s7/a5-rot64.om+seed/qualification/om2/sec/s5s7/pr.om
c0c8cd2d3c8c9fe4e1cb8f9cfebb5af8d5c625368db1d9af713b0acb33504b24 seed/qualification/om2/sec/s5s7/a5-kept.om+seed/qualification/om2/sec/s5s7/pr.om
c7ceccff2f370d5c82df978399a82372f6d354703d9ff263de8d560df13d3481 seed/qualification/om2/sec/s5s7/a5-swap.om+seed/qualification/om2/sec/s5s7/pr.om
db0101e23ea90154be1f42688a82a5fa96c0f994436a7abf76df7ee8b6586b5e seed/qualification/om2/sec/s5s7/v6-slots6.om
610f69b9b45830f926ebb9c4df2d358a366920eaacd3c5ce8f5ec03da6ea8014 seed/qualification/om2/sec/s5s7/v6-fm-e2.om
e1b60bc4c240e03be0c0fbdb630066f295ff7a53b73a380dab52fa8509f498d6 seed/qualification/om2/sec/s5s7/v6-out.om
4034913df2f245dda9527510129e3d442c61a2b431b40a0650d0348f40270339 seed/qualification/om2/sec/s5s7/m6-export2.om
dd88a128445533d88444c7a6a38687413b2dc3462cf361adb1685ccb162f6c0e seed/qualification/om2/sec/s5s7/m6-noexport.om
c579c57cd55d5ae2e9206bac7a278f3ee56bdbff594d0fb46d2c9f2f03603a69 seed/qualification/om2/sec/s5s7/m6-export-layout.om
932ba37772b3f50046908ed558c84bd3752dec4f619dbb8469ab68dc43a4c097 seed/qualification/om2/sec/s5s7/m6-export32.om
e90ac20ccef2c24e7e0c912a7c768610fb2791c393c9ef239b0108d2cee470c0 seed/spirv/demo.unit.om
0419a9380955e1199e9927412cdbde107b081243da2345f0cc77900ce641554e seed/spirv/traps.unit.om
94c38bc25d64c36a7e0cebc1e7fe0cfef51517507322a3f526cb31e1e0553444 seed/wasm/control.unit.om
ebfa2f5c672805ab1cf48f30a94b1bae14767a78a76e637d31e69241ae95748b seed/wasm/integers.unit.om
eb16f331ad57fcae0eba99ddfedc9874120733eaa0ae487e57f26d3beda619d5 seed/wasm/mutant.unit.om
""".strip().splitlines())

# Entryless diagnostic and leak fixtures retain their actual last-function ABI.
REVIEWED_SOURCES.update({
    "8e1b8605e8d77c792a2aaf6adeb2167a38a538d0857792b006eeafd408d78e94":
        "seed/omc/om2/s3/a-kept-diag.om",
    "79d2da33312969e14d9cdefe34b19306cce03471d91add08aa1e0285af22cf9c":
        "seed/qualification/om2/sec/s5s7/a5-kept-diag.om",
    "20773907e95cbc9b1e157be91b099dc1ad13ab6ca79d2966e4259a93a8625eff":
        "seed/qualification/om2/sec/s5s7/a5-swap-diag.om",
    "af6c2faa6087359c0c51bcd5d61d973143f45d4719eb98278370bfb03284a79e":
        "seed/omc/om2/s3/a-exact-diag.om",
    "2702ef2f9bcf2fbadd6c9ced8382a13f1939378c024a5c5706d167d98798be13":
        "seed/qualification/om2/sec/s5s7/a5-exact-diag.om",
    "a7682122ef8f6707e48f8d6cf76459aca4ebab53ab3245dd51b88d8067fad948":
        "seed/qualification/om2/leak/fixtures/pub-outside-store.om",
    "03fba96f85d365cec80d11c1f10400d6f1874c924e3e824458fb812c023f267c":
        "seed/qualification/om2/leak/fixtures/pub-secret.om",
})

# Entryless units added by #971, #972 and #979 (main 474197e4). Reviewed bodies: no main, no host
# words, no exports, so the last function is called with the canonical 1 and 0 scalar vectors; no
# division, view access or trap is reachable from those vectors (fnmode's `trap 13` needs k = 0 after
# a loop that sets k = 3). These are native differentials, not independent result oracles.
REVIEWED_SOURCES.update({
    "32cc3776e0c02d167b50c080f2e447bf14840547d47726f0e76488ed07c0a996": "seed/equiv/bitrev.unit.om",
    "290be2abfa37c0c53c6c66a8b8328dd94f50cdefb4c1d37bda8208fdb1096758": "seed/equiv/clamp.unit.om",
    "f97b8af25a257a856f2c9fb5dfd309e301932881f84dfb06c9c3ee5c509360f1": "seed/equiv/hexv.unit.om",
    "a8691945d7e38430f142e93ba9f0c48effa371a8df474ab814b3121b76b4065b": "seed/equiv/pop16.unit.om",
    "bd257fa4aeedfcea129304ab592ce13346b93a5caf318a9475a1689f1b045019": "seed/equiv/rot.unit.om",
    "3862a9f972a1afd201957b7700cf9dd44b2e289c9c42be140f1af45b2317a401": "seed/equiv/wide.unit.om",
    "00712f9f6ca778f37584675897606accc408be659fe515adfc711007ac73b281": "seed/nki/ops.unit.om",
    "4324ec425080d5641a102ebba3916a6ee40e3185be540593c0d5b1bf71d86f8a": "seed/nki/vecadd.unit.om",
    "c7d149b29f511cea0cdb0bdd6e97861bbd5d08ba5cf8fb2110046dfb38977f65": "seed/omc/om2/hex/fnmode-dec.om",
    "e1bd7495111e1b28fba8f06ddd66bac23e175229f392b88ad1b825b748c72439": "seed/omc/om2/hex/fnmode-hex.om",
})

REVIEWED_SOURCES['dbf002dd6aa36585632b9d01d98ac3f0bd2fa0f0475082940de53638847994bb'] = 'om/sha256/sha256-om2.om'
# Deliberate CPU exhaustion (#970): run only under the cpu-limit driver in plan_case.
REVIEWED_SOURCES['d4801acf04a44467bac5e1bedfc502b20afe009d8065079ae5880e987b0899a7'] = 'seed/qualification/security673/pure-busy.om'

RECORDED_INPUTS = {'5ff3b6cddb9f66971ecb7b58da2d2f07e3df9e7176dd1ba50a2c3a36f2447e81': {'path': 'seed/rung4/tests/fm1.inputs', 'sha256': '3b521473ebf69009db11ed5af2e393c452f65a479796c3e872de4c876301abb6', 'values': [(7, 2), (-7, 2), (7, -2), (-7, -2), (5, 0), (-9223372036854775808, -1), (-9223372036854775808, 1), (9223372036854775807, -1), (0, 5), (-9223372036854775807, -1), (123456789, -1000)]}, '02b9d845454e7379d4d55f3c78af7e9ddd9f87f15d196765b338ee5ae6416965': {'path': 'seed/rung4/tests/fm2.inputs', 'sha256': '18342c0716c373eb7d01da95e11a4bb0f88148bf601817d24c79e5118394ae89', 'values': [(7, 2), (-7, 2), (-2147483648, -1), (-2147483648, 1), (2147483647, -1), (9, 0), (-9, 4), (100, -7)]}, '3323f23ff905d4915ced5c83d97d772c5b4e4e1347ba738ae3752ef74d99cc0e': {'path': 'seed/rung4/tests/fm3.inputs', 'sha256': '1f2c517d98c0c1a2f6cc18965d758dfa52a1099e8209dc0ad0c2692d2536c99f', 'values': [(1, 62), (1, 63), (-1, 63), (3, 62), (-5, 2), (5, 64), (0, 63), (-4611686018427387904, 1), (-4611686018427387905, 1), (7, 0)]}, '06149063fb825604227f54d226fbce04afe48b755f6ccbe2690ef35afe1c5a65': {'path': 'seed/rung4/tests/fm4.inputs', 'sha256': 'f44900dbe6897acaecff1fc0f585902f404197069338a90cef18ecfe317a3bce', 'values': [(1, 30), (1, 31), (-1, 31), (3, 29), (-5, 2), (5, 32), (536870911, 2), (536870912, 2), (-536870912, 2), (-536870913, 2)]}, '3b0acd8ea8b0a46d3131378ae447ffd337e15e0b92da1c45ba2c1a2383dc4b54': {'path': 'seed/rung4/tests/fm5.inputs', 'sha256': 'c79c7bf9df3333862da681fefcc2b6fdab887a25740cdb76594a9bdd1ae9c0e8', 'values': [(3, 4), (0, 0), (1, 1), (4294967296, 1), (18446744073709551615, 2), (1000, 2000)]}, 'cf0a8200ec4341d1c4634bceb6bab5e7cca15bae510ca4d6f41982fe1ad70a9c': {'path': 'seed/rung4/tests/fm6.inputs', 'sha256': '60983787c391a667e6b4198c75c09aa5f75681e882d112334562987fdf280a55', 'values': [(0,), (1,), (5,), (100,), (1000,), (1000000,), (1000001,)]}, 'ccbdc5fd0b1005df6bbd17dee7d8679f7d5fbc0355ca1cca9a5c4e32cc1c352a': {'path': 'seed/rung4/tests/fm7.inputs', 'sha256': 'df36cfb0562e591237602b1fe12f5c5403c3af941ca6aece8faaa77c2c7c2aa5', 'values': [(1, 2), (0, 0), (4294967295, 1), (100, 200)]}, '1cb48989143498ec44e1cbc9ed9bfd11082b9426d937c1ebae5ac02a2c171f93': {'path': 'seed/rung4/tests/fm8.inputs', 'sha256': 'cecbd6b5657af7a9f8bf119aa34bfc11e0ac300fca584435d63f86f60e2d88fb', 'values': [(0,), (1,), (2,), (3,), (4,), (5,), (4294967295,)]}, 'e9548779048e3d97b48f7d9904fa832aa47c8a6158c9355d2b88eefa50c87589': {'path': 'seed/rung4/tests/fm9.inputs', 'sha256': 'dcb7b50d378bbd78226e3c4423cb9b353f292d65dbb66124546be0c92ea0c102', 'values': [(100, 7), (-100, 7), (-32768, -1), (-32768, 2), (32767, 0), (-7, -7)]}, 'b5624972ed3b4394027eca96d94b670f7a8846af51d3083940e63c079c8649e4': {'path': 'tools/exp445/arms/examples/clamp_sub/inputs', 'sha256': 'fdd78820e3fe41bb5c427b72044ae709eabc4b4c1ed9d99493052135f3358da8', 'values': [(5, 3), (3, 5), (0, 0), (7, 7), (4294967295, 0), (4294967295, 1), (1, 4294967295), (100, 99)], 'expected': ['2', '0', '0', '0', '4294967295', '4294967294', '0', '1'], 'expected_path': 'tools/exp445/arms/examples/clamp_sub/expected', 'expected_sha256': '2d8f2492e08fb5a4408d11dfb94011723dcec64fb9e20d406044b6c0203b954d'}, '0bfe80ea229071421efd1418bbf09db219acf1c303d4c8a80025d1033744759d': {'path': 'tools/exp445/arms/examples/count_above/inputs', 'sha256': '188675879873c3484e770bd793be8e5395d1b96575e0031f710edfc610918b3a', 'values': [(1, 2, 3, 4, 2), (0, 0, 0, 0, 0), (5, 5, 5, 5, 4), (-1, -2, -3, -4, -5), (2147483647, -2147483648, 0, 1, 0), (10, 20, 30, 40, 25), (7, 8, 9, 10, 10), (1, 1, 1, 1, -2147483648)], 'expected': ['2', '0', '4', '4', '2', '2', '0', '4'], 'expected_path': 'tools/exp445/arms/examples/count_above/expected', 'expected_sha256': 'ff0be81ecc695f3046237b3157c420f58c65e6439720489e0aa948583452e892'}, '14439646d9db8ec1c40b7646b3ec177d21ce8178a1e7c6c001134339cd8fab76': {'path': 'tools/exp445/arms/examples/count_digits/inputs', 'sha256': '6383af745b837f467812b692ddad3de333f6f6799dfaee7c210628ef2cd4f534', 'values': [(0,), (9,), (10,), (99,), (100,), (12345,), (18446744073709551615,), (10000000000000000000,), (9999999999999999999,)], 'expected': ['1', '1', '2', '2', '3', '5', '20', '20', '19'], 'expected_path': 'tools/exp445/arms/examples/count_digits/expected', 'expected_sha256': '66e82cbe8e78af1ef919a48a526efb2b48ce024c5e8322e89b828854f30a90a7'}, '83b4aad4f657f489ac7a7b9eee847118bc8d37c452a83efd3d5135af4f2e69dd': {'path': 'tools/exp445/arms/examples/cube_xor_pair/inputs', 'sha256': 'fe1cac4cf9dd56c91bc70907af7a0a21e9cee1990617bed1c5dc3b03a1a4aae6', 'values': [(0,), (1,), (2,), (7,), (13,), (14,), (15,), (16,), (200,), (255,)], 'expected': ['1', '9', '19', '855', '557', '1943', 'TRAP 3', 'TRAP 3', 'TRAP 3', 'TRAP 3'], 'expected_path': 'tools/exp445/arms/examples/cube_xor_pair/expected', 'expected_sha256': '06a16d3f2fdc6dc5bc5b7d3bcc4af1f18c2e18c177c3d2df1a6f6ce0b4a9b9ef'}, '1974598d956017c608019ebd9a8b5af42dc34c5eae20809b47940337c14f2349': {'path': 'tools/exp445/arms/examples/dot3/inputs', 'sha256': 'bca0c3df5f4ba03ffd6fd50d74654b51a34120e13266ab3100f7ebd8ffb95b70', 'values': [(1, 2, 3, 4, 5, 6), (0, 0, 0, 0, 0, 0), (-32768, -32768, -32768, -32768, -32768, -32768), (32767, 32767, 32767, 32767, 32767, 32767), (-32768, 32767, 1, 32767, -32768, -1), (1, -1, 1, 1, 1, -1), (100, 200, 300, -3, -2, -1), (5, 0, 0, 7, 9, 11)], 'expected': ['32', '0', '3221225472', '3221028867', '-2147418113', '-1', '-1000', '35'], 'expected_path': 'tools/exp445/arms/examples/dot3/expected', 'expected_sha256': 'bb0f1153516a1e224305057797fa99a392bd137ba2ad1936fd775c9061054cbc'}, '95a41a420eb97cac77023a066936cc4c9673e3224489d118834edd076232ab59': {'path': 'tools/exp445/arms/examples/halve_or_dec_steps/inputs', 'sha256': '81203c47f140c7767b6d831e0cd4c68d0995d674fc5b19b22a0e27b0c3f2b27f', 'values': [(0,), (1,), (2,), (3,), (7,), (8,), (15,), (1000,), (4294967295,), (2147483648,)], 'expected': ['0', '0', '1', '2', '4', '3', '6', '14', '62', '31'], 'expected_path': 'tools/exp445/arms/examples/halve_or_dec_steps/expected', 'expected_sha256': '03cadfc2b73c4cb64ec9878acdb24f73ce82c863934c4de7b74ad3da1296157a'}, 'bd88c1daeb2c9f2bde005d46172b7b7e0b4ed2c95081ea106dd62f8caafb16a0': {'path': 'tools/exp445/arms/examples/has_digit/inputs', 'sha256': '5e3f2fc4b3b9b962f883861183906f399596eee331b65d71195dba4d2a4b2088', 'values': [(0, 0), (0, 1), (12345, 3), (12345, 6), (4294967295, 9), (4294967295, 8), (1000, 0), (7, 7), (5, 10), (5, 255)], 'expected': ['1', '0', '1', '0', '1', '0', '1', '1', '0', '0'], 'expected_path': 'tools/exp445/arms/examples/has_digit/expected', 'expected_sha256': 'a848801f579c98ca8fc9e6a90ed5bfe8659cc9b7a85e2afc471e1c44343eb21f'}, 'fe9f95b097d0137a3ddb4a62246c5e6875fbc19aded228c2f23c4679b84c2ccf': {'path': 'tools/exp445/arms/examples/hash_step/inputs', 'sha256': '129fd09a51d0ecd85174e69c98f0fd700340be2335d35241dee90ec1de459e95', 'values': [(0, 0), (1, 1), (0, 255), (4294967295, 255), (138547332, 1), (123456789, 65), (4294967295, 0), (2, 0)], 'expected': ['0', '32', '255', '224', '4294967293', '3827160524', '4294967265', '62'], 'expected_path': 'tools/exp445/arms/examples/hash_step/expected', 'expected_sha256': '867df00b00610ee5374b95425f9c199cc078fa9e98c5d32478ee9bad993ca41f'}, '4335f62c4d839f7a7d0addd992208b6107249d7620d840080a81076b3800bc26': {'path': 'tools/exp445/arms/examples/narrow_sum/inputs', 'sha256': '3257617c170c938ee5bbd4741016a4f6eb77de48a280944475ba3ab695bc83ca', 'values': [(1, 2), (100, 27), (100, 28), (-100, -28), (-100, -29), (32767, 1), (-32768, -1), (-128, 0), (200, -100), (0, 0)], 'expected': ['3', '127', 'TRAP 5', '-128', 'TRAP 5', 'TRAP 1', 'TRAP 1', '-128', '100', '0'], 'expected_path': 'tools/exp445/arms/examples/narrow_sum/expected', 'expected_sha256': '3aa8e311d290b7f34575a0f23d4657abecadcfe73d106853cf4abfe4cf5140ef'}, '23026d2191616675acd10e798835bcb290ecd259c1e17935019bb2f97021d9fd': {'path': 'tools/exp445/arms/examples/nibble_mirror/inputs', 'sha256': 'dee5944c3255af19088c8b7938aef0a47fa8a65df83786afbeefe265d28c982b', 'values': [(0,), (4660,), (4385,), (43690,), (65535,), (1,), (4096,), (37129,), (23205,), (17185,)], 'expected': ['1', '0', '0', '1', '1', '0', '0', '0', '1', '0'], 'expected_path': 'tools/exp445/arms/examples/nibble_mirror/expected', 'expected_sha256': '36a8593ee441c6ac3381a58d2dc09a26373d3c07187d08df138c4e22f1e1d583'}, 'a55747b9894b8a8c07f33e6678b8f5482eee35096c97ff686be7ec7b7784d9c1': {'path': 'tools/exp445/arms/examples/power_mod/inputs', 'sha256': '5c16767626808d1bbfce04c745237d43d694b31cd2acdc49c3b5c9491805df34', 'values': [(2, 10, 1000), (3, 0, 7), (0, 0, 5), (5, 3, 1), (4294967295, 4294967295, 4294967291), (7, 13, 0), (0, 0, 0), (123456789, 987654321, 1000000007), (2, 31, 4294967295), (10, 2, 0)], 'expected': ['24', '1', '1', '0', '1024', 'TRAP 2', 'TRAP 2', '652541198', '2147483648', 'TRAP 2'], 'expected_path': 'tools/exp445/arms/examples/power_mod/expected', 'expected_sha256': '47b9c5eb1d9d6b0729ed484e8bea8b32aae12c83658870e69ea2d14fe0da2825'}, 'd8196e4bdfe690afeed43f7a52aefbb76f13ca42a6caa51fc61e70a585165213': {'path': 'tools/exp445/arms/examples/rotl8/inputs', 'sha256': 'ec5b74aca087a19be46cb8530bc4cf3a03914e87890ceabcb7250368b1c51ea2', 'values': [(1, 1), (128, 1), (1, 0), (1, 8), (165, 4), (15, 3), (255, 7), (6, 9), (129, 255), (0, 5)], 'expected': ['2', '1', '1', '1', '90', '120', '255', '12', '192', '0'], 'expected_path': 'tools/exp445/arms/examples/rotl8/expected', 'expected_sha256': 'cb04e913e46bcbc9ea2c9831080733e166396faf8edf6a510e2b4f53eb210534'}, '9acda5f3fd9ddc1dc01923fa76a30a4667f2d0e8d460ba2a60c5d6d5fc27807c': {'path': 'tools/exp445/arms/examples/sum_range/inputs', 'sha256': '40ea4e80ec323db346cdaa019887c692792b29802ede84ff3a4cc79199fb067c', 'values': [(1, 10), (5, 5), (6, 5), (-3, 3), (-100000, 100000), (0, 100000), (-10, -1), (100000, -100000)], 'expected': ['55', '5', '0', '0', '0', '5000050000', '-55', '0'], 'expected_path': 'tools/exp445/arms/examples/sum_range/expected', 'expected_sha256': '07eed9ef40f75e42d50f6cdf9b3ba780cb6b0998c47439e0b53f36c33145fb4d'}, '17b76ece94d8d691b3339b4d3f58443814c81d897a6e3f746981263674d32257': {'path': 'tools/exp445/arms/examples/sum_squares3/inputs', 'sha256': '37b9505a9528cda2f2598c72c60528812adecba24d9f859875f1482e4ee7efac', 'values': [(1, 2, 3), (0, 0, 0), (-3, 4, 0), (181, 0, 0), (182, 0, 0), (100, 100, 100), (-100, -100, -50), (-32768, 0, 0), (128, 128, 0), (128, 127, 0)], 'expected': ['14', '0', '25', '32761', 'TRAP 1', '30000', '22500', 'TRAP 1', 'TRAP 1', '32513'], 'expected_path': 'tools/exp445/arms/examples/sum_squares3/expected', 'expected_sha256': '04d8dc4400f7096d6c95956a54712a07073aabef624c97296a15f9ca493316bc'}, '8ecfa30de1ec78883a3c4c40fc5a11aeb2646a38df403a8bf61da47d99017e1d': {'path': 'tools/exp445/arms/examples/tri_kind/inputs', 'sha256': '12deb164b789ad554af56d30c59339772ab16022f405a9be1d026074c442acbb', 'values': [(3, 3, 3), (3, 4, 5), (2, 2, 3), (1, 2, 3), (0, 5, 5), (65535, 65535, 65535), (65535, 65535, 1), (1, 65535, 65535), (5, 1, 1), (10, 6, 5)], 'expected': ['1', '3', '2', '0', '0', '1', '2', '2', '0', '3'], 'expected_path': 'tools/exp445/arms/examples/tri_kind/expected', 'expected_sha256': 'afec9abbb1c91597ce7a1c73a7e254dd10e83cd16d435a4f2475037bc969a81d'}}

# Generalization corpus (#979): 49 entryless reference solutions, each with hidden inputs and
# expected values computed independently from the prompt (tests/one_layer/generalization/corpus/
# oracle.c). Reviewed bodies: u32 arithmetic, local loops and arrays only; no main, no host words, no
# exports, and the last function is the requirement's entry. Every recorded input is called and must
# give its expected value (or its TRAP code). The aggregate pin covers the bytes of every solution,
# inputs and expected file: any change leaves the whole corpus unreviewed (NOT_RUN), never PASS.
CORPUS_TASKS = "tests/one_layer/generalization/corpus/tasks"
CORPUS_TASKS_SHA256 = "bec24e01a74a691e2f3bcc4533adfb8dd1045b1328d03d38102ed357e730dc8b"


def _review_corpus(root):
    base = root / CORPUS_TASKS
    if not base.is_dir():
        return
    blob, entries = hashlib.sha256(), {}
    for name in sorted(p.name for p in base.iterdir() if p.is_dir()):
        files = {kind: (base / name / kind).read_bytes() for kind in ("solution.omegese", "inputs", "expected")}
        for kind, data in files.items():
            blob.update(f"{name}/{kind} {hashlib.sha256(data).hexdigest()}\n".encode())
        entries[hashlib.sha256(files["solution.omegese"]).hexdigest()] = (f"{CORPUS_TASKS}/{name}", files)
    if blob.hexdigest() != CORPUS_TASKS_SHA256:
        return
    for sha, (rel, files) in entries.items():
        REVIEWED_SOURCES[sha] = rel + "/solution.omegese"
        RECORDED_INPUTS[sha] = {
            "path": rel + "/inputs", "sha256": hashlib.sha256(files["inputs"]).hexdigest(),
            "values": [tuple(int(word) for word in line.split()) for line in files["inputs"].decode().splitlines() if line.strip()],
            "expected_path": rel + "/expected", "expected_sha256": hashlib.sha256(files["expected"]).hexdigest(),
            "expected": [line for line in files["expected"].decode().splitlines() if line]}


_review_corpus(Path(__file__).resolve().parents[2])

# Entryless pure units reviewed 2026-10-31 (#1001): arithmetic, comparisons, views and private calls
# only; no main, no host words. The bare callers run the same private-ABI vectors natively on AArch64
# and under qemu-x86_64 and require identical results.
REVIEWED_SOURCES.update({
    '385a94645ecfa33d95b34a06ee84b645c81e90f30cf54737b09255ef68a96bfd': 'seed/qualification/security673/relational/address.om',
    'edcd256af38d73e0f3b694970a15ebdcca60bc3bc4eafe8cfced8efcca537dd6': 'seed/qualification/security673/relational/branch.om',
    '77894dc009c17aaf5f252679fe888bf658d4cdba29a41a976f186f6b04221590': 'seed/qualification/security673/relational/compare32.om',
    '7d1893482efd5cc9ebd2831a46b2d0277b54da4f5e75133842640e1b6cc13f75': 'seed/qualification/security673/relational/dividend.om',
    '518317ae9b8a5ce2709ab6c3d241cb09b885a1f3eb809c2fc2a7df59f6d38af1': 'seed/qualification/security673/relational/division.om',
    '0df468f548a0931718ad1219a24f1f9eec7ff96ad8b5eeb5db629c590e814075': 'seed/qualification/security673/relational/early.om',
    '8fbd4b6c45395ab22e74af94acc18f9fde1806c83484880bda8a0acc9d11d29e': 'seed/qualification/security673/relational/error.om',
    '81c82a28bd7e77419fb286e45b12901b3d5fc9e4a2d553c5fb628a7842a08684': 'seed/qualification/security673/relational/helper.om',
    'db1a4df108de0b547ba6ede9cc581de0123494c8bd9bd8c8f342bf144b8369bd': 'seed/qualification/security673/relational/public-address.om',
    '015995067ab75ee67d2c8010d5cc53894946933e22a3211d78afcfad879f1048': 'seed/qualification/security673/relational/public-branch.om',
    'a3330aae7ea2dc45133bd61bd22d92b4b1d5f0f6278009f9e4da89c3168a81fd': 'seed/stablehlo/refuse.unit.om',
    '8cf6c20c1e5787173414b9773b5e5d66bb78e79780f78d3bda8bfb17e67fc539': 'seed/stablehlo/vecadd.unit.om',
    'f7a069436534baeb9e21fa8a2c70043ebb4f03d041ebac9707f90c142a9b11a8': 'seed/tensix/mix.unit.om',
    '20d6df5fe5a5ae5628bf4f217e5125eed8387313271ca45274c4bfeb2f42fcc7': 'seed/tensix/vecadd.unit.om',
})
# division.om and error.om trap on one of the two default vectors; their results come from
# relational/qualification.cases rows division-one/-zero (7, exit 102) and error-zero/-one (7, exit 106).
RELATIONAL_CASES = "seed/qualification/security673/relational/qualification.cases"
RELATIONAL_CASES_SHA256 = "e6b28875d4a148c389df3fb39f317e9207d00894430a39a814c586e79bf86b65"
RECORDED_INPUTS['518317ae9b8a5ce2709ab6c3d241cb09b885a1f3eb809c2fc2a7df59f6d38af1'] = {
    'path': RELATIONAL_CASES, 'sha256': RELATIONAL_CASES_SHA256, 'values': [(1,), (0,)],
    'expected_path': RELATIONAL_CASES, 'expected_sha256': RELATIONAL_CASES_SHA256, 'expected': ['7', 'TRAP 2']}
RECORDED_INPUTS['8fbd4b6c45395ab22e74af94acc18f9fde1806c83484880bda8a0acc9d11d29e'] = {
    'path': RELATIONAL_CASES, 'sha256': RELATIONAL_CASES_SHA256, 'values': [(1,), (0,)],
    'expected_path': RELATIONAL_CASES, 'expected_sha256': RELATIONAL_CASES_SHA256, 'expected': ['TRAP 6', '7']}

DIRECTIVE_REVIEWS = {'seed/qualification/om2/leak/candidate.cases:63:sha256-om2-scan': {'reason': 'OM-2 SHA artifact pending independent qualification', 'requirement': 'Runtime code for the OM-2 SHA unit remains unavailable to this independent lane.', 'manifest_sha256': '2574b8854ea5fb3a7ce617a93c44ef15a6fb5c2c078dc74114b4908ffb759e92'}, 'seed/qualification/om2/leak/leak.cases:83:sha256-om2-scan': {'reason': 'OM-2 SHA artifact pending independent qualification', 'requirement': 'Runtime code for the OM-2 SHA unit remains unavailable to this independent lane.', 'manifest_sha256': '5185e00235d602da896e29eef03e71edf39c1fbcb138a616373bb41436d67883'}, 'seed/qualification/om2/sec/s0s2/candidate.cases:103:s1-d-mid-short-line': {'reason': "E6 Q2 makes the LF the span, but whether the record's line= (and expect=) is the LF's line or the human line N (one later in OM-1) is not fixed; the tok case checks the rest", 'requirement': 'E4 col/span, E6 Q2', 'manifest_sha256': '507a23edd61db8c50f671ab8c2ba4a7550c71074957ce400e9a4d4885bc6507a'}, 'seed/qualification/om2/sec/s0s2/candidate.cases:315:s2-fmt-bad-trailblank-line': {'reason': 'E6 (a) makes trailing blank lines error 6 but does not say which line the human line names (first or last blank line)', 'requirement': 'E6 a, Q3', 'manifest_sha256': '507a23edd61db8c50f671ab8c2ba4a7550c71074957ce400e9a4d4885bc6507a'}, 'seed/qualification/om2/sec/s3s4/candidate.cases:158:inline-calls-stay-calls': {'reason': 'line 98 (calls to non-inline functions stay calls) is not observable through run/emit; needs a disassembly oracle', 'requirement': 'inline-rec-mutual-om1 run seed/qualification/om2/sec/s3s4/inline-rec-mutual-om1.om+seed/qualification/om2/sec/s3s4/lib-pu.om - file:seed/qualification/om2/oracle/s3s4/exp/inline-rec.out - 0', 'manifest_sha256': '3be3558e2cc1acb19b73ed0760cfd1f17dc773717aa96df895f6b53431e572ec'}, 'seed/qualification/om2/sec/s3s4/candidate.cases:240:slots-order': {'reason': 'line 100-101 slot order (pointer then length) is observable only through the bridge (L837), not through omc output', 'requirement': 'slots-7p-om1 emit seed/qualification/om2/sec/s3s4/slots-7p-om1.om - err:5:9 9', 'manifest_sha256': '3be3558e2cc1acb19b73ed0760cfd1f17dc773717aa96df895f6b53431e572ec'}, 'seed/qualification/om2/sec/s3s4/candidate.cases:260:program-site-table': {'reason': 'exact site tables of program-mode units depend on which OM-1 trap stubs and checks exist (stack, write slices); not fixed by the contract or E7', 'requirement': 'fold-trap-diag-same same seed/qualification/om2/sec/s3s4/fold-trap-diag.om+seed/qualification/om2/sec/s3s4/lib-pu.om seed/qualification/om2/sec/s3s4/fold-trap.om+seed/qualification/om2/sec/s3s4/lib-pu.om', 'manifest_sha256': '3be3558e2cc1acb19b73ed0760cfd1f17dc773717aa96df895f6b53431e572ec'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:142:v6-part-golden-literal': {'reason': 'unreachable without a 2^64-1-element view; bridge side is abi-golden-wrap (status 32)', 'requirement': 'coordinator decision: the literal #748 golden (len 2^64-1, o 2^64-2, k 4) in a unit', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:186:abi-vsum-ok': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 185: unsized ulong view (ptr, len) baseline: status 0, sum 6', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:188:abi-bcnt-ok': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 185: uchar view len 64: status 0, result 64', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:190:abi-null-len1': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 188: len > 0 with NULL -> status 32, result untouched', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:192:abi-misaligned': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 189: ulong view not aligned to 8 -> 32', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:194:abi-size-overflow': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 190: len * 8 overflows 64 bits (len 2^61) -> 32', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:196:abi-golden-wrap': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 191 + coordinator golden: uchar len 2^64-1 -> ptr + len wraps -> 32', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:198:abi-wrap-high': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 191: ptr 2^64-8, len 16 wraps -> 32', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:200:abi-null-len0': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 194: len 0 with NULL is an empty view -> status 0, result 0', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:202:abi-null-len0-ulong': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 194: ulong view len 0 NULL -> sum 0', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:204:abi-bogus-len0': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 194: len 0 with any pointer (1) -> empty view', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:206:abi-edit-overlap': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 192: edit [0,8) overlaps view [4,12) -> 32, nothing written', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:208:abi-edit-adjacent': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 192-193: half-open adjacent ranges do not overlap -> status 0', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:210:abi-edit-empty': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 193: an empty edit range overlaps nothing -> status 0', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:212:abi-edit-result': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 192: edit range overlapping the result buffer -> 32', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:214:abi-out-overlap': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 196-198: out overlapping its input gets memmove semantics (4,3,2,1)', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:216:abi-out-trap': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 197-198: nothing written to out (or result) on a trap; status 9', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:218:abi-out-bounds': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 170, 198: trap 3 inside -> status 3, out untouched', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:220:abi-layout': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 156-158: layout = one pointer; lo little-endian, hi big-endian bytes', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:222:abi-layout-null': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 181 (v1 checks kept): NULL layout pointer -> 32 (README A12)', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:224:abi-null-result': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 181 (v1 checks kept): NULL result pointer -> 32', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:226:abi-reentry': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 195: call from a signal handler during a call -> 33', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:228:abi-two-units': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 207-210: two OM-2 units and one OM-1 unit linked into one binary, called interleaved', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:230:abi-ident-abiv': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'erratum E1: omega2_ident_abiv() holds sha256(abiv.om), the omc pin, OM-2 (README A11)', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:232:abi-ident-abiw': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'erratum E1: omega2_ident_abiw() holds sha256(abiw.om), the omc pin, OM-2', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:234:abi-ident-distinct': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'erratum E1: identities are per unit', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:236:abi-v1-absent': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 182-184 + E1: no omega_vsum symbol (nm -D), v1link.c fails to link', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:238:abi-v1-kept': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'erratum E1: the OM-1 unit abi1 exports omega_madd1 and no omega2_ident_abi1', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:240:abi-units-stale-source': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 209 + E1: units.sh --check after a one-byte change to a unit source refuses (exit 1)', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:242:abi-units-stale-omc': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 209: units.sh --check with a different omc pin refuses', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:244:abi-units-stale-profile': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 209 + E1: units.sh --check with the profile recorded OM-1 for an OM-2 unit refuses', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:246:abi-misaligned-len0': {'reason': 'needs the ABI v2 generator (omega-bridge, AbiLane L837) to bind sec/s5s7/abi/*.om; oracle/s5s7/abi/driver.c case', 'requirement': 'contract 189 vs 194 conflict: ulong view, len 0, ptr 1 (README Q5); expectation not asserted', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:250:sha-empty': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 214: init, final = python sha256("")', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:252:sha-abc': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 214: abc', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:254:sha-split2': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 214-215: every two-way split of seq:1000', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:256:sha-split3-zero': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 214: length-0 updates between three-way splits', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:258:sha-bytewise': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 214-215: one-byte updates', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:260:sha-update-null0': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 214, 194: update(NULL, 0)', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:262:sha-hash-overlap0': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 215 + E3: sha256_hash(buf, 64, buf)', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:264:sha-hash-overlap16': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 215 + E3: digest at buf+16 overlapping data', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:266:sha-hash-overlap-before': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 215 + E3: digest before and overlapping data', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:268:sha-post-final': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 219-222: init, update, final after a final == fresh', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:270:sha-no-init-safe': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 219-220: update/final without init returns (memory safety only)', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:272:sha-hash-eq-stream': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 223: hash(n) == init, update(n), final == python', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:274:sha-ctx-copy': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'erratum E3: byte copy of a context after k updates is equivalent', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:276:sha-big-1': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 214: one update of 2^32+3 bytes', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:278:sha-big-split': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 214: updates of 2^32-1 and 4 bytes', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/candidate.cases:280:sha-big-hash': {'reason': 'needs the OM-2 SHA unit and its bridge-generated header (ShaLane L838); oracle/s5s7/sha/sha_driver.c case', 'requirement': 'contract 214, 223: hash of 2^32+3 bytes', 'manifest_sha256': 'd7e3c1655c1b20c86581725cd52c133867367896ceb17a519995c8733cb72ce4'}, 'seed/qualification/om2/sec/s5s7/mutant-side.cases:41:mut-e-ident': {'reason': 'needs the ABI v2 generator; kill described in README section Mutant kill specs (e)', 'requirement': '(e) stale generation/profile identity accepted (contract 207-209, erratum E1): ABI level.', 'manifest_sha256': '20c198884ccb1ecc6393a1b435339a702893c9aebf13cd3968b78cbd4e98f8d5'}, 'seed/qualification/om2/sec/s5s7/mutant-side.cases:43:mut-f-abi-len': {'reason': 'needs the ABI v2 generator; kill described in README section Mutant kill specs (f)', 'requirement': '(f) bad ABI length accepted (contract 187-193): ABI level, driver abi-null-len1 / abi-golden-wrap.', 'manifest_sha256': '20c198884ccb1ecc6393a1b435339a702893c9aebf13cd3968b78cbd4e98f8d5'}}

WIDTH = {"bool":1,"uchar":1,"byte":1,"ushort":2,"short":2,
         "uint":4,"int":4,"ulong":8,"long":8,
         "u8":1,"i8":1,"u16":2,"i16":2,"u32":4,"i32":4,"u64":8,"i64":8}
MAIN = re.compile(rb"(?m)^[ \t]*(?:export[ \t]+)?fn[ \t]+main(?:[ \t]|$)")
FUNCTION = re.compile(rb"(?m)^((?:export )?fn [^\n]+)")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def _pinned(root, rel, sha):
    path = Path(root) / rel
    if path.is_symlink() or not path.is_file():
        raise ValueError("required regular oracle input is absent: " + rel)
    data = path.read_bytes()
    if digest(data) != sha:
        raise ValueError("reviewed oracle input changed: " + rel)
    return data


def _signature(line, source):
    words = line.decode().split()
    exported = words[0] == "export"
    if exported:
        words = words[1:]
    name = words[1]
    to = words.index("to")
    parameters = []
    p = 2
    layouts = {n.decode():int(z) for n,z in re.findall(rb"(?m)^layout (\w+) ([0-9]+)$",source)}
    while p < to:
        if words[p] == "fresh":
            p += 1
        param = words[p]
        p += 1
        kind = words[p]
        p += 1
        if kind in ("view", "edit", "out"):
            typ = words[p]
            p += 1
            if typ in layouts:
                parameters.append((param,kind,"uchar",layouts[typ],False))
            else:
                count = words[p]
                p += 1
                parameters.append((param,kind,typ,4 if count == "*" else int(count),count == "*"))
        else:
            if kind not in WIDTH:
                raise ValueError("unreviewed scalar parameter type: " + kind)
            parameters.append((param,"scalar",kind,1,False))
    return name,parameters,words[to+1],exported


def bare_calls(root, rel, source):
    """Concrete caller specs for an exact reviewed source, never arbitrary code.

    Existing fm*.inputs / example inputs are retained in full (including traps).
    Remaining fixture calls use two explicitly described shapes: nonzero small
    canonical scalars and distinct arrays, then zero scalar boundaries. These are
    differential inputs, NOT independent semantic predictions. Exports are all
    called, in source order. The non-export last-function rule is preserved.
    """
    sha = digest(source)
    if sha == CURRENT_SHA256:
        return sha256_calls(root,source)
    if sha == digest(GOLDEN_PART_SOURCE):
        authority = "seed/qualification/om2/sec/s5s7/v6-part-wrap.om"
        _pinned(root,authority,"d0326c08e6ea38e6b2f2f5bb982316ad18c6480c148ca47d670848b9ad6a6d67")
        arrays=[{"name":"real-one-byte-storage","size":1,"data":b"\0","observe":False}]
        evidence={"scope":"invalid-view injection, not a constructible full-span array or public bridge call",
                  "authority":authority,"pointer":"real one-byte array; no unit branch reads any element",
                  "provenance":"original 2^64-1-element view is physically impossible; public bridge refusal remains abi-golden-wrap"}
        return [
            {"name":"golden-forged-length-part-trap","entry":0,
             "slots":[{"array":0},18446744073709551615],"arrays":arrays,"result":"ulong",
             "expected_exit":103,"expected_stdout":b"","evidence":dict(evidence)},
            {"name":"golden-valid-empty-part-control","entry":1,
             "slots":[{"array":0},1],"arrays":arrays,"result":"ulong",
             "expected_exit":0,"expected_stdout":bytes(8),"evidence":dict(evidence)},
        ]
    if sha not in REVIEWED_SOURCES or MAIN.search(source):
        return []
    functions = FUNCTION.findall(source)
    exported = [f for f in functions if f.startswith(b"export ")]
    functions = exported or functions[-1:]
    recorded = RECORDED_INPUTS.get(sha)
    if recorded:
        _pinned(root, recorded["path"], recorded["sha256"])
        if "expected_path" in recorded:
            _pinned(root, recorded["expected_path"], recorded["expected_sha256"])
    result = []
    for entry,line in enumerate(functions):
        name,params,returns,_ = _signature(line,source)
        values = recorded["values"] if recorded else ([None,None,None] if b"c = lt n 9\n" in source else [None,None])
        for index,vector in enumerate(values):
            slots,arrays = [],[]
            scalar_index = 0
            for position,(param,kind,typ,count,unsized) in enumerate(params):
                if kind == "scalar":
                    value = vector[scalar_index] if vector is not None else (1 if index == 0 else 0)
                    scalar_index += 1
                    # Function-mode stateful hash APIs: real finalized one-shot calls.
                    if name == "omb3" and param == "fin": value = 1
                    if name == "omsha" and param == "op": value = 3
                    if name in ("omb3","omsha") and param == "n": value = 3 if index == 0 else 0
                    if recorded is None and index in (0,2) and param=="n" and b"c = lt n 9\n" in source:
                        value = 9  # original flags fixture: reach the branch on loaded-data flags
                    slots.append(value & 0xffffffffffffffff)
                else:
                    width = WIDTH[typ]
                    if unsized:
                        count = 4 + position if index == 0 else 0
                        # rev reads four bytes, so the zero-length arm is the bounds negative control.
                    size = width * count
                    if size > 8*1024*1024:
                        raise ValueError("large external buffer needs an explicit reviewed custom caller")
                    data = b"".join((0 if kind == "out" or name in ("omb3","omsha") and param in ("st","c","w","o") else ((i+position+1)&127)).to_bytes(width,"little") for i in range(count))
                    if recorded is None and name not in ("omb3","omsha"):
                        if index==1:
                            data = bytes(size)
                        elif kind!="out":
                            value = bytearray(data)
                            if param=="a" and b"if eq get a 0 7\n" in source:
                                value[0] = 7  # original data-branch: true versus zero false
                            if param=="a" and b"if gt h a 9\n" in source:
                                value[3] = 10  # original call-data: callee's loaded byte exceeds 9
                            if param=="a" and b"ret ulong get t get a 0\n" in source:
                                value[0] = 255  # original data-index: final valid table element
                            if param=="a" and index==2 and b"c = lt n 9\n" in source:
                                value[0] = 5  # original flags: neither the public nor loaded-data predicate holds
                            if param=="a" and typ=="ulong" and (b"ret uint get a 0\n" in source or
                                                               b"ret uint and get a 0 4294967295\n" in source):
                                value[:8] = struct.pack("<Q",4294967296)
                            data = bytes(value)
                    arrays.append({"data":data,"size":size,"observe":True,"name":param})
                    slots.append({"array":len(arrays)-1})
                    if unsized:
                        slots.append(count)
            if len(slots) > 6:
                raise ValueError("reviewed call exceeds six private-ABI slots")
            spec = {"name":f"{name}-{index}","entry":entry,"slots":slots,"arrays":arrays,
                    "result":"void" if returns == "void" else "scalar",
                    "evidence":{"source_sha256":sha,"source_spec":REVIEWED_SOURCES[sha],
                                "signature":line.decode(),"input_authority":recorded or
                                "explicit original loaded-data predicates/conversion boundary and zero vectors; native differential only",
                                "abi":"docs/osc/OSC-1-DESIGN.md section 7; omc param/xpro/xunit"}}
            # A recorded input list is not an independent result oracle. The
            # original rung4 conductor compares the OSC/C referee, including
            # traps; absent a result table retain native differential evidence.
            if recorded and "expected" not in recorded:
                spec["expected_exit"] = None
                spec["evidence"]["result_authority"] = "seed/rung4/gate4.sh cmpc; native differential, not independent semantics"
            trap = None
            if sha in (
                "8e1b8605e8d77c792a2aaf6adeb2167a38a538d0857792b006eeafd408d78e94",
                "79d2da33312969e14d9cdefe34b19306cce03471d91add08aa1e0285af22cf9c",
            ):
                trap = 110  # dup32 halves differ; original s3/sec5 kept controls.
            if index == 1 and sha in (
                "1de96237beb1b2d7d76fb226cd930036d8cc1d8c82faf4114f3309a1396365a5",
                "3490824f6e8cb82e661c7a408e8e4264790875e9e0147914ad4b0571dde0fc79",
                "e1b60bc4c240e03be0c0fbdb630066f295ff7a53b73a380dab52fa8509f498d6",
            ):
                trap = 103  # zero-length input, unconditional element access
            if sha == "ef1b3dc9f12deef3e2536b6689e79f529f216bff87c8c7b8dd5d5f259b7f3703":
                if name == "revtrap":
                    trap = 109  # explicit trap after filling the output
                if index == 1 and name in ("rev", "revtrap"):
                    trap = 103  # only these exports unconditionally read the empty view
            if index == 0 and sha == "21e366be6ad718395e5018186a3915d57ea02147a54b247ef1f69f001960f642":
                trap = 105  # original reachable-trap conversion counterexample
            if index == 1 and sha in (
                "432b642f9b3f5a0502705ef06d61502f4312e82c85adb58b64e5791a5d553e49",
                "1ab250b1d4e7d2f437ee680cce5a8918752cf0ef8efe1d89b490270005ceb294",
                "06c137ece7a94b3e6f5e19043bd6045fbe5208019b4366a43de9cb7558cee587",
                "035d21b803806853451dc2f408cc29c3490714b7b73081dd8cfe4b283abb2f85",
                "f4c545da9cfed4a6884b39634a371c09353139992f0be377dcea2f4e65e9187d",
                "f578c1f645e8a7e6112b2f45028fa8a018f73fad2149d0de16141607b0969ccc",
                "781ba1bf2768974166c10a3724acbf766080db732bee8d195fa3d872a5ab05c9",
                "050c286588e3066c396697c397c999a6ac8aa98334cc9c3bb3fcb43a6d8c084f",
                "5b09682d993bbb9afb96fdbc0f7a75c8e2cf14fc094b5b6c6e1ecf7fcd6ee374",
            ):
                trap = 102  # original div-by-zero arm
            if index == 1 and name == "collatz_steps":
                trap = 104  # cases.tsv dev08, zero input exhausts loop bound
            if sha == "94c38bc25d64c36a7e0cebc1e7fe0cfef51517507322a3f526cb31e1e0553444":
                trap = 109  # cases.tsv control explicit, last-function entry
            if index == 1 and sha == "0419a9380955e1199e9927412cdbde107b081243da2345f0cc77900ce641554e":
                trap = 106  # regression.tsv nestedvoid, callee traps first
            if trap is not None:
                spec.update(expected_exit=trap, expected_stdout=b"")
                spec["evidence"]["trap_authority"] = "reviewed source body and original conductor table; caller vector retained"
            if sha in (
                "af6c2faa6087359c0c51bcd5d61d973143f45d4719eb98278370bfb03284a79e",
                "2702ef2f9bcf2fbadd6c9ced8382a13f1939378c024a5c5706d167d98798be13",
                "20773907e95cbc9b1e157be91b099dc1ad13ab6ca79d2966e4259a93a8625eff",
            ):
                # Exact dup32 constructor: zero and all-one representation
                # boundaries, observing the entire returned representation.
                slots[0] = 4294967295 if index == 0 else 0
                spec.update(expected_exit=0, expected_stdout=bytes([255])*8 if index == 0 else bytes(8))
                spec["evidence"]["result_authority"] = "reviewed exact dup32 constructor and open; full-width representation"
            if sha in (
                "a7682122ef8f6707e48f8d6cf76459aca4ebab53ab3245dd51b88d8067fad948",
                "03fba96f85d365cec80d11c1f10400d6f1874c924e3e824458fb812c023f267c",
            ):
                secret = 1 if index == 0 else 0
                context = bytearray(104)
                context[40 if sha.startswith("a768") else 32] = secret
                arrays[0]["data"] = bytes(104)
                arrays[1]["data"] = bytes([secret])
                value = 0 if sha.startswith("a768") else secret
                spec.update(expected_exit=0, expected_stdout=struct.pack("<Q",value)+bytes(context)+bytes([secret]))
                spec["evidence"]["result_authority"] = "reviewed store offset and branch; pub-secret native-zero/one controls; complete edit and input observations"
            if sha in (
                "313b425a4e6b779633e06c0ed3dd6906730f39d230834b2921e4abffe9428a41",
                "28883f52212233aa4bc76182dec3733ad550f209a109a7bd57f5c4227a373e10",
            ):
                # The original helper really writes stdout. The zero return word
                # is the wrapper's void observation, not a fabricated I/O result.
                spec.update(expected_exit=0, expected_stdout=(b"1\n" if index == 0 else b"0\n")+bytes(8))
                spec["evidence"]["result_authority"] = "reviewed private-void decimal helper body; stdout syscall plus void observation"
            if sha in (
                "d65dd8eefd07ecb2e6fa2498fbedb1ad4919681632e2bf99b5b0fffe6f3c56b5",
                "28932d16844b094298e10b2ddbd3e21499668ec5877d67dca19339100a04363f",
            ):
                payload = arrays[0]["data"]
                spec.update(expected_exit=0, expected_stdout=payload[:1]+struct.pack("<Q",1)+payload)
                spec["evidence"]["result_authority"] = "reviewed write(1,a,0,1) syscall, actual byte-count return and unchanged edit array"
            if recorded and "expected" in recorded:
                want = recorded["expected"][index]
                if want.startswith("TRAP "):
                    spec.update(expected_exit=100+int(want.split()[1]),expected_stdout=b"")
                else:
                    spec.update(expected_exit=0,expected_stdout=(int(want)&0xffffffffffffffff).to_bytes(8,"little"))
            result.append(spec)
    return result


def select_a64_entry(image, index):
    """Select an existing export for the original entryword-zero leak scanner.

    Only dispatch word zero changes; the selected branch's original target and
    every body byte remain identical. This is entry selection, not an optimized
    or rewritten SHA implementation.
    """
    if image.startswith(b"\x7fELF") or len(image)%4 or index<0 or 4*index+4>len(image):
        raise ValueError("entry selection requires a complete AArch64 bare unit")
    word = struct.unpack_from("<I",image,4*index)[0]
    if word>>26 != 5:
        raise ValueError("selected export is not an unconditional B dispatch")
    immediate = word&0x3ffffff
    if immediate&(1<<25): immediate -= 1<<26
    target = 4*index+4*immediate
    if target<0 or target>=len(image):
        raise ValueError("selected branch target is outside original unit")
    return struct.pack("<I",0x14000000|(target//4))+image[4:]


# Match the compiler's program-mode owned-stack bounds, without using or growing
# the host argument stack. External ABI arrays have a separate mapping.
STACK_FLOOR = 268435456
STACK_TOP = 276824064


def _elf(machine, code, entry, memsize=None):
    base = 0x400000
    offset = 4096
    size = offset + len(code)
    header = b"\x7fELF"+bytes((2,1,1,0))+bytes(8)
    header += struct.pack("<HHIQQQIHHHHHH",2,machine,1,base+offset+entry,64,0,0,64,56,2,0,0,0)
    header += struct.pack("<IIQQQQQQ",1,7,0,base,base,size,max(size,memsize or size),4096)
    header += struct.pack("<IIQQQQQQ",1,6,0,STACK_FLOOR,STACK_FLOOR,0,STACK_TOP-STACK_FLOOR,4096)
    return header.ljust(offset,b"\0")+code


def wrap_bare(image, source, arch, spec, corrupt_result=False):
    """Wrap original bytes, preserving relocation-invariant private code verbatim.

    The negative control flips one result bit AFTER the candidate returns. It is
    intentionally not a mutant compiler and must differ on a nontrapping case.
    Arrays live in the ELF data mapping, not Omega's owned-array stack. Optional
    return_bytes observation supports large external-output APIs without changing
    their declared array sizes or dumping uninitialized capacity.
    """
    if image.startswith(b"\x7fELF"):
        raise ValueError("bare wrapper requires a bare unit, not an ELF program")
    if arch in ("x86","x86_64","x86linux"):
        machine, stride = 62,6
    elif arch in ("aarch64","arm64","native"):
        machine, stride = 183,4
    else:
        raise ValueError("unknown oracle architecture: "+arch)
    # Reserve a fixed oracle-stub area; source image starts at the same address on
    # both architectures, independently from its length and export count.
    unit = 4096
    dataoff = (unit+len(image)+15)&~15
    data = bytearray(816)
    positions = []
    logical = 816
    for array in spec["arrays"]:
        logical = (logical+7)&~7
        positions.append(logical)
        payload = array.get("data",b"")
        size = array["size"]
        if len(payload)>size or size<0:
            raise ValueError("invalid oracle array storage")
        if payload:
            data.extend(bytes(logical-len(data)))
            data.extend(payload)
        logical += max(size,8)  # empty views still have a real non-NULL pointer
    frame = 16
    runtime = 528
    total = (logical+15)&~15
    base = 0x401000
    address = lambda offset:base+dataoff+offset
    if address(total) > STACK_FLOOR:
        raise ValueError("external arrays overlap the fixed owned stack")
    def slotvalue(slot):
        return address(positions[slot["array"]])+slot.get("offset",0) if isinstance(slot,dict) else slot
    observations = [(0,8,8)]
    for array,pos in zip(spec["arrays"],positions):
        if array.get("observe",True):
            observed = array.get("observe_size",array["size"])
            if observed<0 or observed>array["size"]:
                raise ValueError("observation exceeds declared array storage")
            observations.append((pos,"result" if array.get("return_bytes") else observed,array["size"]))
    target = unit+spec.get("entry",0)*stride
    if target>=unit+len(image):
        raise ValueError("export target is outside original image")
    code = bytearray()
    fail_jumps = []
    if machine == 62:
        def mov(reg,value):
            code.extend(bytes((0x49 if reg>=8 else 0x48,0xb8+(reg&7))))
            code.extend(struct.pack("<Q",value & 0xffffffffffffffff))
        mov(5,address(frame+256))  # RBP[-8..-48] incoming private ABI slots
        code.extend(b"\x49\x89\xe5")  # R13 = original kernel argument stack
        mov(4,STACK_TOP-256)          # synthetic caller frame plus 272-byte callee allowance
        mov(15,STACK_FLOOR)
        mov(14,address(runtime))     # frozen 32-entry bare runtime table
        for call in spec.get("calls", [spec]):
            for i,slot in enumerate(call["slots"]):
                mov(0,slotvalue(slot))
                code.extend(b"\x48\x89\x85"+struct.pack("<i",-8*(i+1)))
            calltarget = unit+call.get("entry",0)*stride
            if calltarget >= unit+len(image):
                raise ValueError("call target is outside original image")
            code.extend(b"\xe8"+struct.pack("<i",calltarget-len(code)-5))
        if spec["result"]=="void": code.extend(b"\x31\xc0")
        if corrupt_result: code.extend(b"\x48\x83\xf0\x01")
        mov(11,address(0))
        code.extend(b"\x49\x89\x03")
        for pos,size,capacity in observations:
            mov(0,1); mov(7,1); mov(6,address(pos))
            if size=="result":
                mov(11,address(0)); code.extend(b"\x49\x8b\x13")
                mov(11,capacity); code.extend(b"\x4c\x39\xda")
                fail_jumps.append(len(code)); code.extend(b"\x0f\x87\0\0\0\0")
            else: mov(2,size)
            code.extend(b"\x0f\x05")
        mov(0,60); mov(7,0); code.extend(b"\x0f\x05")
        trap = len(code)
        code.extend(b"\x89\xf7\x83\xc7\x64")  # EDI = bare callback ESI trap code + 100
        mov(0,60); code.extend(b"\x0f\x05")
        struct.pack_into("<Q",data,runtime+16,base+trap)
        failure = len(code)
        mov(0,60); mov(7,90); code.extend(b"\x0f\x05")
        for jump in fail_jumps:
            struct.pack_into("<i",code,jump+2,failure-jump-6)
    else:
        def emit(word):code.extend(struct.pack("<I",word))
        def mov(reg,value):
            value &= 0xffffffffffffffff
            emit(0xd2800000 | ((value&65535)<<5) | reg)
            for shift in (16,32,48):emit(0xf2800000 | ((shift//16)<<21) | (((value>>shift)&65535)<<5) | reg)
        mov(16,STACK_TOP)
        emit(0x9100021f)  # ADD SP,X16,#0; same fixed mapped owned stack as x86
        for call in spec.get("calls", [spec]):
            for i,slot in enumerate(call["slots"]):mov(i,slotvalue(slot))
            mov(7,address(runtime))
            branch = len(code)
            calltarget = unit+call.get("entry",0)*stride
            if calltarget >= unit+len(image):
                raise ValueError("call target is outside original image")
            emit(0x94000000 | (((calltarget-branch)//4)&0x3ffffff))
        if spec["result"]=="void":mov(0,0)
        if corrupt_result:
            mov(9,1); emit(0xca090000) # EOR X0,X0,X9
        mov(9,address(0)); emit(0xf9000120)
        for pos,size,capacity in observations:
            mov(0,1); mov(1,address(pos))
            if size=="result":
                mov(9,address(0)); emit(0xf9400122)
                mov(10,capacity); emit(0xeb0a005f)  # CMP X2,X10, unsigned capacity
                fail_jumps.append(len(code)); emit(0x54000008)  # B.HI failure
            else:mov(2,size)
            mov(8,64); emit(0xd4000001)
        mov(0,0); mov(8,93); emit(0xd4000001)
        trap = len(code)
        emit(0x91019020) # ADD X0,X1,#100 (OscRt.trap callback)
        mov(8,93); emit(0xd4000001)
        struct.pack_into("<Q",data,runtime+16,base+trap)
        failure = len(code)
        mov(0,90); mov(8,93); emit(0xd4000001)
        for jump in fail_jumps:
            struct.pack_into("<I",code,jump,0x54000008|(((failure-jump)//4)<<5))
    if len(code)>unit:
        raise ValueError("oracle stub exceeds its fixed reservation")
    payload = bytes(code).ljust(unit,b"\0")+image
    payload = payload.ljust(dataoff,b"\0")+data
    return _elf(machine,payload,0,4096+dataoff+total)


CURRENT_SHA256 = 'dbf002dd6aa36585632b9d01d98ac3f0bd2fa0f0475082940de53638847994bb'
RESOURCE_PINS = {'seed/qualification/om2/fixtures/sha256-k.dec': '66e0d9b40b2605fb5984b25fb10e1ea162dcb7ad9bb593ec8aaf1056a85c5b4e', 'seed/qualification/om2/fixtures/sha256-iv.dec': 'b831ee5e6e1ed07aeb8654d0dbd962f76a3078a89b091453f059ac26f3fea103', 'seed/qualification/om2/selftest/pass.cases': 'b3158f424965f8e8c6f590e736e8bd52622befb45978e7fbffc9e1063fd83dd1', 'seed/qualification/om2/selftest/fail.cases': '2bff4f5a29fa9f17e3416e769538ed845154317cb7d82baa39980c88ba055c44', 'seed/qualification/om2/selftest/cat.om': '2ad4147ae3e2a3b836b862b4653bf2fe83c86a98b4b0c50896ebff84338537a1', 'seed/qualification/om2/selftest/cat-bad.om': '46066af11f0ee008a60d719d3033ff6d9ddef3c854e318271f3cf577ed8a753d', 'seed/qualification/om2/fixtures/gen/seq-300.bin': '43f9b5d59eb108817176c6f65c2c6203a22f2ae8bc28b7a1dde45947678c5042', 'seed/qualification/om2/fixtures/gen/rnd-7-64.bin': '47a8c8a33e5576b929664df2e1bec6aae0158d3dee0210185a975de8a9a04c39', 'seed/qualification/om2/fixtures/gen/rep-0-5.bin': '8855508aade16ec573d21e6a485dfd0a7624085c1a14b5ecdd6485de0c6839a4', 'seed/qualification/security673/branch-corpus.om': 'a2672e4d018ca2ded61044af19bcde0ad7f8a3990429067aa2d5086ee681a413', 'seed/qualification/security673/branch-mutate.om': '4596d9acc712d7475ed964b1a4d8adfd466e08a64f1448cd7159ff1da447848d', 'seed/qualification/security673/branch-check.om': '3165c09cb7ed7523355ccaa1acbdc89be55c2882fbb61fc7f25594bd68deeca3', 'seed/qualification/security673/array-fill.om': 'bcfb200a48291c27d4a700ea074e02616a881767c8838a52ad2dfd7b83b52d33', 'seed/qualification/security673/oracle-corpus.om': '41118388d232dd6897bebf9b239f4174a2e5054cdd07a4753c4ce60bf7c022fa', 'seed/qualification/security673/gate-child.om': '459a60da8e660ee1f433a9679bb41467074fda45997e5168e23e92ebad0d570b'}
PURE_NAME_CONTROLS = {'e1109db40b1ab3f8b7c5b847bb9032810fc3dcf836ec58a0caf5459033636764': 'seed/omc/om2/s3/a-kept.om', 'e6e04d5f60f921be058b89ab369735e6b8aa9d398d836f713bbc8c688c95c513': 'seed/omc/om2/s3/a-ok.om', '92c7813513f2bb511bbacc53e1a351a417cabc10139134e477bfb61483005c92': 'seed/omc/om2/s3/a-om1.om', 'cfccbeb34d3bfb051e01977b9acb2b6166c41bbfaba85a26c85c123643e8012a': 'pack:seed/qualification/om2/c2/corpus.pack:3495087:1831', 'e7ea21711b45c957a6264e373e66f597c091b67809bd0c785113d2ea601e71d7': 'pack:seed/qualification/om2/c2/corpus.pack:3497014:1462', '2e011318d020bd5e0283bde413b74335b4e4b3c5b417e34b03caa832f5efcf66': 'pack:seed/qualification/om2/c2/corpus.pack:3516061:1403', '63eab87101f8c71645b26023d422fb910c8d6ce603504608bfb395fc5a7a0cb0': 'pack:seed/qualification/om2/c2/corpus.pack:3517560:1034', '767fc04dd38d62a2f1f700366a641a2a7ea7b5429e0341dcffb97bcc8558029b': 'pack:seed/qualification/om2/c2/corpus.pack:3596649:1500', '4ae68017b93e47a9ddc638ef9d0f3e950c9639346102c510c36909e728f28dec': 'pack:seed/qualification/om2/c2/corpus.pack:3609196:824', '3bd0be5b72fc92825742422dc7157196d85d57891b81c8ce89a6df8969584a82': 'seed/qualification/om2/sec/s5s7/a5-allops.om', '9cb0393b2c6c8f99ad93e5af2db0ae4bbf17de884767de162f9737c20717f369': 'seed/qualification/om2/sec/s5s7/a5-exact.om', '0ec2b5eb427d759eb7563f9dbf8cd1a921a23b9e80c9d7f779315490b23f274c': 'seed/qualification/om2/sec/s5s7/a5-kept.om', '1e0a5f4c22cee6170a7f6787fb6c1cc215e1ff9aa0e997bc8f7450c06b3a072b': 'seed/qualification/om2/sec/s5s7/a5-rot64.om', '373509e26a12a1c579bf4021d1c275929da7c534812ecdf62c9d0486f11d3eff': 'seed/qualification/om2/sec/s5s7/a5-swap.om', 'b21d8c82a5a3648011325261bac9481e9e3147dd29e17b491e04008bda289295': 'seed/qualification/om2/sec/s5s7/a5-exact.om+seed/qualification/om2/sec/s5s7/pr.om', '56fbd35bc5e262553585cdd18a54524622c638fff89d9bf0f84d9c42884c8366': 'seed/qualification/om2/sec/s5s7/a5-allops.om+seed/qualification/om2/sec/s5s7/pr.om', 'd7df44a4f803645594ec1e758201d8e1c672c4ebe94c553ed9b4cdca98bb4607': 'seed/qualification/om2/sec/s5s7/a5-rot64.om+seed/qualification/om2/sec/s5s7/pr.om', 'c0c8cd2d3c8c9fe4e1cb8f9cfebb5af8d5c625368db1d9af713b0acb33504b24': 'seed/qualification/om2/sec/s5s7/a5-kept.om+seed/qualification/om2/sec/s5s7/pr.om', 'c7ceccff2f370d5c82df978399a82372f6d354703d9ff263de8d560df13d3481': 'seed/qualification/om2/sec/s5s7/a5-swap.om+seed/qualification/om2/sec/s5s7/pr.om'}
ABI_DRIVER = ("seed/qualification/om2/oracle/s5s7/abi/driver.c",
              "a9d134a149ae1bbc89ca799ffa6036cb0612947e36e267e1535a544d34c52d3b")
SHA_DRIVER = ("seed/qualification/om2/oracle/s5s7/sha/sha_driver.c",
              "50ac882ad0e4ec19f3076ad29d52ffa864ac5960490116f9f6c72fc9ef0f7b48")
HELPERS = {"putu": ("seed/qualification/om2/sec/s0s2/lib/putu.om",
                    "313b425a4e6b779633e06c0ed3dd6906730f39d230834b2921e4abffe9428a41"),
           "pr": ("seed/qualification/om2/sec/s5s7/pr.om",
                  "e6d0e0a3e9398c62651fb2838a4dcf35c7836955b2c21327f28b2687d937baf0")}
INLINE_SOURCE = b"om2\nfn plain x ulong to ulong\nret add x 1\nend\nexport fn entry x ulong to ulong\nret plain x\nend\n"
REVIEWED_SOURCES[digest(INLINE_SOURCE)] = "oracle:non-inline-calls-stay-calls"
GOLDEN_PART_SOURCE = b"""om2
fn plen v view uchar * to ulong
ret len v
end
export fn golden v view uchar * to ulong
ret plen part v 18446744073709551614 4
end
export fn empty v view uchar * to ulong
ret plen part v len v 0
end
"""
REVIEWED_SOURCES[digest(GOLDEN_PART_SOURCE)] = "oracle:forged-golden-part-before-any-access"


def sha256_calls(root, source):
    """Current four-entry SHA API, against hashlib, independently from C2 history.

    Exercises one-shot and init/update/final, empty inputs, both padding lengths,
    block boundaries and split/zero updates. Only the digest is observed; context
    bytes not promised by the API are not turned into a new qualification gate.
    These private calls do NOT claim C-boundary null/alignment/overlap validation.
    """
    if digest(source) != CURRENT_SHA256:
        raise ValueError("current SHA API source changed after review")
    specs = []
    for n in (0,3,55,56,63,64,65,1000):
        message = b"abc" if n==3 else bytes(i%251 for i in range(n))
        for streaming in (False,True):
            arrays=[{"name":"ctx","size":104,"data":bytes(104),"observe":False},
                    {"name":"message","size":n,"data":message,"observe":False},
                    {"name":"digest","size":32,"data":bytes(32),"observe":True}]
            ptr=lambda a,o=0:{"array":a,"offset":o}
            if streaming:
                cut = n//2
                calls=[{"entry":0,"slots":[ptr(0)]},
                       {"entry":1,"slots":[ptr(0),ptr(1),cut]},
                       {"entry":1,"slots":[ptr(0),ptr(1,cut),0]},
                       {"entry":1,"slots":[ptr(0),ptr(1,cut),n-cut]},
                       {"entry":2,"slots":[ptr(0),ptr(2)]}]
            else:
                calls=[{"entry":3,"slots":[ptr(1),n,ptr(2)]}]
            specs.append({"name":f"current-sha-{'stream' if streaming else 'hash'}-{n}",
                          "entry":calls[0]["entry"],"slots":calls[0]["slots"],"calls":calls,
                          "arrays":arrays,"result":"void","expected_exit":0,
                          "expected_stdout":bytes(8)+hashlib.sha256(message).digest(),
                          "evidence":{"source_sha256":CURRENT_SHA256,
                          "source_spec":"om/sha256/sha256-om2.om",
                          "oracle":"Python hashlib SHA-256; original API comments lines234-344",
                          "input_hex":message.hex(),"streaming":streaming}})
    return specs


def directive_plan(root, name):
    review = DIRECTIVE_REVIEWS.get(name)
    if review is None:
        return {}
    evidence = dict(review)
    evidence["review_revision"] = REVIEW_REVISION
    manifest = name.split(":",1)[0]
    _pinned(root,manifest,review["manifest_sha256"])
    label = name.rsplit(":",1)[-1]
    if label == "slots-order":
        rel = "seed/qualification/om2/sec/s3s4/slots-3v.om"
        sha = "31ead9b2a44ca4b10fd637b0fd9ee339005d33b48a731795e71f57d8189628a5"
        return {"source":_pinned(root,rel,sha),"driver":"bare-private-abi","evidence":evidence}
    if label == "inline-calls-stay-calls":
        evidence["structural"] = {"minimum_calls":1}
        evidence["structural_scope"] = "two-function bare unit; plain has no callees, entry must CALL plain"
        return {"source":INLINE_SOURCE,"driver":"bare-private-abi","operation":"structural","evidence":evidence}
    if label == "sha256-om2-scan":
        evidence.update(stdin_compiled_source="om/sha256/sha256-om2.om",
                        stdin_source_sha256=CURRENT_SHA256,stdin_export_index=3,
                        stdin_export="sha256_hash",required_verdict="TRACE-EQUIV-STATIC PASS",
                        scope="original scanner on the actual current hash entry; digest API is a separate synthetic case")
        return {"source":_pinned(root,"seed/qualification/om2/leak/leakscan.om",
                                "6b14fd9bb6f174de4a87e14b5ae38c9984de48c91a0b18c468bb1197adb2c82c"),
                "driver":"sha-leak-scan","args":(),"expected_exit":0,"evidence":evidence}
    if label == "v6-part-golden-literal":
        evidence.update(original_impossibility=review["reason"],
                        scope="private-ABI invalid-view negative control; not public bridge conformance",
                        no_access="only length is observed; even an accepting mutant never dereferences the forged view")
        return {"source":GOLDEN_PART_SOURCE,"driver":"bare-private-abi","evidence":evidence}
    if label in ("s1-d-mid-short-line","s2-fmt-bad-trailblank-line","program-site-table"):
        return observation_plan(root,name)
    if label.startswith("abi-") or label in ("mut-e-ident","mut-f-abi-len"):
        _pinned(root,*ABI_DRIVER)
        evidence.update(original_driver=ABI_DRIVER[0],original_driver_sha256=ABI_DRIVER[1],
                        boundary="SysV generated bridge; raw private-ABI body execution is separate")
        return {"blocker":"BLOCKED_ON BridgeX86: "+review["requirement"],"evidence":evidence}
    if label.startswith("sha-"):
        _pinned(root,*SHA_DRIVER)
        evidence.update(original_driver=SHA_DRIVER[0],original_driver_sha256=SHA_DRIVER[1],
                        boundary="original generated C SHA driver, not current raw-unit body")
        return {"blocker":"BLOCKED_ON BridgeX86: "+review["requirement"],"evidence":evidence}
    # Preserve the particular unresolved contract, not a guessed hardware gate.
    return {"blocker":review["reason"],"evidence":evidence}


# Reviewed host-effect drivers for rows whose conductors launch programs or touch files.
# Every effect stays in the runner's private per-architecture cwd under TMPDIR, under the
# runner's wall deadline and EFFECT_CPU_SECONDS of CPU. qemu-user is not a registered
# binfmt, so any program either arm launches is the same native AArch64 file in both
# runs; only the program under test changes architecture. None of these sources has a
# network word except pure-child.om, whose socknew arm is unreachable (see its review).
EFFECT_CPU_SECONDS = 10
# The frozen security673 packets name /bin/true, /bin/echo and /bin/tr by absolute path.
# The qualification host has no unprivileged user namespace (unshare -r: uid_map EPERM),
# so they cannot be substituted; the exact host builds (Ubuntu coreutils 9.4-3ubuntu6.3
# arm64) are pinned and any other build refuses the row as BLOCKED, never PASS.
HOST_CHILD_PINS = {
    "/bin/true": "a82c1b5fd392d72148d53992a860224ff3753cb2dc8909e9bdd1620e54ee1b88",
    "/bin/echo": "a54dbac70bf94d2d3163aee0a43aadeb96c15b29713c1443a0514326660dd205",
    "/bin/tr": "8921ec5afe3898e9f1dbe2e0f568d36de759332547aa28874ddd03ff3954e92e",
}
_S673 = "seed/qualification/security673/"
_RUNS = _S673 + "relational/evidence/"
_IMAGES = {  # Pinned evidence images; *-native-mut.bin are AArch64 executables.
    "branch-native-mut": (_RUNS + "run2/public-branch-native-mut.bin",
                          "3c534a9224fd8ec81f1af1caa48507adb3b01cfc7ab6fdea709a6ee08c6ba5c1", 0o700),
    "address-native-mut": (_RUNS + "run2/public-address-native-mut.bin",
                           "d823b830e66b82b52cf0bc5ee480adacff1957f9a9411413f972ac30f2183417", 0o700),
    "branch-mut": (_RUNS + "run2/public-branch-mut.bin",
                   "cb906d54cb4a1d7940202d567367c6e30eeaa099a981fbabc20574e13721de1a", 0o600),
    "compare32": (_RUNS + "run3/compare32.bin",
                  "df0d1193b9555f3d6a4a1c5bc0e5d04605397000a22d8175e9b9f28965bdb162", 0o600),
    "embed": (_RUNS + "run3/embed.bin",
              "0f65c747d83fbd34f9058142c2dc68a8147365733c56e18abdd24104e944fe50", 0o600),
    "paired": (_RUNS + "run4/paired.bin",
               "686096547cda0cbbdf789c67bdc9f7aa7066ee36023af9c2353a262d2e313711", 0o600),
}
# renames-corpus.om (sha 9c44348b9cb258195be17e59318d90def4745327baed621b0fc46442f50ad883)
# output for mode byte m, exactly as gate669.om renamescc feeds renames-consumer.om.
_REN_SRC = "0020006275696c642f73656375726974793637332d6e61746976652f72656e2d737263"
_REN_DST = "20006275696c642f73656375726974793637332d6e61746976652f72656e2d647374"
RENAMES_PACKETS = {
    0: "02" + _REN_SRC + _REN_DST,
    1: "01" + _REN_SRC,
    2: "03" + _REN_SRC + _REN_DST + _REN_DST,
    3: "020000" + _REN_DST,
    4: "02" + _REN_SRC + "0000",
    5: "02" + _REN_SRC + _REN_DST + "00",
    6: "020020006275696c640073656375726974793637332d6e61746976652f72656e2d737263" + _REN_DST,
}


def host_child_mismatch():
    for path, sha in HOST_CHILD_PINS.items():
        try:
            observed = digest(Path(path).read_bytes())
        except OSError as error:
            return f"{path} unreadable: {error}"
        if observed != sha:
            return f"{path} sha256 {observed} is not the pinned {sha}"
    return None


def _inputs(*names):
    return [{"path": _IMAGES[n][0], "sha256": _IMAGES[n][1], "mode": _IMAGES[n][2]} for n in names]


def effect_plan(root, rel, sha):
    """Plan for a reviewed effect-scratch source; {} when the source is not reviewed here."""
    evidence = {"reviewed_source_sha256": sha}
    plan = {"driver": "effect-scratch", "evidence": evidence}
    if sha == "7f0ea5e3a0064457d3f4d12e9e74e664b7f59a4ca90083b8132b779604ff5310":
        evidence.update(
            effect_review="reads at most 8191 stdin bytes; execprog only of the packet's absolute path; every frozen "
                          "packet names /bin/true, /bin/echo or /bin/tr, each pinned in HOST_CHILD_PINS; no file or "
                          "network words",
            host_children=dict(HOST_CHILD_PINS),
            exit_authority=_S673 + "packets.cases p12-echo-argv: exit0, stdout packets/p12.out")
        mismatch = host_child_mismatch()
        if mismatch:
            return {"blocker": "BLOCKED_ON pinned host child: " + mismatch, "evidence": evidence}
        plan.update(stdin=_pinned(root, _S673 + "packets/p12.bin",
                                  "10840966be3580d3ca7ce939bc9ce2be664af39fc679f711aa76b7763da10a2f"),
                    expected_exit=0)
        return plan
    if sha == "39ff79d87e1e502cb13caaea6e9791a58e02bc7ecb7c27a754360195786c5138":
        evidence.update(
            effect_review="packs argv[1..] and execprogs argv[1]; the only images named are the pinned AArch64 "
                          "evidence images copied into private scratch (sources public-*-main.om have no host words)",
            effect_inputs=_inputs("branch-native-mut", "address-native-mut"),
            exit_authority=_S673 + "relational/qualification.cases execute-public-branch-mut-zero: exit7")
        plan.update(args=(_IMAGES["branch-native-mut"][0],), expected_exit=7)
        return plan
    if sha == "be37db5983ef8b5bda100d1f063be503449fb271c45a87f68084337c1bfe1d86":
        evidence.update(
            effect_review="read-only fopen/read/fclose of argv[1] and argv[2]; the manifest names only pinned "
                          "evidence images, copied into private scratch; stdout write only",
            effect_inputs=_inputs("compare32", "paired", "embed", "branch-mut", "branch-native-mut"),
            exit_authority=_S673 + "relational/bindings.cases embed-primitive: exit0, stdout embed.expected")
        plan.update(args=(_IMAGES["compare32"][0], _IMAGES["paired"][0]), expected_exit=0)
        return plan
    if sha in ("e184cdbc0fbe4d4cb2f2d36d4c159ef7f9fe87bd162d3082209fa32f5dc79252",
               "e125e57d03dbd2dbb23f592e015396b57a1b1277b6b160a91cbb916c38fc7bfa"):
        control = sha.startswith("e184")
        evidence.update(
            effect_review="read-only fopen of /proc/self/mem; pread of this process's own stack pool "
                          "0x10000000..0x10800000 into a private buffer; fclose; no writes",
            exit_authority="seed/omc/om2/s5/s5.cases " + ("s5-scrub-ctl: exit10" if control else "s5-scrub-mem: exit0"))
        plan.update(expected_exit=10 if control else 0)
        return plan
    if sha == "9bb5cd0e59d12af8b13de79db7a949b5278263b638309dfc6a86ab77c0382232":
        evidence.update(
            effect_review="mode0 creates build/security673-native/path-ok in private scratch; modes1-3 are refused "
                          "or failing fopens; modes4-6 argread only",
            effect_dirs=["build/security673-native"],
            exit_authority=_S673 + "gate669.om pathcc/pathwant: modes1,2,5 exit103, others exit0; argument id1")
        args = (_S673 + "branch-corpus.om",)
        plan.update(stdin=b"\0", args=args, expected_exit=0,
                    variants=[{"name": f"gate669-mode{m}", "stdin": bytes([m]), "args": args,
                               "expected_exit": 103 if m in (1, 2, 5) else 0} for m in range(1, 7)])
        return plan
    if sha == "4fed8e605272e03994b565fd799136a96ab14239ccc8bc7526c819f3148e6c69":
        evidence.update(
            effect_review="creates build/security673-native/ren-src in private scratch; renames only names from the "
                          "pinned renames-corpus packets, all under build/security673-native",
            effect_dirs=["build/security673-native"],
            exit_authority=_S673 + "gate669.om renamescc/renameswant: mode0 exit0, modes1-6 exit103")
        plan.update(stdin=bytes.fromhex(RENAMES_PACKETS[0]), expected_exit=0,
                    variants=[{"name": f"gate669-mode{m}", "stdin": bytes.fromhex(RENAMES_PACKETS[m]), "args": (),
                               "expected_exit": 103} for m in range(1, 7)])
        return plan
    if sha == "b784dde3b862bfc7784015377447b3fffd4c6f1a37d04680e03960fbabc5a0de":
        evidence.update(
            effect_review="creates synthetic-secret.data in private scratch; pipes; forks one child that execprogs "
                          "./exec-child (exec-child.om built natively from pinned source) or a missing name; waits "
                          "for that child only; dupfd to 65537 needs RLIMIT_NOFILE above 65537",
            native_child={"source": _S673 + "exec-child.om",
                          "source_sha256": "f2519f6be7068cf8af826101febd0f490327916322ac92a648dba9f3380324ec"},
            nofile=65600,
            exit_authority=_S673 + "exec-parent.om notes: modes0 success, 1 missing executable, "
                                   "2 isolate high-descriptor leak; each returns 0 when every assertion holds")
        plan.update(stdin=b"\0", expected_exit=0,
                    variants=[{"name": f"mode{m}", "stdin": bytes([m]), "args": (), "expected_exit": 0}
                              for m in (1, 2)])
        return plan
    if sha == "95c63d2aa7d69e6e04752279885496060a4152b5c1604ff75d65b201d48c713b":
        evidence.update(
            effect_review="run outside execpure: the runner passes only fds 0-2, so read 3 returns -EBADF, not the "
                          "-1 the strict profile yields, and the program exits 43 before any write, fopen, socknew, "
                          "fork or execprog arm; those arms run only under the AArch64 execpure evidence",
            exit_authority=_S673 + "pure-child.om: read 3 must equal -1 or ret 43")
        plan.update(stdin=b"public-input", expected_exit=43)
        return plan
    return {}


def plan_case(root, rel, source):
    if digest(source) == "e08ec033bd7d2e71ae272488029354eb447d8f7eed49d0f89d9d4eaf55518cfb":
        return {"driver":"pwrite-text", "expected_exit":0,
                "evidence":{"effect_review":"pwrite only to fd1, the runner's private seekable output file; no paths or device access",
                            "source_authority":"seed/omc/om2/s4/v-pwrite-text.om",
                            "result_authority":"s4.cases s4-v-pwrite-text: exact ABC newline output and exit0"}}
    effect = effect_plan(root, rel, digest(source))
    if effect:
        return effect
    sha = digest(source)
    if sha not in REVIEWED_SOURCES:
        return {}
    evidence = {"review_revision":REVIEW_REVISION,"reviewed_source_sha256":sha,
                "source_spec":REVIEWED_SOURCES[sha]}
    if not MAIN.search(source):
        evidence["effect_review"] = "arithmetic/array/private-memory only, except pinned stdout write helpers"
        return {"driver":"bare-private-abi","evidence":evidence}
    if sha in PURE_NAME_CONTROLS:
        evidence["effect_review"] = "open is abstract-value opening or an OM-1 identifier; not a host file operation; only stdout writes"
        override = source
        if b"puth " in source and not re.search(rb"(?m)^fn puth ",source):
            override += _pinned(root,*HELPERS["pr"])
            evidence["appended_helper"] = HELPERS["pr"]
        elif b"putu " in source and not re.search(rb"(?m)^fn putu ",source):
            override += _pinned(root,*HELPERS["putu"])
            evidence["appended_helper"] = HELPERS["putu"]
        elif b"putd " in source and not re.search(rb"(?m)^fn putd ",source):
            override += _pinned(root,*HELPERS["pr"])
            evidence["appended_helper"] = HELPERS["pr"]
        plan = {"source":override,"driver":"pure-name-control","evidence":evidence}
        if sha == "e1109db40b1ab3f8b7c5b847bb9032810fc3dcf836ec58a0caf5459033636764":
            plan["expected_exit"] = 110
            evidence["exit_authority"] = "seed/omc/om2/s3/s3.cases s3-a-kept"
        elif sha == "92c7813513f2bb511bbacc53e1a351a417cabc10139134e477bfb61483005c92":
            plan["expected_exit"] = 5
            evidence["exit_authority"] = "seed/omc/om2/s3/s3.cases s3-a-om1"
        elif sha in ("0ec2b5eb427d759eb7563f9dbf8cd1a921a23b9e80c9d7f779315490b23f274c", "c0c8cd2d3c8c9fe4e1cb8f9cfebb5af8d5c625368db1d9af713b0acb33504b24"):
            plan["expected_exit"] = 110
            evidence["exit_authority"] = "seed/qualification/om2/candidates/s4/export/sec5.cases a5-kept"
        elif sha in ("1e0a5f4c22cee6170a7f6787fb6c1cc215e1ff9aa0e997bc8f7450c06b3a072b", "d7df44a4f803645594ec1e758201d8e1c672c4ebe94c553ed9b4cdca98bb4607"):
            plan["expected_exit"] = 107
            evidence["exit_authority"] = "seed/qualification/om2/candidates/s4/export/sec5.cases a5-rot64"
        return plan
    if sha == "2fe76d4c44b3e3faf6bfd250ebec794ba5b3d11b6b9199366549b664d219cc38":
        evidence["effect_review"] = "opens /dev/null write-only; overflowing slice must trap before write; original guard.cases requires exit103"
        return {"driver":"slice-wrap-control","expected_exit":103,"evidence":evidence}
    if sha == "7bce001ac35fee18b0205df59aeeaea62b1d2eea68e336c0262ad3741f40028e":
        evidence["effect_review"] = "private cwd only: subdir/inside/mode2.tmp, child-local umask, local stat/fchdir, unlink cleanup"
        return {"driver":"fchdir","expected_exit":0,"evidence":evidence}
    if sha == "79857ee82002d11d3df4c0c2a99523aa3419d18c5fc188a41efc778887b4baa3":
        evidence["effect_review"] = "argc1 stdin-only BLAKE3 path; filename fopen arm not entered"
        return {"driver":"hash-stdin","stdin":b"abc","expected_exit":0,"evidence":evidence}
    if sha == "459a60da8e660ee1f433a9679bb41467074fda45997e5168e23e92ebad0d570b":
        evidence["effect_review"] = "mode0 consumes one zero byte and exits7, no signal or sleep; mode1 selfSIGKILL/mode2 deadline are conductor controls"
        return {"driver":"supervisor-child","stdin":b"\0","expected_exit":7,"evidence":evidence}
    if sha == "01fb867723854f43838a814a8571107e6fea2afba12444f57f35c9012741111d":
        evidence["effect_review"] = "SIGKILL targets getpid() of this isolated child only; original conductor must classify INVALID, not PASS"
        evidence["required_conductor_verdict"] = "INVALID signal"
        return {"driver":"supervisor-self-signal","expected_exit":-9,"evidence":evidence}
    if sha == "716c1dc3004d55178e09e7c40fa0995f993eea8611e09a70a1f74b9c7a0cf644":
        evidence["effect_review"] = "child-local five-second nanosleep; original deadline1 conductor must classify INVALID"
        evidence["required_conductor_verdict"] = "INVALID timeout"
        return {"driver":"supervisor-self-sleep","expected_exit":0,"evidence":evidence}
    if sha == "07bc11a69f0d629e0c262056061789114d8710505a308e3e2a5ac42ae73dee94":
        evidence["effect_review"] = "nested compiler/decoder gate; copied allowlist only, private children killed only by observed fork PID; all build writes private"
        return {"driver":"security673-gate","expected_exit":0,"evidence":evidence}
    if sha == "d4801acf04a44467bac5e1bedfc502b20afe009d8065079ae5880e987b0899a7":
        evidence["effect_review"] = "pure arithmetic loop, no host words; deliberately non-terminating CPU exhaustion"
        evidence["exit_authority"] = "seed/qualification/security673/README.md: CPU exhaustion dies with SIGXCPU (152) under the one-second limit"
        return {"driver":"cpu-limit","expected_exit":-24,"evidence":evidence}
    if sha == "250361be99ced1246405b2e4a2e2de71373a90d0a0569fb43821ed8b106e045f":
        evidence["effect_review"] = "nested compiler gate; exact hash argument checked before cases; default original pass.cases copied with pinned inputs"
        return {"driver":"corpus-gate","expected_exit":0,"evidence":evidence}
    return {}


def prepare_case(root, work, driver, compiler, *, case=None):
    """Prepare only copied content-pinned dependencies in architecture-local cwd.

    Returns argv (excluding executable). Does not execute any binary. Original
    conductors see a repository-shaped PRIVATE scratch, never the live checkout.
    Caller-supplied manifests require a complete pinned transitive resource list;
    the caller remains responsible for reviewing every source's host effects.
    """
    root,work,compiler = Path(root),Path(work),Path(compiler)
    if driver not in ("security673-gate","corpus-gate","effect-scratch"):
        return ()
    if root.resolve()==work.resolve() or root.resolve() in work.resolve().parents:
        raise ValueError("conductor scratch must be outside the source checkout")
    if driver == "effect-scratch":
        case = case or {}
        if "host_children" in case:
            mismatch = host_child_mismatch()
            if mismatch or case["host_children"] != HOST_CHILD_PINS:
                raise ValueError("BLOCKED_ON pinned host child: " + (mismatch or "reviewed pin set changed"))
        for rel in case.get("effect_dirs", []):
            if Path(rel).is_absolute() or ".." in Path(rel).parts:
                raise ValueError("unsafe effect scratch directory")
            (work/rel).mkdir(parents=True, exist_ok=True)
        for item in case.get("effect_inputs", []):
            rel = item["path"]
            if Path(rel).is_absolute() or ".." in Path(rel).parts:
                raise ValueError("unsafe effect input path")
            dest = work/rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(_pinned(root, rel, item["sha256"]))
            dest.chmod(item["mode"])
        return ()
    resources = dict(RESOURCE_PINS)
    if driver == "security673-gate":
        resources = {p:s for p,s in resources.items() if p.startswith("seed/qualification/security673/")}
    else:
        resources = {p:s for p,s in resources.items() if not p.startswith("seed/qualification/security673/")}
    resource_specs = {}
    if case:
        for item in case.get("resources",[]):
            rel = item["path"]
            if Path(rel).is_absolute() or ".." in Path(rel).parts:
                raise ValueError("unsafe oracle resource path")
            resources[rel] = item["sha256"]
            resource_specs[rel] = item
    for rel,sha in resources.items():
        item = resource_specs.get(rel,{})
        if "git_revision" in item:
            import subprocess
            revision = item["git_revision"]
            gitpath = item.get("git_path",rel)
            if not re.fullmatch(r"[0-9a-f]{40}",revision):
                raise ValueError("historical oracle needs a full exact Git revision")
            if Path(gitpath).is_absolute() or ".." in Path(gitpath).parts:
                raise ValueError("unsafe historical Git path")
            blob = subprocess.check_output(
                ["git","show",revision+":"+gitpath],cwd=root)
        elif "pack_path" in item:
            packpath = item["pack_path"]
            if Path(packpath).is_absolute() or ".." in Path(packpath).parts:
                raise ValueError("unsafe frozen-pack path")
            offset,length = item["offset"],item["length"]
            if not isinstance(offset,int) or not isinstance(length,int) or offset<0 or length<0:
                raise ValueError("invalid frozen source byte range")
            with (root/packpath).open("rb") as packed:
                packed.seek(offset)
                blob = packed.read(length)
            if len(blob)!=length:
                raise ValueError("frozen source byte range is incomplete")
        else:
            blob = _pinned(root,rel,sha)
        if digest(blob)!=sha:
            raise ValueError("historical oracle resource digest mismatch: "+rel)
        dest = work/rel
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(blob)
    native = work/"seed/rung4/om4.bin"
    native.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(compiler,native)
    native.chmod(0o700)
    if driver=="security673-gate":
        # The operator-selected compiler is trusted only as this oracle input.
        # It compiles the current compiler source; parent owns that source review.
        dest=work/"seed/omc/omc.om"
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes((root/"seed/omc/omc.om").read_bytes())
        (work/"build").mkdir(exist_ok=True)
        return ()
    manifest = (case or {}).get("manifest","seed/qualification/om2/selftest/pass.cases")
    if manifest not in resources:
        raise ValueError("manifest absent from reviewed pinned resource list")
    return ("./"+native.relative_to(work).as_posix(),digest(native.read_bytes()),manifest,"conductor-evidence")


def cubin_calls(source, *, expected_image=None):
    """Full-sized actual cubin unit ABI; no substituted small-array declaration.

    The positive metadata and EXIT bytes are the original host-only
    tests/ampere_cubin_test.c vector. The independent expected_image, if supplied,
    must be that conductor's C-oracle output, never a candidate-generated image.
    The negative vector is the original unit's explicit unsupported-target arm:
    its private return is 1 (not the public C wrapper's OMEGA_CUBIN_E_TARGET=8).
    """
    pin = "3611fd7bf1a560b6cf1075cfeb15b4b17496a388d837d886280c8cb80f301646"
    if digest(source)!=pin:
        raise ValueError("unreviewed cubin unit source")
    metadata = [11,16,1,1024,8,0,86,0,352,8,1]+[0]*93
    text = bytes.fromhex("4d790000000000000000800300ea0f00")
    evidence = {"source_sha256":pin,"source":"om/cubin/cubin_elf.om",
                "conductor":"tests/ampere_cubin_test.c",
                "conductor_sha256":"51db6a2e78a6c597b4a46808083d85674312a99df3198449f45d814b891885e8",
                "effects":"pure array reads/writes; no CUDA, GPU, driver, service or process call",
                "storage":"original 16-MiB text and 16777216-ulong output in external ELF/BSS mapping; owned stack unchanged"}
    def make(name,values):
        arrays = [{"name":"metadata","size":832,"data":struct.pack("<104Q",*values),"observe":False},
                  {"name":"name","size":64,"data":b"ampere_exit","observe":False},
                  {"name":"text","size":16777216,"data":text,"observe":False},
                  {"name":"output","size":134217728,"data":b"","return_bytes":True}]
        return {"name":name,"entry":0,"slots":[{"array":i} for i in range(4)],
                "arrays":arrays,"result":"ulong","expected_exit":0,"evidence":dict(evidence)}
    positive = make("cubin-sm86-full-external-abi",metadata)
    if expected_image is not None:
        if not isinstance(expected_image,bytes) or len(expected_image)<=7 or len(expected_image)>134217728:
            raise ValueError("invalid independent C-oracle image")
        positive["expected_stdout"] = struct.pack("<Q",len(expected_image))+expected_image
        positive["evidence"]["expected_image_sha256"] = digest(expected_image)
    else:
        positive["evidence"]["independent_C_oracle"] = "NOT_RUN; only native-vs-emulated full-result differential"
    bad_metadata = list(metadata)
    bad_metadata[6] = 89
    negative = make("cubin-unsupported-sm89",bad_metadata)
    negative["arrays"][3].update(return_bytes=False,observe_size=24)
    negative["expected_stdout"] = struct.pack("<Q",1)+bytes(24)
    negative["evidence"]["expected"] = "chk rejects target89 before touching output; private refusal1 and three untouched zero words"
    return [positive,negative]


BRIDGE_UNIT_PINS = {
    "seed/qualification/om2/sec/s5s7/abi/abiv.om":"ef1b3dc9f12deef3e2536b6689e79f529f216bff87c8c7b8dd5d5f259b7f3703",
    "seed/qualification/om2/sec/s5s7/abi/abiw.om":"743fb07c03061177ce5133926dc442d21d3ba1b74d133f661a2de9ccec1ace92",
    "seed/qualification/om2/sec/s5s7/abi/abi1.om":"af68bd088bc58ce973cf53fcc96743dba41a489fe56a4a9dd4f3ecc3c0e3aea0",
}
STRICT_UNIT_PINS = {
    "om/units.sh":"db7e2a7c15a3c4d98750bcb019227a64077bfbe5005b7a50b545e4337fa7b3b3",
    "om/sha256/sha256-om2.om":CURRENT_SHA256,
    "om/blake3/b3unit.om":"124c19e6343133d45146e565eeb9307bbe7350a07fca01a736d6da8c3e5d4863",
    "src/omega_om_units.c":"8b60a68657cf49f5631df3bde59d932af937bff84a4dfc8b0321b8ca8f72c4d0",
    "src/omega_native.h":"fefcef6fdc7388868212313803c49999fb683b3bd68d2556f3cd025995c35baf",
    "src/omega_api_decl.h":"0381bde581775ad93c3fedea92f7a92a93016814762f9d163e98cdfae11494f8",
    "src/omega_om_units.ident":"d89c083840cd8dce590fd8e90c1bdbe54dc86aa5018ce27407c8736743e40756",
}
STRICT_OMC_SHA256 = "d6290c7663c2b4bafb59eb3fdf812476e977f4c144def71095c6248ef9b96b84"
STRICT_NATIVE_SHA256 = "c77a5922f91d53d4a0bbd4e7b1c4703f7098263abc843be2918db1ece7496782"
Q5_DRIVER = br'''#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
uint32_t omega2_vcnt(uint64_t *, const void *, uint64_t);
int main(void) {
    uint64_t r = UINT64_C(0x5555555555555555);
    uint32_t s = omega2_vcnt(&r, (const void *)(uintptr_t)1, 0);
    printf("status=%" PRIu32 " result=%016" PRIx64 "\n", s, r);
    return 0;
}
'''


def bridge_plan(root, name):
    """Exact original C-boundary obligations, not composed-main substitutes.

    The runner executes the shared original driver once per architecture and
    records each named line separately. Mutants change generated scratch C only.
    Gate negatives run the unchanged original script with its exact old tools;
    fresh unmodified regeneration is a required positive control.
    """
    review = DIRECTIVE_REVIEWS.get(name)
    if review is None:
        return {}
    label = name.rsplit(":",1)[-1]
    if not label.startswith(("abi-","sha-")) and label not in ("mut-e-ident","mut-f-abi-len"):
        return {}
    _pinned(root,name.split(":",1)[0],review["manifest_sha256"])
    def resource(path,sha):
        _pinned(root,path,sha)
        return {"path":path,"sha256":sha}
    units = [resource(path,sha) for path,sha in BRIDGE_UNIT_PINS.items()]
    evidence = dict(review,review_revision=REVIEW_REVISION,
                    boundary="actual generated SysV C bridge; private-body coverage remains separate")
    common = {"selector":label,"units":units,"args":(BRIDGE_UNIT_PINS[units[0]["path"]],
              BRIDGE_UNIT_PINS[units[1]["path"]],"$COMPILER_SHA256"),
              "cflags":("-std=gnu11","-O2","-Wall","-Wextra","-Werror"),
              "driver":ABI_DRIVER[0],"driver_sha256":ABI_DRIVER[1],
              "resources":[resource(*ABI_DRIVER)],"expected_exit":0,
              "required_line_prefix":"PASS "+label+" ","cache_key":"abi-v2-original-driver",
              "operation":"c-driver","evidence":evidence}
    if label.startswith("sha-"):
        common.update(units=[resource("om/sha256/sha256-om2.om",CURRENT_SHA256)],
                      driver=SHA_DRIVER[0],driver_sha256=SHA_DRIVER[1],
                      resources=[resource(*SHA_DRIVER),resource(
                          "seed/qualification/om2/oracle/s5s7/sha/expected.h",
                          "26771c6e1033babb8736dad7751e3a7bd69161779462486e623d91166b4a6495")],
                      args=("big",) if label.startswith("sha-big-") else (),
                      cflags=common["cflags"]+('-DGENHDR="omega_api.h"',),
                      cache_key="sha-original-driver-big" if label.startswith("sha-big-") else "sha-original-driver")
        evidence["effects"] = ("caller-local arrays, stdout; big mode additionally read-only anonymous "
                               "MAP_NORESERVE 4294967299-byte zero mapping, released with munmap; no file/device/network")
        evidence["oracle"] = "original sha_driver.c and pinned expected.h; no rewritten digest expectations"
        return common
    if label == "abi-misaligned-len0":
        common.update(operation="c-driver",driver=None,driver_sha256=digest(Q5_DRIVER),
                      driver_source=Q5_DRIVER,args=(),required_line_prefix=None,
                      cache_key="abi-q5-native-behavior")
        evidence.update(contract_status="Q5_UNRESOLVED",oracle="exact pinned native bridge behavior",
                        evidence_kind="differential_only",scope="differential_only",
                        conformance_status="UNASSERTED",original_not_run_reason=review["reason"],
                        observation="both uint32 status and full uint64 result sentinel, pointer1 length0")
        return common
    if label == "abi-v1-absent":
        common.update(operation="negative-link",
                      driver="seed/qualification/om2/oracle/s5s7/abi/v1link.c",
                      driver_sha256="a50ceb8b1ec9bbe1dfd824e680d5af62bc1a29673ea797e5566c9d4b94549f54",
                      resources=common["resources"]+[resource(
                          "seed/qualification/om2/oracle/s5s7/abi/v1link.c",
                          "a50ceb8b1ec9bbe1dfd824e680d5af62bc1a29673ea797e5566c9d4b94549f54")],
                      args=(),expected_exit=1,undefined_symbol="omega_vsum",
                      forbidden_symbols=("omega_vsum",),positive_control="abi-v2-original-driver",
                      required_line_prefix=None,cache_key=label)
        return common
    if label == "abi-v1-kept":
        common.update(operation="symbols",args=(),required_symbols=("omega_madd1",),
                      forbidden_symbols=("omega2_ident_abi1",),required_line_prefix=None,cache_key=label)
        return common
    if label.startswith("abi-units-stale-"):
        identity = ("unit=sha256_om2 abi=2 profile=OM-2 src="+CURRENT_SHA256+
                    " omc="+STRICT_OMC_SHA256).encode()
        mutations = {
            "abi-units-stale-source":{"path":"om/sha256/sha256-om2.om","append":b"note strict stale-source negative control\n"},
            "abi-units-stale-omc":{"path":"src/omega_om_units.ident","old":identity,
                                   "new":identity.replace(STRICT_OMC_SHA256.encode(),b"0"*64),"count":1},
            "abi-units-stale-profile":{"path":"src/omega_om_units.ident","old":identity,
                                       "new":identity.replace(b"profile=OM-2",b"profile=OM-1"),"count":1},
        }
        common.update(operation="units-check",units=[],args=("--check",),expected_exit=1,
                      resources=[{"path":path, "sha256":sha,
                                  "git_revision":"6999fa9e6dae4847c155ef4eacaece74f491d3ca",
                                  "git_path":path} for path,sha in STRICT_UNIT_PINS.items()],
                      script="om/units.sh",script_sha256=STRICT_UNIT_PINS["om/units.sh"],
                      strict_compiler={"git_revision":"6999fa9e6dae4847c155ef4eacaece74f491d3ca",
                                       "git_path":"seed/out/omc","sha256":STRICT_OMC_SHA256},
                      strict_generator={"git_revision":"431fff0a95f84c0a8bf9d3383f94d7e1f02d913e",
                                        "git_path":"src/native.om","sha256":STRICT_NATIVE_SHA256},
                      mutation=mutations[label],positive_control_expected_exit=0,
                      required_line_prefix=None,cache_key=label)
        evidence["scope"] = "unchanged original host regeneration gate, not execution of historical AArch64 bindings on x86"
        evidence["effects"] = "private copied tree, private temporary directory, pinned compiler/generator, cmp; no production source writes"
        return common
    if label in ("mut-e-ident","mut-f-abi-len"):
        if label=="mut-e-ident":
            mutation={"old":b"omega-ident 1 unit=abiv abi=2 profile=OM-2 ",
                      "new":b"omega-ident 1 unit=abiv abi=2 profile=OM-1 ","count":1}
            failure="abi-ident-abiv"
        else:
            mutation={"old":b" || n * es > UINTPTR_MAX - lo)","new":b")","count":1}
            failure="abi-golden-wrap"
        common.update(operation="bridge-mutant",mutation=mutation,expected_exit=1,
                      required_line_prefix="FAIL "+failure+" ",cache_key=label,
                      positive_control="abi-v2-original-driver")
        evidence["mutation_scope"] = "generated scratch library only; original C driver unchanged and must reject named defect"
        return common
    evidence["effects"] = "private caller arrays, stdout, local SIGALRM handler/timer for original reentry control; no external target"
    return common


OBSERVATION_PINS = {
    "seed/qualification/om2/sec/s0s2/s1/d-mid-short.om":"b12cc48ac207fef5c86c8406a971b4ec2596d493be516c477a32ade42dd58200",
    "seed/qualification/om2/sec/s0s2/s2fmt/hdr.txt":"c5f1a15eb746bb6e0473518946c3193dce0b0dbf50ed56b5d9151fefa874d5e9",
    "seed/qualification/om2/sec/s0s2/s2fmt/bad-trailblank.om":"4f9083797899a877907e1bd6697c85723c25e85be2630d5c807261ce732569d7",
    "seed/qualification/om2/sec/s3s4/fold-trap-diag.om":"b74f2ab21a3c441563a39474d3c962946e18bdee53120f077378fb7f2bab28f5",
    "seed/qualification/om2/sec/s3s4/lib-pu.om":"28883f52212233aa4bc76182dec3733ad550f209a109a7bd57f5c4227a373e10",
    "seed/qualification/om2/oracle/s3s4/exp/fold-trap-A.out":"94d59b76e5b10f74d285313f8c69cc4799f538481dbc05edb8dc62f7a59f6eab",
}


def observation_plan(root, name):
    """Execute reachable original probes without inventing unresolved wording.

    These are exact native-compiler differential observations, not a resolution
    of Q2/Q3's human-line ambiguity or an architecture-independent stub count.
    The original public-contract limitation remains in every evidence record.
    """
    review = DIRECTIVE_REVIEWS.get(name)
    if review is None:
        return {}
    _pinned(root,name.split(":",1)[0],review["manifest_sha256"])
    label = name.rsplit(":",1)[-1]
    def read_source(paths):
        return b"".join(_pinned(root,path,OBSERVATION_PINS[path]) for path in paths)
    evidence = dict(review,review_revision=REVIEW_REVISION,
                    contract_status="UNRESOLVED_ORIGINAL_WORDING",conformance_status="UNASSERTED",
                    evidence_kind="differential_only",scope="differential_only",
                    original_not_run_reason=review["reason"],oracle="exact pinned native compiler observation",
                    effects="compiler diagnostics; program probe additionally stdin byte and stdout only")
    if label == "s1-d-mid-short-line":
        paths=["seed/qualification/om2/sec/s0s2/s1/d-mid-short.om"]
        evidence["source_pins"]={path:OBSERVATION_PINS[path] for path in paths}
        return {"source":read_source(paths),"operation":"emit","driver":"contract-observation",
                "expected_compile":"4","expected_exit":4,"evidence":evidence}
    if label == "s2-fmt-bad-trailblank-line":
        paths=["seed/qualification/om2/sec/s0s2/s2fmt/hdr.txt",
               "seed/qualification/om2/sec/s0s2/s2fmt/bad-trailblank.om"]
        evidence["source_pins"]={path:OBSERVATION_PINS[path] for path in paths}
        return {"source":read_source(paths),"operation":"emit","driver":"contract-observation",
                "expected_compile":"6","expected_exit":6,"evidence":evidence}
    if label == "program-site-table":
        paths=["seed/qualification/om2/sec/s3s4/fold-trap-diag.om",
               "seed/qualification/om2/sec/s3s4/lib-pu.om"]
        expected_path="seed/qualification/om2/oracle/s3s4/exp/fold-trap-A.out"
        expected=_pinned(root,expected_path,OBSERVATION_PINS[expected_path])
        evidence.update(source_pins={path:OBSERVATION_PINS[path] for path in paths},
                        stdout_sha256=digest(expected),stdin_hex="41",
                        compiler_stderr_comparison="source-site-records",
                        compile_observation="original source-level site records; raw target-specific runtime-stub tables retained as observations",
                        site_source_lines=[20,23,39,44,49,54,59,64,70],
                        runtime_authority="candidate.cases fold-trap-A; constant divide-by-zero at first unrolled iteration")
        return {"source":read_source(paths),"operation":"run","driver":"program-site-observation",
                "stdin":b"A","expected_compile":"accept","expected_exit":102,
                "stdout_oracle":"file:"+expected_path,"evidence":evidence}
    return {}


def bridge_positive_plan(root, plan):
    """Original, unmodified ABI driver for link/mutant positive controls."""
    if plan.get("operation") not in ("negative-link","bridge-mutant") or plan.get("positive_control")!="abi-v2-original-driver":
        raise ValueError("no reviewed ABI positive control for this plan")
    positive = bridge_plan(root,"seed/qualification/om2/sec/s5s7/candidate.cases:186:abi-vsum-ok")
    if positive["units"]!=plan["units"]:
        raise ValueError("negative control and positive control unit identities differ")
    return positive
