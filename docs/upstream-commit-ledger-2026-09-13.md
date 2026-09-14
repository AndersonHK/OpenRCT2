# Upstream commit ledger — 2026-09-13

Planning ledger for [the port plan and decision register](upstream-port-plan-2026-09-13.md). Start with the [gameplay and legacy-design review](upstream-gameplay-review-2026-09-13.md) for behavioral choices. Implementation has started; the status section and migration log supersede the frozen planning status.

- Fork: `e874ceb7708974e00040419d30390686c52a5d31` (`develop`, identical to fetched `origin/develop`).
- Last integrated upstream/common ancestor: `69872010ae6b0febcd1b9b6a53f49e54968ceecc`.
- Frozen target: `15d4b5e933555913d216f4548f673cd55cfb0579` (`upstream/develop`).
- Latest fetched release: `v0.5.5`, `8694e3483690323b6a75fa7264b6c58116f51f31`; develop changelog has begun 0.5.6.
- Complete ancestry difference: **361 commits = 324 single-parent + 37 true merges**; fork-only count **45**. First-parent upstream count is only 166 and would omit constituent history.
- Net upstream delta from the base: **1,818 paths, 42,139 insertions, 39,750 deletions**. Fork delta touches 457 paths; 232 incoming commits touch at least one of those paths (including merge deltas).
- `git cherry` marks all 324 single-parent commits `+`; this does not rule out fork-native semantic equivalents. The 1G reset is one such equivalent.
- Isolated `git merge-tree --write-tree` reports **140 conflicted paths: 125 content and 15 modify/delete**. It wrote temporary objects only, leaving the index, branch and working files untouched.
- All 37 true merges have an empty `git show --remerge-diff` at this Git version: no additional merge-resolution patch was found. Their constituent commits still require accounting.

## Implementation status

All recommendations were approved by the owner on 2026-09-13. The newer historical, one-source-per-receipt loop and batched validation supersede the original grouped plan. See [the migration log](upstream-migration-2026-09-13.md) for decisions, behavior and pending checks.

**Source receipts recorded: 294 / 361; remaining after the newest receipt: 67.**

| Source | Disposition | Fork receipt |
| --- | --- | --- |
| `e555c76500` | manually-ported | `df0ec5b8f0456c50ce0040363c85d3d57aee0a60` |
| `d81c3a7f14` | manually-ported | `f92a9caf94cfc9a665c9d0cc3034a8b53b401edc` |
| `d181e640ac` | manually-ported | `713147f52983f3f799293e102601ce363bdb6376` |
| `a4f0d22988` | manually-ported | `68897c3b5878dd15f7fae2e9d42251c0fdce10aa` |
| `775c4e7635` | manually-ported | `75891b0b9dd203b1b4e1493df8a044900f91dffa` |
| `9198ff13f4` | manually-ported | `a458814859a33b9f4ed3cf3e14c5563bd0946b3c` |
| `cd44174f9b` | merge-no-unique-source-change | `956cb285b3f29f053ba21c6ee749c3c65715f965` |
| `4a7ee146ca` | manually-ported | `fe03c6d1f0703eddbd7770a1fbb674dbfe19f3c0` |
| `31760893d6` | merge-no-unique-source-change | `c9f5ebc4990c359142c032c061e9513977693f6e` |
| `f475c5b949` | manually-ported | `c9446e6d6f178155a8680fcdc6dda5fb34b70a59` |
| `c51e3a3e40` | manually-ported-with-fork-adaptation | `27aa32a78764407a27ab28aaf9220915b067d4a0` |
| `c35f71131f` | manually-ported | `357b09dc4b159203934e2cf22c14c27b472f5f68` |
| `ea084b9828` | manually-ported | `13c1801e0596db46b83d64314a9324a83238b912` |
| `b9484d7f48` | manually-ported | `49fa37546a45c136eccb16ea688455fdda9b11fc` |
| `89b9f7d64c` | manually-ported | `ecd9dfd1bbdc52e6ea6ccb1e73d4f28b973c2778` |
| `7339f6eba6` | merge-no-unique-source-change | `543cbdfa279ef9fb12f6762c05bb8e715d21f622` |
| `b8cf2f8c35` | manually-ported-with-fork-adaptation | `9f0411fb13f741c9b5137ef1bc2cee21487e62be` |
| `430bb95b63` | manually-ported-with-fork-adaptation | `2886403bc27edc170a91dc16422a5b5e65d2b6d9` |
| `d517f19ce2` | adopt with fork behavior preservation | `1d1c5bbb7e59f63c77ee54be5a2187338e28244a` |
| `0284f9cda6` | adopt with fork call-site adaptation | `cde0dcdd1b37670964a6ebb70d8d05736d3f8d2e` |
| `77c69cb55d` | already satisfied by U019 adaptation | `f1c21f7e6598552653a24cd6d99328804682c5ee` |
| `7df1cfe7c0` | adopt | `e7919f4dbafbd6ef5dedc7f90fe299240f2edbab` |
| `2ac48e6c42` | adopt naming in fork mixer | `7ef9085dd3c81489a5c79d2e72855c674c651a16` |
| `c80a0e168d` | adopt | `04df30180503c2d3ab5c97e7c8bc7f656c72dc4c` |
| `a7881846e1` | adopt naming only | `74162ebe200b794c4cbfdbac349147dd3bc637e4` |
| `e80faf5606` | adopt naming only | `5601a0cb564e283023d1c715467ed9643de1a8e6` |
| `abbbb251ee` | adopt naming only | `1493a5714809c2421c6552968f6c42830b86e738` |
| `47c1bf9166` | adopt naming only | `e6eb2367226557e399fde0578c0481e09741bc9c` |
| `b2375341bb` | adopt naming only | `022ea2310fdea6b954c6c3e96a8220bc55a80755` |
| `2ed6bef643` | adopt naming only | `0342b582ae16f9fe696f9dc852cf5074e0024896` |
| `46dbc1cf20` | adopt naming only | `276955b2f9702a8db7668b51e8a540c204b1e04c` |
| `e1f2c365c1` | adopt naming only | `7d13c4ef2f15a8f4c27575ffb6916974c514e399` |
| `0ab5cb5c49` | adopt naming only | `00ab8534baf3817c1d5c950a813f17dc4bcf3fd5` |
| `c728e66032` | adopt naming only | `5d41788fc1365b6e82aea60538a4839ec5e1c31b` |
| `9c5057a524` | adopt naming only | `ace6742170ae135f5a5b6c23e13fce0bde8067df` |
| `8b9bde1f54` | ancestry receipt only | `8a80e52197a864e6ee65ed6498035ac9fb1bc3e6` |
| `799002a820` | adopt | `169f32a42e15a64187d2d2693d0b0aa83461b14b` |
| `b782a7dcc9` | ported; companion-fork dependency correction | `5975998dbe8b34d21ab753db8c79f23d854163fb` |
| `a3f7b5d3b0` | ported | `ef5ecc9e9b1d8141a101b6b89566eae1e940c93c` |
| `d26b161668` | ported | `a1ea0a886f247d5c16a130745d6331a87889472d` |
| `4bf86c6e6c` | ported | `ea3d283a3c53226f739b0052d77db358f738af0f` |
| `0f620b0714` | ported | `27b6d06a891b045062556de3670ce38e993a0fba` |
| `082994cd52` | ported | `b3ae95f5337e4a285c52b1d85f5fe228f14b301e` |
| `bfd0a374ff` | merge receipt; already ported | `a7d9dfac77243731a13d864e53564dbe83393a8f` |
| `302058221f` | ported | `075ebd0d7cc0221e721082e3052471c8e84d3efd` |
| `1423ab779a` | ported | `781ea6d87e8dda57533048964ba9a5b00b217294` |
| `bcd7708dc1` | merge receipt; already ported | `20ada0ae25c0d20d767ed9baa383bd9a524205d8` |
| `40b7741b58` | ported | `2229c6e46acbede1dd46c7ad72f477bbd048a853` |
| `26b28153bb` | adapted | `ca39d23303ff95df98226c8d09d161a3f50df914` |
| `8bfe55af63` | ported | `128e4489257bf8f1014fd991cf09faeeaab03d93` |
| `687fa7181d` | ported | `4486080d551c9da282583368b1c3521f2ec8e4c0` |
| `e103cb11bb` | adopt naming only | `7da651bc9d8a70220a1c285ef6f47bda8611d437` |
| `3fa93c3278` | adopt naming only | `71aaa996c67e1580f65fc7c254c9e718aa7174d2` |
| `068f68e4a9` | adopt naming only | `854299910df3b56cd5f668c8a7d6cdbfe0cd2813` |
| `4dbb57a87a` | adopt naming only | `9e3032d0d24f7d63a08fb4717c037aa2a337323b` |
| `1b63339ef7` | adopt naming only | `82acab715bf93307496c03a4fe1d597c08ba8924` |
| `1e2817a5e1` | adopt naming only | `34bf25f0e7e9ac38d6aaf65bf53d2a4cf0d4a7fd` |
| `8c3ad47b92` | adopt naming only | `8992695c2b5498c8a683cf3225f5c55b690355aa` |
| `45007c1865` | adopt naming only | `30ef750b2f985b899e645914080d8767e7441ba7` |
| `3a36fc1c0a` | adopt naming only | `d8df9a70eb8369b55cb83f8887d0f3b57f703f7c` |
| `959401a3a0` | adopt naming only | `bb1798e6a4508f584718b5702d03bae6a8ce3d5b` |
| `0d0498a081` | adopt naming only | `775be3abdb92559187c244839abcfba8cbceb366` |
| `2769a3ff8c` | adopt naming only | `043e1849b1050db7678100fb00e0898dea745578` |
| `331dd2670a` | adopt naming only | `87181081ccd71bedde97b9de11362d46a4f13c60` |
| `17f7713603` | adopt naming only | `ea1c304157cb79a23a8e7482810df70e42a1ca6c` |
| `5a0aa69409` | adopt naming only | `74f43922a539132bd0a992f189d2e02a4219aadf` |
| `e89ab11a6a` | adopt naming only | `952627bca2ab606710ec8b0b124ea40b30d05551` |
| `882467fe20` | adopt naming only | `a631f0646f36803842ff83de23996979d9e9039d` |
| `cb13ddba48` | adopt naming only | `f6c2de3d944a2e8426ae45241e7cf6a12a36ae99` |
| `95cf306886` | adopt naming only | `ef6c6c80b82bec0f5e4e063599be4c2077550a30` |
| `275e6597a7` | adopt naming only | `dd2128ca15b4617ac5ddfb14256dbefe917c7a6f` |
| `546cd2dab6` | adopt naming only | `01463a05f289d3d7df8b2ff547b4fc027c866c0c` |
| `0c4322a982` | adopt naming only | `a632f702ce90ce9f688865a8531646b6f732b25b` |
| `c166b42a67` | adopt naming only | `70e21da941dbacbe7e090b155597778fb82ffdcf` |
| `8ae5221c6b` | adopt naming only | `503dbc5f54e783760010ff86e5dd1d08ed60dd84` |
| `6d8c65ba5f` | adopt naming only | `7570bb48ff9fefb979369b7a8adfe245e12a2b43` |
| `45bc989f1b` | adopt naming only | `8986d7bfa96ca7f9c874cc23b6d38616fe479c66` |
| `086b5ecabe` | adopt naming only | `6f3221651a0ceac403b3c181c3833fb9fc018437` |
| `25df52f1bf` | adopt naming only | `9637f95d15aced9ee80c799d6bd1d7a4998845cb` |
| `d0db940bde` | adopt naming only | `6e62cb38cf7466450a7f5ad0f0110e3f55a29a66` |
| `1c4daab873` | adopt naming only | `998cdc15f57e58c4ff3f2cd5aa9d63db9b68aa51` |
| `e6cc15f033` | adopt naming only | `6ca16cce29bb9bc8db00065178667530bdb930e4` |
| `b0e7d321e3` | adopt naming only | `bc14c9f3580cbab0f607b89a3b2292ec5646ff4b` |
| `a850d78a7d` | adopt naming only | `11ae635a477969c876dbda5ebd1743ca4cbb11fb` |
| `a6175f8930` | adopt naming only | `851e6dbe0e694bdec5ba6dc22049b9fbad889005` |
| `53f5a41a74` | adopt naming only | `d5cd7af071fd8b09c16488258871748806ebb7e0` |
| `9bd1e0c858` | adopt naming only | `27211dc0580cc39de5f06bc55f4429d46fc71d05` |
| `76e75e687f` | adopt naming only | `44f3170c89ab2ac07c08d8a121b0acb1a1938ac1` |
| `41164a7453` | adopt naming only | `11cdaefc1a1ecd4c00fc10502f410e7cde47ee8f` |
| `4502be0fb2` | adopt naming only | `e219dbb097b49d7ec369ab7f1dc1931cb283b31c` |
| `5ba0b8028c` | adopt naming only | `45426d71c1260eef7e9587400b443d0c9d74e8f5` |
| `780a135087` | adopt naming only | `7cede9e610c2c91c0437f048f6e3201505c59d10` |
| `f256f03fff` | adopt naming only | `04a153056d2d2d94c075e4c1e7ccc79c5c626696` |
| `bd4a9eb0ef` | adopt naming only | `052d74247f7f9fbeb13c0daeef96762167f42a63` |
| `7f879409ac` | adopt naming only | `72b586364afeca68b9ede762fc8bb2335e10cb43` |
| `28ad2fcaa2` | adopt naming only | `d8bbb8b475f5937fe1b6578a6cd30844493de7fe` |
| `991fc7b4a1` | adopt naming only | `1d39f177d825b0f544eca1ba7acbcac0e1600b1f` |
| `0d8af50f45` | adopt naming only | `6313a20bf868fca3fbf385dc1464622867c34318` |
| `9c830feef5` | adopt naming only | `2daf82d6dd24306a0df5bb0517516b2681610123` |
| `b229ff8f60` | adopt naming only | `eb37bbc80c90126d9c9c11ad36f91943b3274f69` |
| `ae5dc4422a` | adopt naming only | `5b0d349882d2f32962c11d99dadf1fb527f27a98` |
| `b5a92d9296` | adopt naming only | `eb7fecd241ff327543bb7ba7dfc93add732d1046` |
| `78fb8d73bb` | adopt naming only | `05bea6ca18c140aca87d848dd0c3ce96f04f57c8` |
| `25fc422c44` | adopt naming only | `47796ba94619b34fbb61336b8b69236e3a6d5b98` |
| `38bce48524` | adopt naming only | `96efffcedd1825460551a9d951e3fb20995a49e3` |
| `a5892a2d17` | adopt applicable changes | `ce868d41f6c665b6a349850e11c6157718b4eb33` |
| `228c4bfb34` | adopt applicable changes | `8f8c39badff2656e712cc14543f71e4d3b72bcce` |
| `205497a480` | history receipt | `8b96fd8f802f7c17479dc5380607a6145dbd8c3d` |
| `7f39bdebf5` | adopt applicable changes | `a08b88d1a2a3ebd3f8241616876abd9347bfcf2c` |
| `b3ef890f38` | adopt applicable changes | `d543575a66cff59e73101f7c8d52763a65b80999` |
| `7f8fc39ff5` | adopt applicable changes | `c270e7ae163fe8eeeebf1c8d4354b65d02339909` |
| `5c0caf461a` | adopt with fork header dependencies retained | `cc475ae5bbf43562fbf7ef321d38e26a3a8bc9c3` |
| `6323d95edc` | adopt header cleanup with direct fork dependencies retained | `552188767c33a2915ab44489d915354cc02e9111` |
| `53a81b1b45` | history receipt | `f7ac048b68a050524e98ced537d51008bcc37a34` |
| `51649e5873` | adapt namespace refactor | `58609494400c74813795fe49aa9010321a5fddd7` |
| `4087c9c6b9` | adapt header cleanup | `592235f42d829b3083328e750d3dad6445481012` |
| `15b36d6ced` | adapt window type/header ownership | `c8c6c49920b4eab3f688e75c6217947490dd6a33` |
| `9c59a4c5e8` | adopt applicable changes | `ebca1bd84d32a3341ec7f0f187ce215408300dde` |
| `536973e9e6` | adopt applicable changes | `5f69d7b4b691ebb476244c4ab4d5a9a842e8e48f` |
| `ac8b290671` | adapt wall flags | `2b47fc47c7c8c3a30f85862b80981d1f96f5d848` |
| `94a889e4c9` | adopt applicable changes | `a0c1e1ce8275908d628b0334e2ce27c68e5d12b8` |
| `c2c126da9c` | adopt applicable changes | `d1dce1682d9bae8be186268d328e1d87e82b68f0` |
| `263cb53047` | history receipt | `3bb88b05112330292f930290c863444128f5848a` |
| `5a814722e1` | adopt approved D10 with fork protocol adaptation | `68226604c5923eb5429ff7cfe3ab27a42040f7f8` |
| `42885ec965` | adopt applicable changes | `e51a0764fe69912eebeddd8d176ea22bcc58c265` |
| `d7d8915065` | adopt applicable changes | `256db7e697e1d97276f3c3ab0e1ed2d7e19ede75` |
| `f91e2d680b` | adopt applicable changes | `d3a84032e00858486e9e5f1bd4eac16814c89cfc` |
| `f4ddda3f69` | adopt applicable changes | `3a15714082b3b6f83482ced6de73f471f53719cf` |
| `f967ebc2d2` | adopt applicable header cleanup | `ea5e9a6da14b6da5e7ba35546c605ee9bbcc2f69` |
| `6c5c2079ce` | adapt script header cleanup | `4c6cf4bdbeb1b71010e587f0faa3a2029cf081c9` |
| `3c375c60ba` | adopt applicable changes | `7823231e25a13abf729d9dbbb8ce4a486926d493` |
| `8cd44cc686` | adapt world header cleanup | `112e9f4af4ea2611e8823f14ad2a000634baede6` |
| `8839956721` | history receipt | `93185c989637bf2091daf94963b91878744da82c` |
| `11bf58dceb` | adapt clear flags | `ddd09a1ca588b2acae4fd2b98e084edabe1a4016` |
| `a70874710a` | adopt with fork-specific flag callers adapted | `a68f07dd91f6f145b4989f40f1e94e1a1393993b` |
| `f08bc61f03` | adopt typed flags with fork payment/rating callers retained | `02dfaef98638a6f285a183645ab262172e24fae7` |
| `5aa3f85c56` | history receipt | `301e6cc84cd65f1759143910f24da130ed6c8fd3` |
| `2e43967fe5` | adopt approved D03 in clearance-cheat mode only | `216701c39ac614a8b63b1f7fde6ce20fba03f764` |
| `4c1cedbb08` | adopt secondary flags and separate validated door sound | `03ba7ecf75fbbc4781a0d8963ae613fc5c37d23b` |
| `d4c67d9b8d` | adopt applicable changes | `a122ba9e8c192d24e23ce511f71493c9ec88f8b9` |
| `8b4b1c6561` | adopt applicable changes | `d537a2ee5416017d38a6a40388ec36b78dd72db7` |
| `bb0b26e3f2` | adopt applicable changes | `38415e70f465f52fc2fba10991723207f128ccf3` |
| `5d3903209e` | history receipt | `ccb38d4f6ae6c7723b96147ac35f8ec885c51cb0` |
| `5c0fc86397` | adopt applicable changes | `12959e5113028b03130aea9e2f8089caffd561c2` |
| `a629158a4b` | adopt applicable changes | `49df23eafd03e1acc90f9e8bca399f1d4d4a80d6` |
| `24869300df` | adopt applicable changes | `ec071472bc48ab54092d2c751bbceaa68a3dee4f` |
| `bd935d75c9` | history receipt | `9df017697391c906e31dc6961cdefaf89e84d97a` |
| `a187bc9550` | adopt applicable changes | `74108ad686e00486e322d61a7c586310c23656a7` |
| `4e576a04ad` | adopt applicable changes | `7c48c88804099dbe3718e33ce5f15d51caa8478a` |
| `5ca2c1845c` | adopt applicable changes | `c0c5209dbdf1e62984f3bb6a11232816c9c5d2ef` |
| `e6be313f1d` | adopt applicable changes | `8079602451ba0bac7c038e3915c42b79bae57e85` |
| `d6863ccf99` | adopt applicable changes | `67f46bb34e48ef39ac71c42ae6320de0311d23b2` |
| `d9c423b1d6` | history receipt | `313324f5f74cd62fd817d0aac4c0052c1c16e108` |
| `bdbe9013cc` | adopt applicable changes | `b9e11e3a6a01a98699640b6d6e17a61b7eb89f94` |
| `ac2515d483` | adopt applicable changes | `f420db6abd56fcaf88923b65611cf8eeb7ecf181` |
| `be98df4081` | adopt applicable changes | `8ecda6556e9877e4cf394c9d0e69f37673884f2d` |
| `4af2e9d1fd` | adopt applicable changes | `45743f848cfd898be0fef054ea30d36df02eaaec` |
| `95025ba916` | adopt applicable changes | `ecb04cea7677ed30cdc925a1c6a6f67f7797a971` |
| `2f9db0a5d4` | adopt applicable changes | `7824f3ae20001f8b704255105360ba12987fa394` |
| `d3c54532ca` | adopt applicable changes | `0d6af1ab30e867cf690ba586750ce2cae230bfb4` |
| `25865dce31` | adopt applicable changes | `eac6c509180a437f16dd650e05d6ea1207983221` |
| `84f1946db8` | adopt applicable changes | `50ab4378af4acb5b25b3a5fb53013f8fa862a887` |
| `631804fd13` | adopt applicable changes | `060a4bf13790f2d6488316a62f71d0ad232cd26e` |
| `e501376aa3` | adopt applicable changes | `18b9091c7445d7af48c4dda8440366732e7d7d06` |
| `a80faffd5c` | adopt applicable changes | `0ebb380961b4dc012313144789e62e752046e192` |
| `2c8891ca8b` | adopt applicable changes | `92489d5a36efc4e3103e0ad23712d12f80b7e596` |
| `e75c1dde15` | adopt applicable changes | `8999239cc242d36211662f5a939dec8a02bfaf3a` |
| `8a0507708f` | adopt applicable changes | `035aacb55efb15c190d66d6383fbae02be2926eb` |
| `b95d24bb68` | adopt applicable changes | `12ba72078c2fec4767cda2dc259ddf678b4b7cc2` |
| `34e8149e93` | adopt applicable changes | `548433401d8b41bd8f391b52c075a8878a5673f5` |
| `bc68828c7d` | adopt applicable changes | `b56d43dbd3ef6cf8cd568aa8f786c832c82b0eea` |
| `cef7dd164b` | adopt applicable changes | `bfd04b63296b4f2602d0a4959bbaea600b9eb373` |
| `d943171fae` | adopt applicable changes | `3263b9bae218356612b7e000109804864e8092c3` |
| `7b1b014fce` | adopt applicable changes | `c0636090860f48528ec53c6114e7bab94369e103` |
| `4ebed48631` | adopt applicable changes | `7aa3675ceed20ed7532618aea860f7f2fbca1c17` |
| `b854b8d2f6` | adopt applicable changes | `c4ef1030819f49d399157b30dd6993d5d35a88ed` |
| `1bf29b9afe` | adopt applicable changes | `3b2263c3ede1b91d555ae941f6df5a81ec231fd9` |
| `4d7866e3ad` | adopt applicable changes | `d23c36a9287aad2265f76daf45905476b5b3c60b` |
| `5af73fa31b` | adopt applicable changes | `a7ced1c200cb55e206c22dc97009e55ff9340507` |
| `1eacb1eec8` | adopt applicable changes | `4b903899e27871ebda783c38f02be486158025aa` |
| `df06376780` | adopt applicable changes | `a67e1db0cfd917d653f5de7081cb331312e54fa7` |
| `3937aaa967` | history receipt | `73751da44addb22c52b958688b42d3480ceb0077` |
| `95b54aa4f3` | adopt applicable changes | `7393218a28bf01df37f7fae2ea165cc80b02e9f4` |
| `27a9d4b3d5` | adopt applicable changes | `cb64f95e31ba14103e784a4bee1fb0433742c11b` |
| `11547e998a` | adopt applicable changes | `36efe1015daf62f520064d03681264973d2fb7b0` |
| `db53fd3f7f` | adopt applicable changes | `5000db0b9b332b9eb22a60511981412a362e055d` |
| `d2783e0564` | adopt applicable changes | `3ffd12141b33079ea80e494c1451b4b4fe37156b` |
| `26c10680d5` | adopt applicable changes | `e24adc33e799f7bb4288ecae214147c72a9cde07` |
| `11da390704` | history receipt | `97b52a3a06bfe23dc007980671e0b1ae0e896f1e` |
| `3d90510a63` | adopt applicable changes | `0b81367e16aee13cf6c3b63ab0bae7892de86a22` |
| `182efeea9a` | adopt applicable changes | `7b719dc96c4e3d9e93ffd39475399ab8d4a852aa` |
| `8ebb607fc4` | adopt applicable changes | `611589b1fb94a1e9bf62b2c2a3de88b127db4c38` |
| `af352f18ef` | adopt applicable changes | `599c6e0cf12dca6c16a313acf328469f609b0168` |
| `8ebe3965a2` | adopt applicable changes | `24934aae0dd7a638ce733958b8a00c171507c491` |
| `8dab2cce07` | adopt applicable changes | `86deabab870645ece1761848051a0578ec1a52d8` |
| `ab3065fbe1` | adopt applicable changes | `6077ca4edd4f53c7242e83daf0004dbe90bc9a03` |
| `b5c1570b78` | adopt applicable changes | `b5d769173b00f50802680ec74b3e20e44fb509a8` |
| `f5f4b522c7` | adopt applicable changes | `3c6def531a1805fda0d4398b712c7ce82e20a012` |
| `a74f7437b9` | adopt applicable changes | `e2b2eb90530ba8c82f4cb9dfac42931ba6725de8` |
| `bb0215abb0` | adopt applicable changes | `17250edccdfb2b961549e65bfbc763b669a719b9` |
| `840f1c4f91` | adopt applicable changes | `031f00dfd5327374b463e77069796d53c2b1d01b` |
| `8de0f10777` | adopt applicable changes | `4ab943d26ceb888a63135f5a336454c2a1504fc6` |
| `ad578a121d` | adopt applicable changes | `e0f8987dcd718c028711eb16eb1f8071e3edac6c` |
| `e25c301668` | adopt applicable changes | `6fa1afb1d477afe0c1f5eb30008f50efb89dccb3` |
| `ac926d5550` | adopt applicable changes | `086b4effe2c8ac284b0f7e32c821b8784c0a1f81` |
| `6b2fb43861` | adopt applicable changes | `b65b08504b0761ef22e3ee4ea644c3ddc43029ce` |
| `3768b08999` | adopt applicable changes | `300af2ea32b2beded7333a68ea0f82cd22a72af3` |
| `d1ea87512f` | adopt applicable changes | `48e756b87fa48d1271aec50a8e78d9003cb5eb7e` |
| `911afdbd6e` | adopt applicable changes | `37d60287a0fe0f876c2f958115840c3428747e32` |
| `c34fee484a` | adopt applicable changes | `123d6bb6af4202c520a98801915d4f6de6a83814` |
| `34762ceb81` | adopt applicable changes | `09da873e8a2738526ff15afc3284f6929d9f5aa3` |
| `4cce29b5a9` | adopt applicable changes | `65c3da47955524f76f43367da8f3dbee9dfdc692` |
| `44b178e30f` | adopt applicable changes | `f2a749554e7cea55ede8f6fab22688e76a40aa90` |
| `2449e06d9c` | history receipt | `ea35d0d96cc5e01db18563153d59e4a6db53b00e` |
| `d005dda508` | adopt applicable changes | `c44799a5c817dabcde9bed517160076e6cb9877e` |
| `7bf6a4b4fb` | adopt applicable changes | `396c980ddf6469766d136f1e5d56bbf6f4e5efa9` |
| `54b7abd25a` | adopt applicable changes | `b5d3210306a98defa917d9cf7ae8916de48a72be` |
| `4fdcae5092` | adopt applicable changes | `52dcd150884c498601b8e804ba287ee645f07119` |
| `3dc5a1a7ef` | history receipt | `9af1915291b3699bcc7fc036c2885bebf8426092` |
| `7cc48f5c14` | adopt applicable changes | `b5c64a6e2cb2c527f973a5172c7412032ee81895` |
| `f66c0a65f9` | adopt applicable changes | `362407d9ab34fe498771d111523cd04b60675745` |
| `1f76487769` | adopt applicable changes | `1662f4cf2386806441673cf4b954dfb56f359003` |
| `aa65346003` | adopt applicable changes | `1e7faabede408dd93f650fac4966685db9d3c795` |
| `2e7cb6c4bf` | history receipt | `4a68a28610d72fa35287aadc2978d4499217b197` |
| `3dd304a8d5` | adopt applicable changes | `007aecbd03630ca69bca12ee2c577de99e35d7df` |
| `e356b3cae6` | adopt applicable changes | `5533bee4639922d88a8fd44f9658981b60675852` |
| `1557aca820` | adopt applicable changes | `8644bf5556bf86b52acbf0f16ccc39b8366a7490` |
| `4872effb80` | adopt applicable changes | `66a5b23f8f8f72d49f6d16b274a79bfca84e777d` |
| `146a7427a3` | adopt applicable changes | `adc30b389c31b047b359b74f49012184d1ff8584` |
| `00df3c16b7` | adopt applicable changes | `f2c1f1042eeeca412b4d744155358cb19e232423` |
| `7a855d8c3b` | adopt applicable changes | `68dcd12bf2456fba67f5622313bbb03540ff6f9f` |
| `a5edcd5780` | adopt applicable changes | `5af2c583e5eeda3ac8f407339da5906e8759d8c8` |
| `65dfdd3030` | adopt applicable changes | `acb63fa878d6b280283453ce5d4b42d4ac22d4a8` |
| `d611a7d560` | adopt applicable changes | `8fe36a66dc15a106f930443e20e3fad46f08f9be` |
| `19e2404ac2` | adopt applicable changes | `65c1ea75d41baf34fc836591182df00e737a8755` |
| `e9b9979ee3` | adopt applicable changes | `06150d210b7ca251b6bb86636132149db53e946e` |
| `f10a778c7a` | adopt applicable changes | `1031e17fc0ffc866221200552fd7f70fcaad8317` |
| `bc834240fd` | adopt applicable changes | `7caa27080a05c5888fd2466751417bd47e425559` |
| `99b16bc638` | adopt applicable changes | `bca5e50b2c86b79a529754247d2c6cfdbbc35f3b` |
| `d414239d5b` | adopt applicable changes | `111cea033eba291a29cb92be0e0391785dccadf1` |
| `dc6fd8a913` | adopt applicable changes | `07c513b14573eb9d06da873b4604d0b01ca4c5c7` |
| `d7bba3b92e` | history receipt | `4e726ab12e7587210550ed89481ce1c4452ca521` |
| `f66c153a10` | adopt applicable changes | `0ff89e0b994476c135665ffa91d84ea1d4b7fcd7` |
| `341d3b834a` | history receipt | `e14ae48908bfdf33daab9e174a345b4e438518ba` |
| `31004bbf33` | adopt applicable changes | `df70c8c9276e6fee6a14e353a9174082b34865cf` |
| `79be249f71` | adopt applicable changes | `613aa2870f089f4ca61fa5ecc55ef5b8841795d2` |
| `63a787c2ae` | history receipt | `c1a1a036527dd33cdaeb4f848b84487dcbd69669` |
| `1e8d8707c0` | adopt applicable changes | `d6f87cc132360306d73828f951f8de246087aff6` |
| `bcdb769cfd` | adopt applicable changes | `d3492d958e9677cc7472db83a3c005a4044e1a7f` |
| `35898a258c` | adopt applicable changes | `1797e3e440d4fa2c30d31912e21fa3f6416c7f4d` |
| `e16a74030d` | adopt applicable changes | `f2c4bd1cf796bfa801ad53efba69e2b2488be5d8` |
| `9d502af244` | adopt applicable changes | `ad8486aed052ca167d2864fe0c99bde071b1a00b` |
| `c8ab334060` | adopt applicable changes | `467dac9b52ad06a9a9d500bdadb2d14e738c623e` |
| `d687a06a22` | adopt applicable changes | `98477675ce71a67c9946c59c1e4218f10f5f5123` |
| `f5a3ff521d` | adopt applicable changes | `d221faff35c8745aa6fb8c1c3e64aff8b591c2a3` |
| `0ea046196d` | adopt applicable changes | `2c20178ca59cd4496b4282f4b093cb3810769856` |
| `7a6a02e35c` | adopt applicable changes | `11889e4b81555118b5f62c0ebf2419a0966804ab` |
| `7aca955172` | adopt applicable changes | `bda14e45acab38d55e446ef334fb6097e0032469` |
| `f31588edfc` | adopt applicable changes | `c01b560a8a09523f6b2f052f2b44a3c49f5a6bde` |
| `5d23833cc3` | adopt applicable changes | `04f1ab3566e19dbb6341f5a4bd61bf5b77a730ee` |
| `436bc0d53e` | history receipt | `82a083e3f347d394865acc1f52763281720fbce8` |
| `08aa13fee3` | adopt applicable changes | `37a0147a92649df89105f5b44be7f3058c6f0527` |
| `8e40456478` | history receipt | `492a2b44d9e630fac0665cd6d9bf5eb448c52923` |
| `c7bee6257d` | adopt applicable changes | `0b5a483165403a4073c82f5be4be3f7433568e06` |
| `672bd553fe` | adopt applicable changes | `2f0b1da286b0cfb6031c856cf3d6ea316e7e695b` |
| `340bb07626` | adopt applicable changes | `3b06fa75fe95078d25800a5718e98700d84d44e1` |
| `57de7b82be` | adopt applicable changes | `315b650e6856a527a5f42c4b3c5f3816f2a13ed1` |
| `52a684a0da` | adopt applicable changes | `be509c7c5a9a82b86543e42a2cee8e36056a248c` |
| `a0840b2a71` | adopt applicable changes | `d99bbfa52bc3672f30b34d6773ba75fb747504f1` |
| `e9c7f6667c` | adopt applicable changes | `0a6422494d43758fab94cee86da4e6eaaed30c28` |
| `9d59ed34dd` | adopt applicable changes | `2a3ba9f8c5ccd403b0e4aa2c132ed93ee4c72392` |
| `1ce70b8bf8` | adopt applicable changes | `dc32f5a0621401075c707299e76f8dfb6c0d5927` |
| `550fe4be85` | history receipt | `e4a3fb883e732c2a0f30499094696708c6573003` |
| `21e37e12f9` | adopt applicable changes | `070ead006337f332e4983bde5ec85031546b0c35` |
| `763d42cad7` | history receipt | `748398c604b8347ab970d4223d916f03ab2c9e0d` |
| `4beb0ec8e0` | adopt applicable changes | `e850102af69441b9816efa6977e2ffaab90579ec` |
| `c776757fb2` | adopt applicable changes | `0e421827c8248e63c23e2f5b8fe944c155fd37b8` |
| `b92dd1d78c` | adopt applicable changes | `8670fdff468a3500d39ad959c92d517494621986` |
| `0fb70602d3` | adopt applicable changes | `dd5989d8c10ef7b3cfba4e9a265581f4176272f0` |
| `69c8522d43` | adopt applicable changes | `9494be94abb4827e98974da4e4ff87d4a193b036` |
| `4fa8219e1f` | adopt applicable changes | `b311ee83e62fd0d76fea31b1492a961f03b32be8` |
| `9adbdd643b` | adopt applicable changes | `8013fb81cbf6efaea3fe150a976647e6adf3f404` |
| `fae5cf0f9d` | adopt applicable changes | `4751e314aace7973f3dcf605bf48a3d0a3f4ce79` |
| `ad94483ff1` | adopt applicable changes | `38eaae27fe90d8c859eca611bd180cf65feb8736` |
| `3e70c47a88` | adopt applicable changes | `b8663283e67c0a471db00a61e8d9abb83cc0fb05` |
| `f219737349` | adopt applicable changes | `87f9d8a9f5d4fad7c4143bd410134ba6f3f2f6b5` |
| `abcc8f90ca` | adopt applicable changes | `05cccd2eb2fc964e1ffc342cf6ff3c6275e1c39d` |
| `db9ab61b28` | adopt applicable changes | `a155413fc32b9bfbfa7b3d3d7d83e299ca7c3e06` |
| `bb870ce4fa` | adopt applicable changes | `3063b6ae4e3a60781d4ee9a228f7f1bf59ba408e` |
| `9279d06590` | history receipt | `68f53010b31dd1674a8b4bd65e16454b62f09e33` |
| `144aebc9df` | adopt applicable changes | `8a85d1f3d9fc36af61e80f4b94a69e6adab38a44` |
| `01b4f05421` | adopt applicable changes | `a946fc13f9feef35f991150abbeabb0850f12f98` |
| `1452f71be9` | adopt applicable changes | `45766971e8b0ac971c43672c860e0cafdb862706` |
| `3a50f2378f` | adopt applicable changes | `Upstream-Commit: 3a50f2378f4ed4ab06997b51edbb6dac04904d12` |

## How to use the ledger

Rows are in reverse topological order (parents before children), not author-date order. Old author dates and misleading “Merge” subjects are not grounds to exclude a commit. Full SHAs, both dates, parent SHAs, every changed path, fork overlap, probe results, implementation status and evidence slots are in the [JSON ledger](upstream-commit-ledger-2026-09-13.json). Paths on merge rows describe the first-parent delta and are not additional work.

`port` recommends adopting upstream behavior; `adapt` recommends implementing it at the fork owner; `decision` requires the linked product decision; `already-present-code` identifies existing code equivalence only; `history` is ancestry accounting. All recommendations remain unimplemented unless explicitly recorded otherwise. `C` means targeted code comparison; `T` means intent/path triage and still needs hunk review during implementation; `H` means merge-resolution probe. None is a runtime validation result.

The overlap column is the count of changed paths also modified by the fork since the base. Zero overlap does not imply safe behavior or dependency independence. Package labels name owning work, not a license to cherry-pick arbitrary commit order.

## Work-package index

| Package | Incoming commits | Contract |
| --- | ---: | --- |
| P0 — History and release integration | 37 | Record ancestry after all constituent work is verified; do not apply merge commits twice. |
| P1 — Identifiers, translations and assets | 35 | Merge strings by meaning; relocate fork IDs 7039..7080 before accepting upstream IDs 7039..7063. Keep resources and code aligned. |
| P2 — Type, flag and header migration | 168 | Adapt names and types across fork-only consumers; preserve numeric encodings, time units, ownership and deterministic behavior. Build without stale PCH. |
| P3 — Construction and topology | 9 | Use fork mutation-safe map owners and action Query/Execute paths; verify clearance, cent costs, ghost isolation and topology invalidation. |
| P4 — Ride, guest and gameplay behavior | 6 | Preserve sampled ratings, platform boarding, pricing and transport routing. Resolve linked decisions before changing behavior. |
| P5 — Windows, input and HUD | 38 | Preserve fork controls and 40 TPS timing; verify visibility, hit testing, window lifetime, focus and turbo responsiveness. |
| P6 — Rendering and presentation | 21 | Adapt to owned frame packets and Vulkan; never restore removed OpenGL code or expose mutable simulation objects to the render worker. |
| P7 — Plugins, objects and file lifecycle | 35 | Keep fork API units and private save version 60016; verify scripting lifetimes, cache invalidation and importer/exporter round trips. |
| P8 — Build, platform and release metadata | 12 | Preserve x86/x64 AVX2 and Vulkan build/deploy contracts. Audit dependencies and all first-party projects. |

## Full commit ledger

| ID | Commit and upstream subject | Work / treatment | Fork overlap | Review | Port note / decision |
| --- | --- | --- | ---: | --- | --- |
| U001 | [`e555c76500`](https://github.com/OpenRCT2/OpenRCT2/commit/e555c76500b49a84298d8827190e788512c760d8) Prevent setting invalid (not unknown) ride types (#26826) | P4 / adapt | 1 | C | Reject ride type values >= RIDE_TYPE_COUNT; retain fork invalidation and existing cheat gating.  |
| U002 | [`d81c3a7f14`](https://github.com/OpenRCT2/OpenRCT2/commit/d81c3a7f144148775f8b539dd4fe809c526e0c1b) Fix #26842: Best staff award doesn't need one of each staff type (#26843) | P4 / decision | 1 | C | Despite its subject, the patch REQUIRES all four staff types for Best Staff, plus existing count thresholds. [D04](upstream-port-plan-2026-09-13.md#d04) |
| U003 | [`d181e640ac`](https://github.com/OpenRCT2/OpenRCT2/commit/d181e640ac5283c9530b0ceb26f447b503b83122) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U004 | [`a4f0d22988`](https://github.com/OpenRCT2/OpenRCT2/commit/a4f0d22988fd5c54cb67a142bb14c9be2c749b95) Fix #26288: Land bordering map edges does not blend at certain angles (#26844) | P6 / adapt | 1 | T | Fix edge-of-map land blending; carry the corrected surface logic into fork surface presentation/cache ownership.  |
| U005 | [`775c4e7635`](https://github.com/OpenRCT2/OpenRCT2/commit/775c4e76350382a59fbdcd5504ee9437cafc6bf1) Update backtrace for upcoming release | P8 / adapt | 1 | T | Review release crash-report metadata against fork deployment; do not treat upstream credentials/configuration as fork telemetry policy.  |
| U006 | [`9198ff13f4`](https://github.com/OpenRCT2/OpenRCT2/commit/9198ff13f4c9cfb0f5f786f30c6f3f7c502a1c4b) Update to objects v1.7.11 | P1 / port | 0 | T | Update object archive to v1.7.11 and verify SHA-256; preserve local object precedence and deployed data pairing.  |
| U007 | [`cd44174f9b`](https://github.com/OpenRCT2/OpenRCT2/commit/cd44174f9b04ee909309214fee35656a0d07f871) Merge branch 'develop' | P0 / history | 119 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U008 | [`4a7ee146ca`](https://github.com/OpenRCT2/OpenRCT2/commit/4a7ee146caab8888eb31e56a33c0559db89b17bd) Release v0.5.4 | P8 / adapt | 2 | T | Record final release/version state, preserving andersonhk protocol flavor and advancing fork revision for accepted simulation/action changes.  |
| U009 | [`31760893d6`](https://github.com/OpenRCT2/OpenRCT2/commit/31760893d61fa4fee2ed3fafc2f7db1cec6ec36d) Merge branch 'master' into develop | P0 / history | 2 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U010 | [`f475c5b949`](https://github.com/OpenRCT2/OpenRCT2/commit/f475c5b94923c902580b2237c7ba54c04320dc92) Start v0.5.5 | P8 / adapt | 0 | T | Record final release/version state, preserving andersonhk protocol flavor and advancing fork revision for accepted simulation/action changes.  |
| U011 | [`c51e3a3e40`](https://github.com/OpenRCT2/OpenRCT2/commit/c51e3a3e4000ddd0435304c4f0eb587cd8cc14a6) Move ride type selection to operations tab; add visibility button to appearance tab | P5 / decision | 4 | T | Move ride-type controls to operations and visibility to appearance, including shop exceptions; retain maze, fare and directed-leg controls. [D06](upstream-port-plan-2026-09-13.md#d06) |
| U012 | [`c35f71131f`](https://github.com/OpenRCT2/OpenRCT2/commit/c35f71131fed435a1f98cb31799d891e901bb5da) Hide operating mode 'tweak' option for shops and stalls | P5 / decision | 1 | T | Move ride-type controls to operations and visibility to appearance, including shop exceptions; retain maze, fare and directed-leg controls. [D06](upstream-port-plan-2026-09-13.md#d06) |
| U013 | [`ea084b9828`](https://github.com/OpenRCT2/OpenRCT2/commit/ea084b982808d9b7e392a19598896a4cc92c3b33) Keep colour tab visible for shops when ride type cheats are on | P5 / decision | 1 | T | Move ride-type controls to operations and visibility to appearance, including shop exceptions; retain maze, fare and directed-leg controls. [D06](upstream-port-plan-2026-09-13.md#d06) |
| U014 | [`b9484d7f48`](https://github.com/OpenRCT2/OpenRCT2/commit/b9484d7f485d27355b27d0d8e4d16dbfa4585988) Fix facility drawing | P6 / port | 1 | T | Correct facility drawing in the ride window; verify current/final ride appearance controls.  |
| U015 | [`89b9f7d64c`](https://github.com/OpenRCT2/OpenRCT2/commit/89b9f7d64c6c0f0ded7ba99ffae58c937daa647b) Add changelog entries | P1 / port | 0 | T | Apply final documentation/changelog/string correction; distinguish upstream release history from features actually ported.  |
| U016 | [`7339f6eba6`](https://github.com/OpenRCT2/OpenRCT2/commit/7339f6eba68a38574ee4663fe803a90125a455b1) Merge pull request #26099 from AaronVanGeffen/ride-type-move | P0 / history | 4 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U017 | [`b8cf2f8c35`](https://github.com/OpenRCT2/OpenRCT2/commit/b8cf2f8c359a818debd359c1ae9966877548c29a) Fix #25169: convert command strips packed objects (#26808) | P7 / adapt | 1 | C | Pack custom objects by default in convert; forward ExportObjectsList in path exporter while retaining TargetVersion and cent conversion.  |
| U018 | [`430bb95b63`](https://github.com/OpenRCT2/OpenRCT2/commit/430bb95b63541ae8d6c1610cf16b50ee3578365c) Rework includes in openrct2/object and openrct2/paint (#26852) | P2 / adapt | 27 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U019 | [`d517f19ce2`](https://github.com/OpenRCT2/OpenRCT2/commit/d517f19ce293d5d7190aac5dba0f2d2a71ac9559) Use flags for widget visibility instead of empty widget type (#26801) | P5 / adapt | 12 | C | Replace visibility-only WidgetType::empty assignments with hidden flags throughout fork windows; genuine widget-type changes remain.  |
| U020 | [`0284f9cda6`](https://github.com/OpenRCT2/OpenRCT2/commit/0284f9cda66206d8d99362f1dddf490dc0733640) Refactor dropdown flag to FlagHolder, reverse "stay open" flag (#26855) | P5 / adapt | 9 | T | Dropdown flags invert stay-open meaning; migrate every caller including fork-only dropdowns, preserving interaction.  |
| U021 | [`77c69cb55d`](https://github.com/OpenRCT2/OpenRCT2/commit/77c69cb55d94e64f08a1978b49c0af99525a11a2) Fix debug menu/button always visible in toolbar (#26854) | P5 / port | 0 | T | Hide debug controls when disabled; include final multiplayer/editor visibility fixes in the same verification matrix.  |
| U022 | [`7df1cfe7c0`](https://github.com/OpenRCT2/OpenRCT2/commit/7df1cfe7c084d4b65b3ebfc1c0fc261c2bf135ca) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U023 | [`2ac48e6c42`](https://github.com/OpenRCT2/OpenRCT2/commit/2ac48e6c4233597efab90bfa11d88b0cc28e6d1a) Rename members of MixerGroup | P2 / adapt | 6 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U024 | [`c80a0e168d`](https://github.com/OpenRCT2/OpenRCT2/commit/c80a0e168d214a23fffe02a59e84319b1abb1bbe) Rename members of FileExtension | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U025 | [`a7881846e1`](https://github.com/OpenRCT2/OpenRCT2/commit/a7881846e18921dec2e5b1e5109aeb53dd41469e) Rename members of TunnelGroup | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U026 | [`e80faf5606`](https://github.com/OpenRCT2/OpenRCT2/commit/e80faf5606b59b47b242fec9a5af562baafd64a2) Rename members of TunnelSubType | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U027 | [`abbbb251ee`](https://github.com/OpenRCT2/OpenRCT2/commit/abbbb251eeafe49454a43fa2fc7a25c821ab2039) Rename members of CarEntryAnimation | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U028 | [`47c1bf9166`](https://github.com/OpenRCT2/OpenRCT2/commit/47c1bf91669e56e9dcd0d6ebc2c0f742715b98ce) Rename members of SpriteGroupType | P2 / adapt | 6 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U029 | [`b2375341bb`](https://github.com/OpenRCT2/OpenRCT2/commit/b2375341bbc96db4288aea3f50375b9b301027fb) Rename members of RideConstructionState | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U030 | [`2ed6bef643`](https://github.com/OpenRCT2/OpenRCT2/commit/2ed6bef643c7217dcaaf4735dc4714789f140e1a) Rename members of VehicleTrackSubposition | P2 / adapt | 4 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U031 | [`46dbc1cf20`](https://github.com/OpenRCT2/OpenRCT2/commit/46dbc1cf203caa8b9d8253814e4a684b8aba2ac4) Rename members of ObjectiveStatus | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U032 | [`e1f2c365c1`](https://github.com/OpenRCT2/OpenRCT2/commit/e1f2c365c1f875daed2feafc641e646f7d162638) Rename members of PluginType | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U033 | [`0ab5cb5c49`](https://github.com/OpenRCT2/OpenRCT2/commit/0ab5cb5c49753d3cf50e79c799fffea8fb6f7777) Rename members of ScConfigurationKind | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U034 | [`c728e66032`](https://github.com/OpenRCT2/OpenRCT2/commit/c728e66032040e58b76291c7521cb2776b774314) Rename members of FileDialogType | P2 / adapt | 2 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U035 | [`9c5057a524`](https://github.com/OpenRCT2/OpenRCT2/commit/9c5057a524ac748dca642928c2594db1b8a099ef) Rename members of TileInspectorPage | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U036 | [`8b9bde1f54`](https://github.com/OpenRCT2/OpenRCT2/commit/8b9bde1f548fd4d9c6ba269a45e47721ee9c0c27) Merge pull request #26856 from Gymnasiast/refactor/enum-class | P0 / history | 20 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U037 | [`799002a820`](https://github.com/OpenRCT2/OpenRCT2/commit/799002a820822ad6f9551c1095878ad63f5336a0) Fix colour buttons showing up for uncolourable shops with cheats active (#26858) | P5 / decision | 1 | T | Move ride-type controls to operations and visibility to appearance, including shop exceptions; retain maze, fare and directed-leg controls. [D06](upstream-port-plan-2026-09-13.md#d06) |
| U038 | [`b782a7dcc9`](https://github.com/OpenRCT2/OpenRCT2/commit/b782a7dcc9da6a30b2626724604d380316f09ec3) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U039 | [`a3f7b5d3b0`](https://github.com/OpenRCT2/OpenRCT2/commit/a3f7b5d3b09f470dee3e1b28ba157e40e3ff95c4) Restore overriding widget type for construction bank/speed setting (#26863) | P5 / adapt | 1 | T | Port the final separated banking/speed widget design and overlap fix, including restored genuine widget-type switching.  |
| U040 | [`d26b161668`](https://github.com/OpenRCT2/OpenRCT2/commit/d26b161668e42872287912b0de5e811a66c3f6a5) Move gCurrentWindowColours to Drawing.String.cpp | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U041 | [`4bf86c6e6c`](https://github.com/OpenRCT2/OpenRCT2/commit/4bf86c6e6c96cb7a171bf51f9a6cadcdf3e178a6) Move tile inspector constants to TileInspectorGlobals.h | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U042 | [`0f620b0714`](https://github.com/OpenRCT2/OpenRCT2/commit/0f620b0714209970908c69d525173e77d2545189) Remove Widget.h include from Window.h | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U043 | [`082994cd52`](https://github.com/OpenRCT2/OpenRCT2/commit/082994cd5266f2c3aac4fe09b5b265ac506e7e4c) Move widget index globals into WidgetIndexGlobals.h | P2 / adapt | 8 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U044 | [`bfd0a374ff`](https://github.com/OpenRCT2/OpenRCT2/commit/bfd0a374ff3ec4fc6a46d7839ae6424634475ce8) Merge pull request #26867 from AaronVanGeffen/strip-window-header | P0 / history | 9 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U045 | [`302058221f`](https://github.com/OpenRCT2/OpenRCT2/commit/302058221f4c3c47e079f194c61ffc49f6f7bbf0) Move WindowFlags into their own header (#26869) | P2 / adapt | 2 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U046 | [`1423ab779a`](https://github.com/OpenRCT2/OpenRCT2/commit/1423ab779a5a47190f72e04449690e184efc8a36) Fix #26811: Crash when a ride points to a non-existing station object | P4 / adapt | 1 | T | Handle missing station object without dereference; retain station platform/staging behavior.  |
| U047 | [`bcd7708dc1`](https://github.com/OpenRCT2/OpenRCT2/commit/bcd7708dc1a1da55aa1f74e570e93ebeb0ec34c3) Merge pull request #26872 from Gymnasiast/fix/26811 | P0 / history | 1 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U048 | [`40b7741b58`](https://github.com/OpenRCT2/OpenRCT2/commit/40b7741b58bfa1d165249a0ba53e3b1b97f58ec3) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U049 | [`26b28153bb`](https://github.com/OpenRCT2/OpenRCT2/commit/26b28153bbfdfe30ec74f1e13db3f17d89329a01) Disentangle speed controls from the banking widgets (#26864) | P5 / adapt | 1 | T | Port the final separated banking/speed widget design and overlap fix, including restored genuine widget-type switching.  |
| U050 | [`8bfe55af63`](https://github.com/OpenRCT2/OpenRCT2/commit/8bfe55af6378da68c0e48d9be3f716e7b8e9155b) Add Liquid Glass app icon for macOS 26+ (#26862) | P8 / port | 1 | T | Apply platform/CI/documentation change with relevant target check; retain fork build targets and workflows.  |
| U051 | [`687fa7181d`](https://github.com/OpenRCT2/OpenRCT2/commit/687fa7181df6747d544930250d6272f1deccd58b) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U052 | [`e103cb11bb`](https://github.com/OpenRCT2/OpenRCT2/commit/e103cb11bb7e116bbd48d7bf01c7a8163391b148) Rename members of DrawingEngine | P2 / adapt | 8 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U053 | [`3fa93c3278`](https://github.com/OpenRCT2/OpenRCT2/commit/3fa93c32786eef367c5a851061e74f0e11c10ef3) Rename members of Weather::Type | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U054 | [`068f68e4a9`](https://github.com/OpenRCT2/OpenRCT2/commit/068f68e4a910011f1c47485f3380153cefbaf9cd) Rename members of Weather::EffectType | P2 / adapt | 2 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U055 | [`4dbb57a87a`](https://github.com/OpenRCT2/OpenRCT2/commit/4dbb57a87a851571e3ac0c9f41c30611f189b969) Rename members of Weather::Level | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U056 | [`1b63339ef7`](https://github.com/OpenRCT2/OpenRCT2/commit/1b63339ef7d64d45bdaf24dc289c0e0a2d5fd982) Rename members of ScrollbarType | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U057 | [`1e2817a5e1`](https://github.com/OpenRCT2/OpenRCT2/commit/1e2817a5e1a0b3d93711cf0f7e6530ac1a6d7fa1) Rename members of ColumnSortOrder | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U058 | [`8c3ad47b92`](https://github.com/OpenRCT2/OpenRCT2/commit/8c3ad47b92f67fbee1c4a3933545631263ff0006) Rename members of CustomToolbarMenuItemKind | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U059 | [`45007c1865`](https://github.com/OpenRCT2/OpenRCT2/commit/45007c1865e3380311e312f201935fc616cd3c20) Rename members of CursorID | P2 / adapt | 7 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U060 | [`3a36fc1c0a`](https://github.com/OpenRCT2/OpenRCT2/commit/3a36fc1c0aa5dc71148e3703ccc2ba51fffa41ea) Rename members of TitleScript | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U061 | [`959401a3a0`](https://github.com/OpenRCT2/OpenRCT2/commit/959401a3a082177f645f73023ecd58a18544534c) Rename Guest::MazeType and its members | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U062 | [`0d0498a081`](https://github.com/OpenRCT2/OpenRCT2/commit/0d0498a0819134d2a80919f3e3bda5c3dc11891d) Rename members of ReplayMode | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U063 | [`2769a3ff8c`](https://github.com/OpenRCT2/OpenRCT2/commit/2769a3ff8cdf39f028666f5d6da65fb7ad392c70) Rename members of RecordType | P2 / adapt | 2 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U064 | [`331dd2670a`](https://github.com/OpenRCT2/OpenRCT2/commit/331dd2670a7f15a9e980c00ca233f86c760fbe4b) Rename members of AudioCodecKind | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U065 | [`17f7713603`](https://github.com/OpenRCT2/OpenRCT2/commit/17f7713603dd806b21c89376797663c434feb8a5) Rename members of SoundType | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U066 | [`5a0aa69409`](https://github.com/OpenRCT2/OpenRCT2/commit/5a0aa69409f312233bcfe4f95cf1203205b33e96) Rename members of PixelDataKind | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U067 | [`e89ab11a6a`](https://github.com/OpenRCT2/OpenRCT2/commit/e89ab11a6a1e1fa0f04669da2b93ed895aae0c2b) Rename members of PixelDataPaletteKind | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U068 | [`882467fe20`](https://github.com/OpenRCT2/OpenRCT2/commit/882467fe20a6446d58e39a91dbd56860e3f5934b) Rename members of GuestList::TabId | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U069 | [`cb13ddba48`](https://github.com/OpenRCT2/OpenRCT2/commit/cb13ddba4897d3ab96e26623636c9ded253ee454) Rename members of GuestList::GuestViewType | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U070 | [`95cf306886`](https://github.com/OpenRCT2/OpenRCT2/commit/95cf3068869c314b2aea0cd683651e2b60e1dbbf) Rename members of GuestList::GuestFilterType | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U071 | [`275e6597a7`](https://github.com/OpenRCT2/OpenRCT2/commit/275e6597a772858ad71294339303a397b884b796) Rename members of LandRightsMode | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U072 | [`546cd2dab6`](https://github.com/OpenRCT2/OpenRCT2/commit/546cd2dab65d44aeadb3fc07a93e4cef7561f77b) Rename members of ResizeDirection | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U073 | [`0c4322a982`](https://github.com/OpenRCT2/OpenRCT2/commit/0c4322a98297de1b4edfaf7343b8651cd418f61f) Rename members of ResizeDirection | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U074 | [`c166b42a67`](https://github.com/OpenRCT2/OpenRCT2/commit/c166b42a67a58492dbef5d2cfc32ad75c2e61534) Rename members of ListItemType | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U075 | [`8ae5221c6b`](https://github.com/OpenRCT2/OpenRCT2/commit/8ae5221c6bdf72cad2f22c2c04fe2f25bbd7c14a) Rename members of DisplayType | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U076 | [`6d8c65ba5f`](https://github.com/OpenRCT2/OpenRCT2/commit/6d8c65ba5f349de80e04544fd6d6af369c3fbc5d) Rename members of Http::Status | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U077 | [`45bc989f1b`](https://github.com/OpenRCT2/OpenRCT2/commit/45bc989f1bf3e9c3986cd2221a73c95f5e1e0cac) Rename members of Http::Method | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U078 | [`086b5ecabe`](https://github.com/OpenRCT2/OpenRCT2/commit/086b5ecabe73351cefca8f1464df0c7d318aa25e) Rename members of FlagType | P2 / adapt | 2 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U079 | [`25df52f1bf`](https://github.com/OpenRCT2/OpenRCT2/commit/25df52f1bf51d4be9516755289c4ddd9413bbe86) Rename members of ImageCatalogue | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U080 | [`d0db940bde`](https://github.com/OpenRCT2/OpenRCT2/commit/d0db940bdeca52191c1f0bdc81c0652d8c83a4b6) Rename members of ImportMode | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U081 | [`1c4daab873`](https://github.com/OpenRCT2/OpenRCT2/commit/1c4daab873f851be55eddd3e074be0bbda917922) Rename members of Palette | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U082 | [`e6cc15f033`](https://github.com/OpenRCT2/OpenRCT2/commit/e6cc15f033672dec277fb7553d0ad23e6276e684) Rename members of PaletteIndexType | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U083 | [`b0e7d321e3`](https://github.com/OpenRCT2/OpenRCT2/commit/b0e7d321e34a316a3127242129515cc39fb69573) Rename members of Qualifier | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U084 | [`a850d78a7d`](https://github.com/OpenRCT2/OpenRCT2/commit/a850d78a7d5c23355b42e9c290e771c71043dc6c) Rename members of DuckState | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U085 | [`a6175f8930`](https://github.com/OpenRCT2/OpenRCT2/commit/a6175f893014a1e18d41488a0a48f31dd125eb5e) Rename members of JumpingFountainType | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U086 | [`53f5a41a74`](https://github.com/OpenRCT2/OpenRCT2/commit/53f5a41a74c3b746fad60b0b0b3e8172feb85173) Rename members of MusicNiceFactor | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U087 | [`9bd1e0c858`](https://github.com/OpenRCT2/OpenRCT2/commit/9bd1e0c858d9ae70bc700835c5245b7e121cd1cf) Rename members of ObjectGeneration | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U088 | [`76e75e687f`](https://github.com/OpenRCT2/OpenRCT2/commit/76e75e687f36f9f2a325b147500af2b8e6e14f95) Rename members of TunnelType | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U089 | [`41164a7453`](https://github.com/OpenRCT2/OpenRCT2/commit/41164a74533f4c5744d0b8e4dc86eb5364db842e) Rename members of JuniorRCSubType | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U090 | [`4502be0fb2`](https://github.com/OpenRCT2/OpenRCT2/commit/4502be0fb2f2d96a7522b6eee8a76c69f0e986b1) Rename members of Plane | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U091 | [`5ba0b8028c`](https://github.com/OpenRCT2/OpenRCT2/commit/5ba0b8028ccb11d9aadd05f5f973fef740bcb3ea) Rename members of PathSearchResult | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U092 | [`780a135087`](https://github.com/OpenRCT2/OpenRCT2/commit/780a135087ec76cbadd501afe326962e254e959b) Rename members of RideComponentType | P2 / adapt | 51 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U093 | [`f256f03fff`](https://github.com/OpenRCT2/OpenRCT2/commit/f256f03fff2dd6c4bd2178d0f163bc2844c49cde) Rename members of RideColourKey | P2 / adapt | 50 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U094 | [`bd4a9eb0ef`](https://github.com/OpenRCT2/OpenRCT2/commit/bd4a9eb0ef5b0e8d0dc098d7838a2d8fc820c54d) Rename members of TrackDesignCreateMode | P2 / adapt | 5 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U095 | [`7f879409ac`](https://github.com/OpenRCT2/OpenRCT2/commit/7f879409acfa787215bfa66633823e2a754b9284) Rename members of RatingsCalculationType | P2 / adapt | 51 | C | Rename rating/RTD/mode symbols only; do not restore upstream BaseRatings, BonusMazeSize, operating modes or default coefficients over fork models. [D12](upstream-port-plan-2026-09-13.md#d12) |
| U096 | [`28ad2fcaa2`](https://github.com/OpenRCT2/OpenRCT2/commit/28ad2fcaa2570ee2d2bc7c665c641b4168687cc0) Rename members of RatingsModifierType | P2 / adapt | 51 | C | Rename rating/RTD/mode symbols only; do not restore upstream BaseRatings, BonusMazeSize, operating modes or default coefficients over fork models. [D12](upstream-port-plan-2026-09-13.md#d12) |
| U097 | [`991fc7b4a1`](https://github.com/OpenRCT2/OpenRCT2/commit/991fc7b4a17d606d5800b12575f12cab6133c577) Rename members of RideConstructionWindowContext | P2 / adapt | 5 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U098 | [`0d8af50f45`](https://github.com/OpenRCT2/OpenRCT2/commit/0d8af50f45b0d4dffbcae25dd5ce63afcac89947) Rename members of GameCommand | P2 / adapt | 4 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U099 | [`9c830feef5`](https://github.com/OpenRCT2/OpenRCT2/commit/9c830feef560cb2017223721112cf2b764d8ebac) Rename members of TrackDesignGameStateFlag | P2 / adapt | 4 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U100 | [`b229ff8f60`](https://github.com/OpenRCT2/OpenRCT2/commit/b229ff8f607ac0d4543fcef6cdd4c7935eacbae8) Rename members of MiniGolfState | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U101 | [`ae5dc4422a`](https://github.com/OpenRCT2/OpenRCT2/commit/ae5dc4422a3ccb5908c8c5037784ea712c0b35c8) Rename members of MiniGolfAnimation | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U102 | [`b5a92d9296`](https://github.com/OpenRCT2/OpenRCT2/commit/b5a92d92968697a1645327e47d6cb33019cd3e5f) Rename members of BoatHireSubState | P2 / adapt | 2 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U103 | [`78fb8d73bb`](https://github.com/OpenRCT2/OpenRCT2/commit/78fb8d73bb82f78b276b55399b393b8f1fe749fb) Rename members of ScenarioSource | P2 / adapt | 4 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U104 | [`25fc422c44`](https://github.com/OpenRCT2/OpenRCT2/commit/25fc422c44b6298ea85a680bc44537464c59f517) Rename members of IntroState | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U105 | [`38bce48524`](https://github.com/OpenRCT2/OpenRCT2/commit/38bce4852403e43d47af72906fe53680f2ede126) Rename members of ScatterToolDensity | P2 / adapt | 2 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U106 | [`a5892a2d17`](https://github.com/OpenRCT2/OpenRCT2/commit/a5892a2d173fce9dd838512bbe0151eae8b3a864) Enforce `enum class` member code style via Clang-Tidy | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U107 | [`228c4bfb34`](https://github.com/OpenRCT2/OpenRCT2/commit/228c4bfb34d74729d67e4ba309a5ee5412f7851d) Rename CursorNames to kCursorNames | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U108 | [`205497a480`](https://github.com/OpenRCT2/OpenRCT2/commit/205497a4800f9cc1a06c8616318845e551f501d5) Merge pull request #26876 from Gymnasiast/refactor/remaining-enum-class-members | P0 / history | 101 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U109 | [`7f39bdebf5`](https://github.com/OpenRCT2/OpenRCT2/commit/7f39bdebf52fabf3bcb2a431cda2edbc9c83f914) Remove openrct2/park includes | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U110 | [`b3ef890f38`](https://github.com/OpenRCT2/OpenRCT2/commit/b3ef890f383ea9af6904b079c906336f0b3ed4f0) Remove openrct2/peep includes | P2 / adapt | 2 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U111 | [`7f8fc39ff5`](https://github.com/OpenRCT2/OpenRCT2/commit/7f8fc39ff533d965c7875101a833158196a518bb) Remove openrct2/platform includes | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U112 | [`5c0caf461a`](https://github.com/OpenRCT2/OpenRCT2/commit/5c0caf461afb2acc4d704c817b284a2cec6b8467) Remove openrct2/rct1, openrct2/rct2 and openrct2/rct12 includes | P2 / adapt | 5 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U113 | [`6323d95edc`](https://github.com/OpenRCT2/OpenRCT2/commit/6323d95edc0a239031938ff8c20983f5894ecf7d) Remove openrct2/ride includes | P2 / adapt | 65 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U114 | [`53a81b1b45`](https://github.com/OpenRCT2/OpenRCT2/commit/53a81b1b45b95b0745fa6f3fb38162f8bef64d42) Merge pull request #26874 from Harry-Hopkinson/remove-even-more-includes | P0 / history | 71 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U115 | [`51649e5873`](https://github.com/OpenRCT2/OpenRCT2/commit/51649e5873c8441e8bfab995e3e4d1d5953417aa) Rework Ride and Vehicle headers into OpenRCT2 namespace (#26881) | P2 / adapt | 27 | C | Namespace migration moves legacy RideRatingsCalculateValue wholesale: keep fork 1.5x/1.2x age bonuses, target-price update and sampled rating branch. [D11](upstream-port-plan-2026-09-13.md#d11) [D12](upstream-port-plan-2026-09-13.md#d12) |
| U116 | [`4087c9c6b9`](https://github.com/OpenRCT2/OpenRCT2/commit/4087c9c6b9a6a6c6871dda823ee1473006902679) Rework script binding includes (#26882) | P2 / adapt | 7 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U117 | [`15b36d6ced`](https://github.com/OpenRCT2/OpenRCT2/commit/15b36d6ced4d227826a1ad4c218cd5fa5ae79835) Introduce dedicated header for window-related enum types (#26883) | P2 / adapt | 7 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U118 | [`9c59a4c5e8`](https://github.com/OpenRCT2/OpenRCT2/commit/9c59a4c5e8dde0d146ec6b501745a9a565c4efe0) Fix notation of two numbers | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U119 | [`536973e9e6`](https://github.com/OpenRCT2/OpenRCT2/commit/536973e9e6aa355049831dabf306d916ff09464e) Add GetFlagHolder() overload for normal/inverted flag | P2 / adapt | 0 | T | Add normal/inverted FlagHolder helper overload; retain exact boolean inversion at migrated callers.  |
| U120 | [`ac8b290671`](https://github.com/OpenRCT2/OpenRCT2/commit/ac8b29067165e49ae492a7c3d6fbd292c475371d) Refactor WALL_SCENERY_FLAGS into enum class+FlagHolder | P2 / adapt | 4 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U121 | [`94a889e4c9`](https://github.com/OpenRCT2/OpenRCT2/commit/94a889e4c9d6d719e4072270a327dd650cf2c4ec) Refactor SCROLL_FLAGS into enum class+FlagHolder | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U122 | [`c2c126da9c`](https://github.com/OpenRCT2/OpenRCT2/commit/c2c126da9ce7ada8eedac10549d8fd4f07163669) Refactor BTM_TOOLBAR_DIRTY_FLAGS into enum class+FlagHolder | P2 / adapt | 2 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U123 | [`263cb53047`](https://github.com/OpenRCT2/OpenRCT2/commit/263cb53047ea432cf6bd80507095f69982065e42) Merge pull request #26884 from Gymnasiast/more-enum-refactor | P0 / history | 6 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U124 | [`5a814722e1`](https://github.com/OpenRCT2/OpenRCT2/commit/5a814722e136db8fc168f06a4794f29c88ca31b9) Add Walls-only and Footpath Addition modes to Clear Scenery (#26877) | P3 / decision | 5 | C | Split walls from small scenery and add path-addition clearing; preserve nested fork actions and explicitly account for changed plugin clear mask. [D10](upstream-port-plan-2026-09-13.md#d10) |
| U125 | [`42885ec965`](https://github.com/OpenRCT2/OpenRCT2/commit/42885ec9659d628a842f76d45be9c9430ba1a8fb) Update GitHub checkout action to v7 (#26885) | P8 / port | 1 | T | Apply platform/CI/documentation change with relevant target check; retain fork build targets and workflows.  |
| U126 | [`d7d8915065`](https://github.com/OpenRCT2/OpenRCT2/commit/d7d89150658d8f6a654ce10cc8d598d844fe277c) Fixed bug where ride.previousVerticalG was reset to 0 instead of 100 (#26879) | P4 / already-present-code | 2 | C | Vehicle.cpp:1489 already resets previousVerticalG to 1G. Retain fork sample resets; upstream changelog/protocol bookkeeping is separate.  |
| U127 | [`f91e2d680b`](https://github.com/OpenRCT2/OpenRCT2/commit/f91e2d680b9daee427eac08d379a27038e6c9fd3) Remove openrct2/sawyer_coding includes | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U128 | [`f4ddda3f69`](https://github.com/OpenRCT2/OpenRCT2/commit/f4ddda3f6974938165d15571b7014c4c0c731636) Remove openrct2/scenario includes | P2 / adapt | 2 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U129 | [`f967ebc2d2`](https://github.com/OpenRCT2/OpenRCT2/commit/f967ebc2d239eb9cb5762c5c060aea59dd212775) Remove openrct2/scenes includes | P2 / adapt | 2 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U130 | [`6c5c2079ce`](https://github.com/OpenRCT2/OpenRCT2/commit/6c5c2079ce16675a7996a426d707a6f4ccc2cbce) Remove openrct2/scripting includes | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U131 | [`3c375c60ba`](https://github.com/OpenRCT2/OpenRCT2/commit/3c375c60ba4cb354f8672e0aec10e7a585ce789a) Remove openrct2/windows includes | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U132 | [`8cd44cc686`](https://github.com/OpenRCT2/OpenRCT2/commit/8cd44cc6861ed74658c7deeec81eaf5513e5f294) Remove openrct2/world includes | P2 / adapt | 21 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U133 | [`8839956721`](https://github.com/OpenRCT2/OpenRCT2/commit/8839956721913ef48e6981477339a01f6f39e4f5) Merge pull request #26886 from Harry-Hopkinson/remove-more-includes | P0 / history | 26 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U134 | [`11bf58dceb`](https://github.com/OpenRCT2/OpenRCT2/commit/11bf58dceb588ec4e1d04fa2e247b55979cbb0bc) Refactor CLEARABLE_ITEMS into enum class+FlagHolder | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U135 | [`a70874710a`](https://github.com/OpenRCT2/OpenRCT2/commit/a70874710a412bdb62f6179018ee72894aae9ed7) Refactor peep flags to enum class+FlagHolder | P2 / adapt | 13 | T | Migrate peep flags/names while retaining fork needs, platform/transport states, happiness-based growth and time conversions. [D13](upstream-port-plan-2026-09-13.md#d13) [D14](upstream-port-plan-2026-09-13.md#d14) |
| U136 | [`f08bc61f03`](https://github.com/OpenRCT2/OpenRCT2/commit/f08bc61f03fa135e2df0c472bfe3b0d5d22af1bd) Refactor park flags into enum class+FlagHolder | P2 / adapt | 33 | C | Migrate park flag access without restoring legacy population caps, rating penalties, finance intervals or upstream price policy. [D13](upstream-port-plan-2026-09-13.md#d13) [D14](upstream-port-plan-2026-09-13.md#d14) |
| U137 | [`5aa3f85c56`](https://github.com/OpenRCT2/OpenRCT2/commit/5aa3f85c56c4bd68500380190d9deac357686423) Merge pull request #26891 from Gymnasiast/more-enums | P0 / history | 39 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U138 | [`2e43967fe5`](https://github.com/OpenRCT2/OpenRCT2/commit/2e43967fe5738d991623dba05ef85a8f5d24221c) Fix: water rides ignore zero clearances, preventing adjacent terrain modifications and building them anywhere (#26816) | P3 / decision | 4 | C | Clearance cheat also bypasses floating/water-height checks, allowing dry or mid-air water rides and terrain/water edits. [D03](upstream-port-plan-2026-09-13.md#d03) |
| U139 | [`4c1cedbb08`](https://github.com/OpenRCT2/OpenRCT2/commit/4c1cedbb084cfa77b1523d593993d6fcdbdaca29) Refactor wall scenery flags 2 into enum class+FlagHolder | P2 / adapt | 4 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U140 | [`d4c67d9b8d`](https://github.com/OpenRCT2/OpenRCT2/commit/d4c67d9b8df443306e439c3c30e52100c9e041a2) Turn FrictionSound into enum class | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U141 | [`8b4b1c6561`](https://github.com/OpenRCT2/OpenRCT2/commit/8b4b1c6561b407359dc3198dfc09ee2737bc27d2) Refactor VEHICLE_VEHICLE_* to two enum classes | P2 / adapt | 6 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U142 | [`bb0b26e3f2`](https://github.com/OpenRCT2/OpenRCT2/commit/bb0b26e3f2155d180cefce6cfe9e8fd0f35b294d) Rename RTD constants | P2 / adapt | 49 | T | Rename rating/RTD/mode symbols only; do not restore upstream BaseRatings, BonusMazeSize, operating modes or default coefficients over fork models. [D12](upstream-port-plan-2026-09-13.md#d12) |
| U143 | [`5d3903209e`](https://github.com/OpenRCT2/OpenRCT2/commit/5d3903209e66820cdf129ed86c2a4a1f51ce8e4c) More enum class conversions (plus a constexpr rename) (#26897) | P0 / history | 59 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U144 | [`5c0fc86397`](https://github.com/OpenRCT2/OpenRCT2/commit/5c0fc863979b7c7b9a8675cf69d51661309c0033) Rename CarEntry members to adhere to code style | P2 / adapt | 27 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U145 | [`a629158a4b`](https://github.com/OpenRCT2/OpenRCT2/commit/a629158a4bbe87b89b8d16f86e218ab23e0d8a24) Rename SpriteGroupNames to kSpriteGroupNames | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U146 | [`24869300df`](https://github.com/OpenRCT2/OpenRCT2/commit/24869300dfeb6d5f80587e3dfb5c934198eeccf2) Rename VehicleSpriteGroup member function to adhere to code style | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U147 | [`bd935d75c9`](https://github.com/OpenRCT2/OpenRCT2/commit/bd935d75c947eba5c4620e96cd978c25c9484468) Merge pull request #26899 from Gymnasiast/more-renames | P0 / history | 27 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U148 | [`a187bc9550`](https://github.com/OpenRCT2/OpenRCT2/commit/a187bc95503c9f35f224046e6bf945ab6adfac39) Close #26827: Add map resize hook to scripting API (#26828) | P7 / adapt | 2 | C | Add map.resize hook after successful resize/shift and fork topology reset; script callbacks must see the completed map.  |
| U149 | [`4e576a04ad`](https://github.com/OpenRCT2/OpenRCT2/commit/4e576a04ada64265e64abc4132a9ea6f06a8ebb6) Fix forced portrait orientation on Android app launch (#26873) | P8 / port | 0 | T | Apply platform/CI/documentation change with relevant target check; retain fork build targets and workflows.  |
| U150 | [`5ca2c1845c`](https://github.com/OpenRCT2/OpenRCT2/commit/5ca2c1845c5ef1d4a6dc79a0b980851b4bc668bc) Remove openrct2-ui/drawing includes | P2 / adapt | 15 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U151 | [`e6be313f1d`](https://github.com/OpenRCT2/OpenRCT2/commit/e6be313f1d925c83ab4055960b0cfc8611eddabf) Remove openrct2-ui/input includes | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U152 | [`d6863ccf99`](https://github.com/OpenRCT2/OpenRCT2/commit/d6863ccf998573fd6cecbf33272cf378523ea554) Remove openrct2-ui/interface includes | P2 / adapt | 16 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U153 | [`d9c423b1d6`](https://github.com/OpenRCT2/OpenRCT2/commit/d9c423b1d6e7698e8435ab5d301ec52cf7ab5720) Merge pull request #26907 from Harry-Hopkinson/start-removing-ui-includes | P0 / history | 31 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U154 | [`bdbe9013cc`](https://github.com/OpenRCT2/OpenRCT2/commit/bdbe9013ccbcfb36be7c3948f296da8dcd9a61c2) Rename track motion functions and name globals | P2 / adapt | 6 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U155 | [`ac2515d483`](https://github.com/OpenRCT2/OpenRCT2/commit/ac2515d483bb4e7f3d7e09cdce79a44653f1722e) Make TileElement.h adhere to code style | P2 / adapt | 13 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U156 | [`be98df4081`](https://github.com/OpenRCT2/OpenRCT2/commit/be98df4081f86c41453fb3e8f81f2b6b3cc1cc9a) Make BannerElement.h adhere to code style | P2 / adapt | 13 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U157 | [`4af2e9d1fd`](https://github.com/OpenRCT2/OpenRCT2/commit/4af2e9d1fdf10778792543314c2922f0553a23c1) Make EntranceElement members adhere to code style | P2 / adapt | 22 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U158 | [`95025ba916`](https://github.com/OpenRCT2/OpenRCT2/commit/95025ba9166d8f88589b9484342350e507a85ff8) Make LargeSceneryElement members adhere to code style | P2 / adapt | 13 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U159 | [`2f9db0a5d4`](https://github.com/OpenRCT2/OpenRCT2/commit/2f9db0a5d48e9bc770c79ba3d647767e7efdb494) Make PathElement members adhere to code style | P2 / adapt | 28 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U160 | [`d3c54532ca`](https://github.com/OpenRCT2/OpenRCT2/commit/d3c54532cab94c7f69998f8fc54cfd98c025c17d) Make SmallSceneryElement members adhere to code style | P2 / adapt | 17 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U161 | [`25865dce31`](https://github.com/OpenRCT2/OpenRCT2/commit/25865dce31965a58a010f386b4a632886bf86461) Make SurfaceElement members adhere to code style | P2 / adapt | 28 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U162 | [`84f1946db8`](https://github.com/OpenRCT2/OpenRCT2/commit/84f1946db82d9022ecd79d9edf878e0e2eb0710a) Make TrackElement members adhere to code style | P2 / adapt | 33 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U163 | [`631804fd13`](https://github.com/OpenRCT2/OpenRCT2/commit/631804fd133aad292a75e1b5514558c1c975a50d) Make WallElement members adhere to code style | P2 / adapt | 10 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U164 | [`e501376aa3`](https://github.com/OpenRCT2/OpenRCT2/commit/e501376aa3eab0b8c2ec1a6644aea2042e5d5372) Merge pull request #26914 from Gymnasiast/refactor/tile-element-code-style | P0 / history | 64 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U165 | [`a80faffd5c`](https://github.com/OpenRCT2/OpenRCT2/commit/a80faffd5ce231205c7f1957af9ecc1be4cd00a7) Remove openrct2-ui/ includes | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U166 | [`2c8891ca8b`](https://github.com/OpenRCT2/OpenRCT2/commit/2c8891ca8b2513ec9567d54619ffbb1f362ea44a) Remove openrct2-ui/ride includes | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U167 | [`e75c1dde15`](https://github.com/OpenRCT2/OpenRCT2/commit/e75c1dde1519770617b8fb49821cdb7911041c72) Remove openrct2-ui/scripting includes | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U168 | [`8a0507708f`](https://github.com/OpenRCT2/OpenRCT2/commit/8a0507708f40f4856dd48e0c8ef6f3a975871a77) Remove openrct2-ui/title includes | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U169 | [`b95d24bb68`](https://github.com/OpenRCT2/OpenRCT2/commit/b95d24bb68218ca9a597063420e4b580592bebc9) Remove openrct2-ui/windows includes | P2 / adapt | 16 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U170 | [`34e8149e93`](https://github.com/OpenRCT2/OpenRCT2/commit/34e8149e936a785acf9cbe3cdbf6934d222b9ac9) Merge pull request #26915 from Harry-Hopkinson/remove-final-openrct2-ui-includes | P0 / history | 20 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U171 | [`bc68828c7d`](https://github.com/OpenRCT2/OpenRCT2/commit/bc68828c7dd07ac51eff12e133156a2f31c43024) Improve save field alignment in file browser window (#26916) | P5 / port | 1 | T | Align file-browser save field; validate scaled UI.  |
| U172 | [`cef7dd164b`](https://github.com/OpenRCT2/OpenRCT2/commit/cef7dd164ba8394ae0b7b43f961ded962e06ddb8) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U173 | [`d943171fae`](https://github.com/OpenRCT2/OpenRCT2/commit/d943171faefddb192ac3aa143270f449f447d2db) Create enum class for EntranceType | P2 / adapt | 19 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U174 | [`7b1b014fce`](https://github.com/OpenRCT2/OpenRCT2/commit/7b1b014fce773472ce2eef594bd8df7ef3ba9979) Create enum class for EntranceSequence | P2 / adapt | 8 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U175 | [`4ebed48631`](https://github.com/OpenRCT2/OpenRCT2/commit/4ebed48631c32af6ea32387bb5a8e10d38b56eb5) Create enum class+FlagHolder for EntranceElementFlag | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U176 | [`b854b8d2f6`](https://github.com/OpenRCT2/OpenRCT2/commit/b854b8d2f612468239dfaa052ef19e7ad2a8012d) Create enum class+FlagHolder for LargeSceneryElementFlag | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U177 | [`1bf29b9afe`](https://github.com/OpenRCT2/OpenRCT2/commit/1bf29b9afebe3c2cd7d7ddc11510ac30c5f2cbb0) Create enum class+FlagHolder for FootpathElementFlag | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U178 | [`4d7866e3ad`](https://github.com/OpenRCT2/OpenRCT2/commit/4d7866e3ad4428270840a757022dc46b6ab18c2e) Create enum class+FlagHolder for SmallSceneryElementFlag | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U179 | [`5af73fa31b`](https://github.com/OpenRCT2/OpenRCT2/commit/5af73fa31b36391daf3fca281fd978481673b684) Create enum class+FlagHolder for TrackTileElementFlag | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U180 | [`1eacb1eec8`](https://github.com/OpenRCT2/OpenRCT2/commit/1eacb1eec8a512885e4e84d546fe2980fd6b5b58) Fix formatting for MazeConstruction.cpp | P2 / port | 0 | T | Apply MazeConstruction formatting only; preserve fork maze capacity behavior.  |
| U181 | [`df06376780`](https://github.com/OpenRCT2/OpenRCT2/commit/df06376780f443c3038edbd14438cfcf66e48daf) Move indestructible cheat check to caller | P3 / adapt | 1 | C | isIndestructible reports stored track flag; apply makeAllDestructible at removal permission check. Audit fork callers.  |
| U182 | [`3937aaa967`](https://github.com/OpenRCT2/OpenRCT2/commit/3937aaa967de8c61dccca1ee0b1f999c43d35230) Merge pull request #26917 from Gymnasiast/refactor/more-enums | P0 / history | 25 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U183 | [`95b54aa4f3`](https://github.com/OpenRCT2/OpenRCT2/commit/95b54aa4f3ad66c2d1e906e64aea5af695e09244) Fix bottom toolbar button invalidation (#26921) | P5 / adapt | 0 | T | Apply final panel invalidation/resize/update-widget fixes on the chosen HUD owners; preserve fork weather timing. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U184 | [`27a9d4b3d5`](https://github.com/OpenRCT2/OpenRCT2/commit/27a9d4b3d5e8488601e9d78daea56b64a3070221) Fix #26903: no permission for making a ride visible or invisible (#26911) | P7 / port | 1 | T | Add permission coverage for ride visibility action; verify denied and allowed users.  |
| U185 | [`11547e998a`](https://github.com/OpenRCT2/OpenRCT2/commit/11547e998a4f0e6c3f2b0c05018b53ab6a024e41) Make enum class+FlagHolder for ownership flags and normalise them | P2 / adapt | 10 | C | Ownership API normalizes high-nibble flags to low-bit FlagHolder; preserve packed tile bytes, script values and fork surface-rejoin/cache checks.  |
| U186 | [`db53fd3f7f`](https://github.com/OpenRCT2/OpenRCT2/commit/db53fd3f7f1aa07549db5f509973fc57a2c4a92f) Introduce SurfaceElement::hasOwnership() | P2 / adapt | 4 | T | Ownership API normalizes high-nibble flags to low-bit FlagHolder; preserve packed tile bytes, script values and fork surface-rejoin/cache checks.  |
| U187 | [`d2783e0564`](https://github.com/OpenRCT2/OpenRCT2/commit/d2783e05642247971350f8b8c3fc6383be7c46c1) Rename some ‘OwnershipFlag’s | P2 / adapt | 7 | T | Ownership API normalizes high-nibble flags to low-bit FlagHolder; preserve packed tile bytes, script values and fork surface-rejoin/cache checks.  |
| U188 | [`26c10680d5`](https://github.com/OpenRCT2/OpenRCT2/commit/26c10680d59ad031eb9ca444b055850863f2f3bf) Move two map ownership functions out of Map.h | P2 / adapt | 5 | T | Ownership API normalizes high-nibble flags to low-bit FlagHolder; preserve packed tile bytes, script values and fork surface-rejoin/cache checks.  |
| U189 | [`11da390704`](https://github.com/OpenRCT2/OpenRCT2/commit/11da390704950cd30f1275c392140480908183a0) Merge pull request #26920 from Gymnasiast/refactor/ownership-flags | P0 / history | 10 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U190 | [`3d90510a63`](https://github.com/OpenRCT2/OpenRCT2/commit/3d90510a63d47510c6a91028616e7fb1186f1b56) Shrink and reposition top toolbar, allowing rain to be drawn (#26936) | P5 / decision | 0 | T | Split/reposition HUD, news and editor controls; migrate theme settings and 30-second fork weather preview with the new owners. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U191 | [`182efeea9a`](https://github.com/OpenRCT2/OpenRCT2/commit/182efeea9ae9c0f7e5f9ee28a20a2432fa0907d4) Fix title windows not flashing when already open (#26935) | P5 / port | 0 | T | Request attention for already-open title windows or quit-blocking save prompt; retain normal focus/pause policy.  |
| U192 | [`8ebb607fc4`](https://github.com/OpenRCT2/OpenRCT2/commit/8ebb607fc4692deefdf5108538990c49a5cc06b6) Fix #21632: Crash when loading custom image larger than 300 by 300 pixels (#26934) | P7 / port | 0 | T | Report oversized/invalid plugin images as exceptions instead of crashing or asserting; test >300px and malformed input.  |
| U193 | [`af352f18ef`](https://github.com/OpenRCT2/OpenRCT2/commit/af352f18ef113f77af6f6f8880168c15215804a3) Create functions to get a random colour (#26939) | P2 / adapt | 2 | C | Keep deterministic ScenarioRandMax separate from UI UtilRand; preserve fork timing at colour-cycle call sites.  |
| U194 | [`8ebe3965a2`](https://github.com/OpenRCT2/OpenRCT2/commit/8ebe3965a2f1e2540f8882a1d67e7b66f5470195) Re-include unistd.h in Platform.Linux.cpp (#26940) | P8 / port | 0 | T | Apply platform/CI/documentation change with relevant target check; retain fork build targets and workflows.  |
| U195 | [`8dab2cce07`](https://github.com/OpenRCT2/OpenRCT2/commit/8dab2cce07684fd6a3cbd8670bef546d0a86c4de) Split off ParkInfoPanel and DateInfoPanel from GameBottomToolbar (#26919) | P5 / decision | 7 | T | Split/reposition HUD, news and editor controls; migrate theme settings and 30-second fork weather preview with the new owners. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U196 | [`ab3065fbe1`](https://github.com/OpenRCT2/OpenRCT2/commit/ab3065fbe12a43e80a34c02ea46fb486c91d68c2) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U197 | [`b5c1570b78`](https://github.com/OpenRCT2/OpenRCT2/commit/b5c1570b782dc1587a475045491f67c9c5dcb0ac) Rename members of smaller entities (#26946) | P2 / adapt | 17 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U198 | [`f5f4b522c7`](https://github.com/OpenRCT2/OpenRCT2/commit/f5f4b522c7438b6c2ad30f28a941c4d325116f49) Replace some bottomToolbar invalidation with date/parkInfoPanel (#26948) | P5 / adapt | 1 | T | Apply final panel invalidation/resize/update-widget fixes on the chosen HUD owners; preserve fork weather timing. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U199 | [`a74f7437b9`](https://github.com/OpenRCT2/OpenRCT2/commit/a74f7437b92694dab9fbd0d6374f566854c0f4fd) Rename symbols of Peep (#26949) | P2 / adapt | 31 | T | Migrate peep flags/names while retaining fork needs, platform/transport states, happiness-based growth and time conversions. [D13](upstream-port-plan-2026-09-13.md#d13) [D14](upstream-port-plan-2026-09-13.md#d14) |
| U200 | [`bb0215abb0`](https://github.com/OpenRCT2/OpenRCT2/commit/bb0215abb0e4a48cd71e5fc8b105ce664b49d59e) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U201 | [`840f1c4f91`](https://github.com/OpenRCT2/OpenRCT2/commit/840f1c4f9147ad328700377d93d90428a53d197a) Move includes in scripting files inside ENABLE_SCRIPTING (#26950) | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U202 | [`8de0f10777`](https://github.com/OpenRCT2/OpenRCT2/commit/8de0f107774c4762496f84c2a0c2c544a380b1f1) Fix #25496: guest pickup button does not grey out when guest state changes (#26926) | P5 / adapt | 1 | T | Refresh guest pickup availability as state changes, including platform waiting/boarding and transport states.  |
| U203 | [`ad578a121d`](https://github.com/OpenRCT2/OpenRCT2/commit/ad578a121d0a6ef633c3d36523626f69949fff5d) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U204 | [`e25c301668`](https://github.com/OpenRCT2/OpenRCT2/commit/e25c301668db6ac1dd496e30463276f9905d07d5) feat: add PathNavigator (#26412) | P7 / decision | 2 | C | Add physical-path plugin navigator, not guest routing. Keep live map reads on safe owner; resolve ambiguous same-height/reordered path identity. [D09](upstream-port-plan-2026-09-13.md#d09) |
| U205 | [`ac926d5550`](https://github.com/OpenRCT2/OpenRCT2/commit/ac926d5550b21addad8fc0630c0e528d931915e5) Fix #23872: Set load/save folder for parks loaded outside the game (#26927) | P7 / port | 1 | T | Use externally opened park folder for subsequent load/save while retaining fork layered object-directory handling.  |
| U206 | [`6b2fb43861`](https://github.com/OpenRCT2/OpenRCT2/commit/6b2fb43861a9b3d35a79c01c188ccfe6b435a41b) Move interface types from Location.hpp to ScreenCoords.hpp (#26945) | P2 / adapt | 22 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U207 | [`3768b08999`](https://github.com/OpenRCT2/OpenRCT2/commit/3768b089990e75c298e359d85118043a86e821a6) Request user attention when the save prompt blocks quitting (#26933) | P5 / port | 3 | T | Request attention for already-open title windows or quit-blocking save prompt; retain normal focus/pause policy.  |
| U208 | [`d1ea87512f`](https://github.com/OpenRCT2/OpenRCT2/commit/d1ea87512ff1d793b5dc80ae49562efd9a9e2075) Fix land paint price not updating when holding Ctrl (#26954) | P3 / adapt | 0 | T | Refresh terrain paint cost under Ctrl; retain cent precision and provisional-state behavior.  |
| U209 | [`911afdbd6e`](https://github.com/OpenRCT2/OpenRCT2/commit/911afdbd6e00809c4c6a899d8b6fa906ff3e868e) Fix changelog broken by #26954 (#26956) | P1 / port | 0 | T | Apply final documentation/changelog/string correction; distinguish upstream release history from features actually ported.  |
| U210 | [`c34fee484a`](https://github.com/OpenRCT2/OpenRCT2/commit/c34fee484abbbec39b0efb1b032da3b129a02877) Fix result.position.z of WallPlaceAction (#26441) | P3 / adapt | 1 | C | Return actual resolved placement Z from Query and Execute; preserve fork safe handles and placement mutation ordering.  |
| U211 | [`34762ceb81`](https://github.com/OpenRCT2/OpenRCT2/commit/34762ceb81f8799bc4078c3425ccc124990ef06f) Fix #25558: plugin buttons ignore an explicitly set border (#26925) | P7 / port | 1 | T | Honor explicit plugin button border; test hidden/type state with final widget migration.  |
| U212 | [`4cce29b5a9`](https://github.com/OpenRCT2/OpenRCT2/commit/4cce29b5a9aceb14321ef7350457a17e896adf16) Fix #26056: Freeze when trying to download all objects on save where they are missing | P7 / adapt | 0 | C | Retain async request futures to avoid blocking temporary destruction; audit cancel/restart/window shutdown and late callbacks against fork background I/O ownership.  |
| U213 | [`44b178e30f`](https://github.com/OpenRCT2/OpenRCT2/commit/44b178e30f6b072ba86ca7a6edacf4a09054858f) Make all places returning std::async [[nodiscard]] | P7 / adapt | 0 | T | Retain async request futures to avoid blocking temporary destruction; audit cancel/restart/window shutdown and late callbacks against fork background I/O ownership.  |
| U214 | [`2449e06d9c`](https://github.com/OpenRCT2/OpenRCT2/commit/2449e06d9c360e494cf8da9e5cc6cffe9feeea7a) Merge pull request #26955 from tupaschoal/future_hang_download_all | P0 / history | 0 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U215 | [`d005dda508`](https://github.com/OpenRCT2/OpenRCT2/commit/d005dda5083f28cd3ed7442804241b6c3f3efdb0) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U216 | [`7bf6a4b4fb`](https://github.com/OpenRCT2/OpenRCT2/commit/7bf6a4b4fbf0b0b384c7635629be0b1a50b86297) Fix result.position.z of SmallSceneryPlaceAction (#26958) | P3 / adapt | 1 | C | Return actual resolved placement Z from Query and Execute; preserve fork safe handles and placement mutation ordering.  |
| U217 | [`54b7abd25a`](https://github.com/OpenRCT2/OpenRCT2/commit/54b7abd25ae34ab15fd06ced429c6d5bf9e1b710) Fix: When showing missing objects, some types show up as ‘Unknown type’ | P7 / port | 0 | T | Show missing object type names and raise the error above the load window; preserve object lookup precedence.  |
| U218 | [`4fdcae5092`](https://github.com/OpenRCT2/OpenRCT2/commit/4fdcae5092682e3839b8c332a485c51b471f36ad) Fix: The window with missing objects is displayed under the ‘Load game’ window | P7 / port | 0 | T | Show missing object type names and raise the error above the load window; preserve object lookup precedence.  |
| U219 | [`3dc5a1a7ef`](https://github.com/OpenRCT2/OpenRCT2/commit/3dc5a1a7efb7712d2a43b9bdd01f76c22cb86be3) Merge pull request #26965 from Gymnasiast/fix/object-load-error-stuff | P0 / history | 0 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U220 | [`7cc48f5c14`](https://github.com/OpenRCT2/OpenRCT2/commit/7cc48f5c142b46d0f8a5ef50e7c9dac969fb530b) Close #25362: Dragged footpaths over hills are disconnected (#25913) | P3 / decision | 1 | C | Automatically slope terrain-following drag paths; also fix stale previews and modifier switching. Use fork actions/invalidation. [D05](upstream-port-plan-2026-09-13.md#d05) |
| U221 | [`f66c0a65f9`](https://github.com/OpenRCT2/OpenRCT2/commit/f66c0a65f9da330c164cfe2625bcd30fbfd2497b) Move PaletteMap implementation to PaletteMap.cpp (#26969) | P6 / adapt | 2 | T | Move PaletteMap implementation with its Debug include fix; retain Vulkan palette/texture consumers.  |
| U222 | [`1f76487769`](https://github.com/OpenRCT2/OpenRCT2/commit/1f76487769475f36465e2052baacfb77a4b51a9c) Move gPickupPeep* to PickupPeep.{cpp,h} | P6 / adapt | 8 | T | Adopt PickupPeep API ownership while preserving fork image+position+zoom value snapshot and asynchronous packet lifetime.  |
| U223 | [`aa65346003`](https://github.com/OpenRCT2/OpenRCT2/commit/aa653460037eae7764fbedfbf81df01e07face4f) Refactor pickup peep into namespace, hide globals, use ScreenCoordsXY | P6 / adapt | 5 | T | Adopt PickupPeep API ownership while preserving fork image+position+zoom value snapshot and asynchronous packet lifetime.  |
| U224 | [`2e7cb6c4bf`](https://github.com/OpenRCT2/OpenRCT2/commit/2e7cb6c4bfa66559e4f12217a5712eb6368a43ba) Merge pull request #26970 from Gymnasiast/refactor/pickup-peep | P0 / history | 8 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U225 | [`3dd304a8d5`](https://github.com/OpenRCT2/OpenRCT2/commit/3dd304a8d506c422fb3ac0ff1dba566b4f45eece) Merge pull request #26972 from fasma-oO/fasma-contribution | P2 / adapt | 0 | T | Single-parent commit despite Merge subject: rename Trigonometry constants and both vehicle consumers.  |
| U226 | [`e356b3cae6`](https://github.com/OpenRCT2/OpenRCT2/commit/e356b3cae6f4952b819b3236eda4260ea0df1fe5) Update reference to Duktape in readme.txt | P1 / port | 0 | T | Apply final documentation/changelog/string correction; distinguish upstream release history from features actually ported.  |
| U227 | [`1557aca820`](https://github.com/OpenRCT2/OpenRCT2/commit/1557aca82098edaa3637d9ef296807a1ed4f10d1) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U228 | [`4872effb80`](https://github.com/OpenRCT2/OpenRCT2/commit/4872effb804d6bf7ec3e4cba7e4e46217321832d) Make vehicle subposition writable in API (#26971) | P7 / decision | 1 | C | Add validated mutable vehicle subposition, track-change update and interpolation reset; verify fork seat/sample/presentation consistency. [D08](upstream-port-plan-2026-09-13.md#d08) |
| U229 | [`146a7427a3`](https://github.com/OpenRCT2/OpenRCT2/commit/146a7427a39ffb14e184816d70510ed5137a76b1) Refactor FontStyle constants to kCamelCase (#26974) | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U230 | [`00df3c16b7`](https://github.com/OpenRCT2/OpenRCT2/commit/00df3c16b7ea185169b53d118608abe7fcef0b15) Rename TRACK_DESIGN_PREVIEW_MAP_SIZE to kTrackDesignPreviewMapSize | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U231 | [`7a855d8c3b`](https://github.com/OpenRCT2/OpenRCT2/commit/7a855d8c3b161dd9513168381e9b33738124c9fe) Rename RCT2ToOpenRCT2LanguageId to kRCT2ToOpenRCT2LanguageId | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U232 | [`a5edcd5780`](https://github.com/OpenRCT2/OpenRCT2/commit/a5edcd5780ab9749bb138d9b6af0be0de5abb1d5) Rename RCT1ResearchFlagsSeparator to kRCT1ResearchFlagsSeparator | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U233 | [`65dfdd3030`](https://github.com/OpenRCT2/OpenRCT2/commit/65dfdd3030bfe5da17cc3324dc2a96a1d5766dff) Rename MASK_SIZE to kMaskSize | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U234 | [`d611a7d560`](https://github.com/OpenRCT2/OpenRCT2/commit/d611a7d560d7f50ff7bccc85c2fdcd2d88dd0e27) Rename TRACK_NEARBY_SCENERY_DISTANCE to kTrackNearbySceneryDistance | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U235 | [`19e2404ac2`](https://github.com/OpenRCT2/OpenRCT2/commit/19e2404ac26d767628db75f4d65de3b22b33c4d1) Rename SYNCHRONISED_VEHICLE_COUNT to kSynchronisedVehicleCount | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U236 | [`e9b9979ee3`](https://github.com/OpenRCT2/OpenRCT2/commit/e9b9979ee318f0d9c56add4da5b71fdbc6b7b613) Rename NUM_HookTypeS to kHookTypeCount | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U237 | [`f10a778c7a`](https://github.com/OpenRCT2/OpenRCT2/commit/f10a778c7a847a9af0f1335c259ece6b3a2d940e) Rename MIN_TILE_ELEMENTS to kMinTileElements | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U238 | [`bc834240fd`](https://github.com/OpenRCT2/OpenRCT2/commit/bc834240fd83f7279ea23818e0909c4b7ae5b66a) Rename SCALE and PI_SCALED to kScale and kScaled | P2 / adapt | 1 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U239 | [`99b16bc638`](https://github.com/OpenRCT2/OpenRCT2/commit/99b16bc638b5d075bb4b42bcdf1b2d6b48080a99) Merge pull request #26979 from Gymnasiast/refactor/track-design-preview-map-size | P0 / history | 5 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U240 | [`d414239d5b`](https://github.com/OpenRCT2/OpenRCT2/commit/d414239d5b5a25c312988e91dac976490c6c3f14) Fix #18197: Track Designs Manager does not autoload scenery | P7 / adapt | 1 | T | Load scenery for track-design manager, rebuild ride-entry map only for ride objects, refresh after deletion; retain temporary-map isolation.  |
| U241 | [`dc6fd8a913`](https://github.com/OpenRCT2/OpenRCT2/commit/dc6fd8a913fefe94d87cdc41c29851bdcc9edeec) Do not reload ride type to ride entry map unless actually loading a ride object | P7 / adapt | 1 | T | Load scenery for track-design manager, rebuild ride-entry map only for ride objects, refresh after deletion; retain temporary-map isolation.  |
| U242 | [`d7bba3b92e`](https://github.com/OpenRCT2/OpenRCT2/commit/d7bba3b92e2f14d7a0987cee11c2457fb31bfc94) Merge pull request #26975 from Gymnasiast/fix/18197 | P0 / history | 2 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U243 | [`f66c153a10`](https://github.com/OpenRCT2/OpenRCT2/commit/f66c153a101cc49d4f77eb53a52915dea785848e) Replace SDL.h headers with SDL sub headers (#26990) | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U244 | [`341d3b834a`](https://github.com/OpenRCT2/OpenRCT2/commit/341d3b834a9894a0d673afda833651aebb671aac) Fix PaletteMap.cpp compilation in debug mode (#26989) | P6 / adapt | 0 | T | Move PaletteMap implementation with its Debug include fix; retain Vulkan palette/texture consumers.  |
| U245 | [`31004bbf33`](https://github.com/OpenRCT2/OpenRCT2/commit/31004bbf332455a82084d466a9e081dfc760aebd) Fix #24610: Graph tabs on Finances window get a little bit longer every time you switch between them (#26976) | P5 / adapt | 0 | T | Stop finance graph tabs growing on repeated switches; preserve cent values and fork finance time labels.  |
| U246 | [`79be249f71`](https://github.com/OpenRCT2/OpenRCT2/commit/79be249f71ad2a616f0f521e6ec3ce873f59d732) Remove dead code testing for ImageIndex -2 (#26993) | P6 / adapt | 0 | T | Remove obsolete ImageIndex -2 handling; audit fork-owned command/sprite sentinel usage.  |
| U247 | [`63a787c2ae`](https://github.com/OpenRCT2/OpenRCT2/commit/63a787c2ae68478c11ceaefe91b01fec16b3eeac) Guard bottom toolbar resizing to prevent nullptr deref (#26994) | P5 / adapt | 1 | T | Apply final panel invalidation/resize/update-widget fixes on the chosen HUD owners; preserve fork weather timing. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U248 | [`1e8d8707c0`](https://github.com/OpenRCT2/OpenRCT2/commit/1e8d8707c08fd654fc9e628944a44c38e67d3c32) Fix partial/no car drawn on vehicle tab if ride has <1 cars per train (#26992) | P6 / adapt | 1 | T | Guard fewer-than-one visible car in ride preview and Splash Boats paint recursion; use fork presentation snapshot owners.  |
| U249 | [`bcdb769cfd`](https://github.com/OpenRCT2/OpenRCT2/commit/bcdb769cfd3dc57b8524ac7f476ea64e42596128) Fix #26802: Splash Boats draw recursion (#26991) | P6 / adapt | 1 | C | Guard fewer-than-one visible car in ride preview and Splash Boats paint recursion; use fork presentation snapshot owners.  |
| U250 | [`35898a258c`](https://github.com/OpenRCT2/OpenRCT2/commit/35898a258c35374a458666771990a6e137004a07) Refactor TRACK_ELEMENT_*_MASK and ObjectSelectionFlag to constexpr/enum class (#26986) | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U251 | [`e16a74030d`](https://github.com/OpenRCT2/OpenRCT2/commit/e16a74030d4cdbc80a0e21f05271a7f696b8bff3) Fix #18415: Preview in Track Designs Manager not updating after a delete (#27003) | P7 / adapt | 1 | T | Load scenery for track-design manager, rebuild ride-entry map only for ride objects, refresh after deletion; retain temporary-map isolation.  |
| U252 | [`9d502af244`](https://github.com/OpenRCT2/OpenRCT2/commit/9d502af2442facf52d37f11c043d2acdf9a09f92) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U253 | [`c8ab334060`](https://github.com/OpenRCT2/OpenRCT2/commit/c8ab33406096069c77ed979d9cac236e8e98748a) Fix track style buttons overlapping with banking ones (#27006) | P5 / adapt | 1 | T | Port the final separated banking/speed widget design and overlap fix, including restored genuine widget-type switching.  |
| U254 | [`d687a06a22`](https://github.com/OpenRCT2/OpenRCT2/commit/d687a06a2244dfc452dd09f46c58bfd27282033b) Add scenario patching feature to rename rides | P7 / adapt | 1 | T | Extend scenario patch schema for ride names, clearing and ID arrays; preserve fork topology changes and import repair.  |
| U255 | [`f5a3ff521d`](https://github.com/OpenRCT2/OpenRCT2/commit/f5a3ff521d563cc415cd6ee5dbee2575fd3ef4b8) Patch Heide-Park to correct some names | P7 / port | 0 | T | Adopt final scenario name corrections through scenario import patching; verify affected scenario hashes and custom-name preservation.  |
| U256 | [`0ea046196d`](https://github.com/OpenRCT2/OpenRCT2/commit/0ea046196d5d4947d42832bf84746870f3d696b5) Patch Diamond Heights to correct spelling of ‘Doppelgänger’ | P7 / port | 0 | T | Adopt final scenario name corrections through scenario import patching; verify affected scenario hashes and custom-name preservation.  |
| U257 | [`7a6a02e35c`](https://github.com/OpenRCT2/OpenRCT2/commit/7a6a02e35c89fcd67cd858cf880a922fc7418a11) Patch Alton Towers to correct spelling of ‘Hex... the Legend of the Towers’ | P7 / port | 0 | T | Adopt final scenario name corrections through scenario import patching; verify affected scenario hashes and custom-name preservation.  |
| U258 | [`7aca955172`](https://github.com/OpenRCT2/OpenRCT2/commit/7aca955172a67da773c4fc79250032fa59c35e7d) Extend scenario patch for Okinawa Coast to CD version | P7 / decision | 0 | C | Apply existing Okinawa ownership corrections to the CD scenario hash: changes starting land/construction rights, not just names. [D15](upstream-port-plan-2026-09-13.md#d15) |
| U259 | [`f31588edfc`](https://github.com/OpenRCT2/OpenRCT2/commit/f31588edfc4cb70151f84b5aa5c041de29d6a12b) Add ability to clear a ride’s name, in addition to setting it | P7 / adapt | 1 | T | Extend scenario patch schema for ride names, clearing and ID arrays; preserve fork topology changes and import repair.  |
| U260 | [`5d23833cc3`](https://github.com/OpenRCT2/OpenRCT2/commit/5d23833cc3c82dd07466d51be842428bd49d13ee) Clear ‘Bullet Coaster 1’ name in Okinawa Coast (as an example) | P7 / port | 0 | T | Adopt final scenario name corrections through scenario import patching; verify affected scenario hashes and custom-name preservation.  |
| U261 | [`436bc0d53e`](https://github.com/OpenRCT2/OpenRCT2/commit/436bc0d53eeda036930c60b5d6ad5a84d9934bd7) Merge pull request #26996 from Gymnasiast/feature/rename-ride-in-scenariopatch | P0 / history | 1 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U262 | [`08aa13fee3`](https://github.com/OpenRCT2/OpenRCT2/commit/08aa13fee37938f6259507ee081a0af43a8b4491) Fix: sanitise packed object filenames | P7 / port | 0 | C | Sanitize packed object storage filename, keep identifier intact, and provide empty-name fallback; test containment/collisions in selected object directory.  |
| U263 | [`8e40456478`](https://github.com/OpenRCT2/OpenRCT2/commit/8e40456478268022cd2c29cd23ac5bde50f2740d) Merge pull request #26966 from Gymnasiast/sanitise-object-filenames | P0 / history | 0 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U264 | [`c7bee6257d`](https://github.com/OpenRCT2/OpenRCT2/commit/c7bee6257df77a5ada33d1475b802c5d2ab49252) Create enum and flagholder for StationObjectFlags | P2 / adapt | 5 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U265 | [`672bd553fe`](https://github.com/OpenRCT2/OpenRCT2/commit/672bd553fe27d5c1c4ed786d3973da822f9e8421) Add GitHub pull request template (#27007) | P8 / port | 0 | T | Apply platform/CI/documentation change with relevant target check; retain fork build targets and workflows.  |
| U266 | [`340bb07626`](https://github.com/OpenRCT2/OpenRCT2/commit/340bb07626c59c62d4b044af23249c5949fbf01b) Fix #26960: Vertical twists not available on the Inverted Impulse Coaster (#27012) | P5 / adapt | 1 | C | Repairs widget-visibility regression introduced by d517f19ce2; preserve existing vertical-twist access. No new track group or rating change.  |
| U267 | [`57de7b82be`](https://github.com/OpenRCT2/OpenRCT2/commit/57de7b82be001d497bee266a50dbfd118b59ba92) Switch characters from decimal to the more common hex notation | P1 / port | 0 | T | Take final sprite-font glyph definitions/assets and numeric codepoints as one package; check money glyphs with cent formatting.  |
| U268 | [`52a684a0da`](https://github.com/OpenRCT2/OpenRCT2/commit/52a684a0dae90fb087f5cb3df0001e10dbba6d4f) Fix #15891: Add lower case hard sign | P1 / port | 1 | T | Take final sprite-font glyph definitions/assets and numeric codepoints as one package; check money glyphs with cent formatting.  |
| U269 | [`a0840b2a71`](https://github.com/OpenRCT2/OpenRCT2/commit/a0840b2a711c8a97db3a6464ec9a2f8cc3dc854f) Close #19000: Add sprite font glyph for the Won | P1 / port | 2 | T | Take final sprite-font glyph definitions/assets and numeric codepoints as one package; check money glyphs with cent formatting.  |
| U270 | [`e9c7f6667c`](https://github.com/OpenRCT2/OpenRCT2/commit/e9c7f6667ccc2d6e44fd8fa5f4bfc2284d7d94ed) Move currency glyphs to currency folder | P1 / port | 0 | T | Take final sprite-font glyph definitions/assets and numeric codepoints as one package; check money glyphs with cent formatting.  |
| U271 | [`9d59ed34dd`](https://github.com/OpenRCT2/OpenRCT2/commit/9d59ed34dddd927b80b19dd9656f25fa6b36f0f1) Close #21441: Add sprite font glyph for the Hryvnia (₴) | P1 / port | 1 | T | Take final sprite-font glyph definitions/assets and numeric codepoints as one package; check money glyphs with cent formatting.  |
| U272 | [`1ce70b8bf8`](https://github.com/OpenRCT2/OpenRCT2/commit/1ce70b8bf8c5ad282ffc47850501a5ce885154d2) Redesign f-with-hook (guilder) glyph | P1 / port | 0 | T | Take final sprite-font glyph definitions/assets and numeric codepoints as one package; check money glyphs with cent formatting.  |
| U273 | [`550fe4be85`](https://github.com/OpenRCT2/OpenRCT2/commit/550fe4be8588a5f176fff1e00b8bd41f9427051f) Merge pull request #27015 from Gymnasiast/more-sprite-font-characters | P0 / history | 2 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U274 | [`21e37e12f9`](https://github.com/OpenRCT2/OpenRCT2/commit/21e37e12f93bcf40c841485ff0907efeb0931471) Rework EditorBottomToolbar into EditorStepController (#26961) | P5 / decision | 6 | T | Split/reposition HUD, news and editor controls; migrate theme settings and 30-second fork weather preview with the new owners. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U275 | [`763d42cad7`](https://github.com/OpenRCT2/OpenRCT2/commit/763d42cad783512f51c3299ff216d46f3b8f5db9) Fix editor inadvertently showing the pause and fastforward buttons (#27021) | P5 / port | 0 | T | Apply final editor/multiplayer pause-speed-chat visibility rules; preserve fork local speed/turbo behavior. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U276 | [`4beb0ec8e0`](https://github.com/OpenRCT2/OpenRCT2/commit/4beb0ec8e0a178542316c7d6a0b1a30d01c84a20) Put lesser-used arguments to MapCanConstructWithClearAt() into a struct (#27010) | P3 / adapt | 11 | C | Introduce MapProposedConstructionInfo at fork clearance owner without replacing ClearanceResult::elementErased or compaction-safe traversal.  |
| U277 | [`c776757fb2`](https://github.com/OpenRCT2/OpenRCT2/commit/c776757fb2718e6c4258792e552bf561ad11f639) Fix #22500: LandSetHeightAction does not provide an error title (#27018) | P3 / adapt | 3 | T | Supply land-height error title with remapped upstream string ID; retain mutation-safe floating-obstruction checks.  |
| U278 | [`b92dd1d78c`](https://github.com/OpenRCT2/OpenRCT2/commit/b92dd1d78ccdf65d07789a0387a3a9167ed2e865) Use RCTC ride names in RCT1 scenarios (#27017) | P7 / port | 0 | T | Adopt final scenario name corrections through scenario import patching; verify affected scenario hashes and custom-name preservation.  |
| U279 | [`0fb70602d3`](https://github.com/OpenRCT2/OpenRCT2/commit/0fb70602d3404c43c19a02400ac220045a460d8f) Convert contents of EntityRegistry.h to camelCase (#27019) | P2 / adapt | 56 | T | Rename entity registry/tweener APIs across fork pool, checksums, presentation snapshots and visual lifecycle consumers.  |
| U280 | [`69c8522d43`](https://github.com/OpenRCT2/OpenRCT2/commit/69c8522d4367f84770aa31b32db78efa5a808d10) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U281 | [`4fa8219e1f`](https://github.com/OpenRCT2/OpenRCT2/commit/4fa8219e1f571d96488f430ab678de8edea1e51f) Small changelog correction | P1 / port | 0 | T | Apply final documentation/changelog/string correction; distinguish upstream release history from features actually ported.  |
| U282 | [`9adbdd643b`](https://github.com/OpenRCT2/OpenRCT2/commit/9adbdd643bb2cd3413f2f8952c140d002504d59a) Remove AddFuncs calls in ScObject.hpp (#26978) | P7 / adapt | 1 | T | Replace AddFuncs registration with final prototype/class setup; retain fork QuickJS /WX count fixes and verify teardown/re-registration.  |
| U283 | [`fae5cf0f9d`](https://github.com/OpenRCT2/OpenRCT2/commit/fae5cf0f9d7e181c7e769accb5db4560400716ed) Fix #22854: network connections of plugins pile up when loading another park (#26928) | P7 / port | 0 | T | Dispose plugin network connections when stopping/unloading plugins; verify park reload and remote plugin lifecycle.  |
| U284 | [`ad94483ff1`](https://github.com/OpenRCT2/OpenRCT2/commit/ad94483ff1029903c3e7f3ddf712c59b16c61ee7) Allow getting and setting caret position for plugin textbox while it's in focus (#26938) | P7 / port | 2 | T | Expose focused textbox caret getter/setter; verify focus transitions and bounds with final widget API.  |
| U285 | [`3e70c47a88`](https://github.com/OpenRCT2/OpenRCT2/commit/3e70c47a88994e05995f87c9d6c1f5a349a9f22e) Fix UI misbehaviors when releasing mouse outside of game window (#27004) | P5 / adapt | 0 | T | Fix outside-window mouse release and gate held/gamepad input on focus; clear accumulated scroll while retaining turbo input pumping.  |
| U286 | [`f219737349`](https://github.com/OpenRCT2/OpenRCT2/commit/f219737349cdb1d6425f15f219fe3a12cc02444b) Fix #7858: dropdown stays open when its parent window is closed (#27027) | P5 / port | 0 | T | Close orphan dropdowns and reject stale hover indices after page changes; exercise fork dynamic ride widgets.  |
| U287 | [`abcc8f90ca`](https://github.com/OpenRCT2/OpenRCT2/commit/abcc8f90caed10382875b8c9e81b9238c6597102) Turn UnicodeChar into a strong enum, rename for codestyle | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U288 | [`db9ab61b28`](https://github.com/OpenRCT2/OpenRCT2/commit/db9ab61b280a48db047eb18e1ca9eed16564e44a) Turn CSChar into a strong enum, rename for codestyle | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U289 | [`bb870ce4fa`](https://github.com/OpenRCT2/OpenRCT2/commit/bb870ce4fad1f1a6d6fd9dead58f6fe918455765) Turn CodePage into a strong enum | P2 / adapt | 0 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U290 | [`9279d06590`](https://github.com/OpenRCT2/OpenRCT2/commit/9279d0659011c8acd42d89c720c46639f3fa53af) Merge pull request #27016 from Gymnasiast/refactor/unicode-char-strong-enum-fix-warning | P0 / history | 0 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U291 | [`144aebc9df`](https://github.com/OpenRCT2/OpenRCT2/commit/144aebc9df497381fafb4c8643d9a0a3ba6cfe7b) Convert contents of EntityTweener.h to camelCase (#27029) | P2 / adapt | 12 | T | Rename entity registry/tweener APIs across fork pool, checksums, presentation snapshots and visual lifecycle consumers.  |
| U292 | [`01b4f05421`](https://github.com/OpenRCT2/OpenRCT2/commit/01b4f05421347b6154ff4d2801d8a11553c99ce3) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U293 | [`1452f71be9`](https://github.com/OpenRCT2/OpenRCT2/commit/1452f71be9e100619cd9fed67b08bd4d36988690) Prune outdated pragma warning disable directives (#27028) | P2 / adapt | 4 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U294 | [`3a50f2378f`](https://github.com/OpenRCT2/OpenRCT2/commit/3a50f2378f4ed4ab06997b51edbb6dac04904d12) Fix #24520: close button ignores sprite font with enlarged UI (#27009) | P5 / port | 2 | T | Respect enlarged sprite font in close button; test scaled UI and theme fonts.  |
| U295 | [`ff9dc55921`](https://github.com/OpenRCT2/OpenRCT2/commit/ff9dc55921f3bc81d8aaeba12d083ae34f00c0b2) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U296 | [`4be8013397`](https://github.com/OpenRCT2/OpenRCT2/commit/4be80133970f12c5c72c945f2140c907ff402db0) Refactor Guest.h members and EASTEREGG_PEEP_NAME to enum class (#27038) | P2 / adapt | 5 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U297 | [`29a15582c1`](https://github.com/OpenRCT2/OpenRCT2/commit/29a15582c100faeb68a7a4c4ed6b3b6897772961) Fix #27023: Transparent plugin sprites do not work correctly in software rendering mode (#27025) | P6 / adapt | 0 | C | Mark custom image transparency correctly; verify software reference and Vulkan indexed sprite cache refresh.  |
| U298 | [`f1faf90ed4`](https://github.com/OpenRCT2/OpenRCT2/commit/f1faf90ed4fe5849f7a9fe07a0afbc8eb3a1ca9e) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U299 | [`4a081c584c`](https://github.com/OpenRCT2/OpenRCT2/commit/4a081c584c22f411cdf6ff80a387d2ef15327501) Update backtrace token for v0.5.5 | P8 / adapt | 1 | T | Review release crash-report metadata against fork deployment; do not treat upstream credentials/configuration as fork telemetry policy.  |
| U300 | [`bf1c99fad5`](https://github.com/OpenRCT2/OpenRCT2/commit/bf1c99fad5c9165294b1958dfa2dc266b437e9bb) Small rewording in changelog | P1 / port | 0 | T | Apply final documentation/changelog/string correction; distinguish upstream release history from features actually ported.  |
| U301 | [`96a6638263`](https://github.com/OpenRCT2/OpenRCT2/commit/96a6638263408ee2b00110b77f06e7328eaaf211) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U302 | [`3ae38cf3d7`](https://github.com/OpenRCT2/OpenRCT2/commit/3ae38cf3d758e6e619730761d1eb81ce921e26d7) Merge branch 'develop' | P0 / history | 267 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U303 | [`8694e34836`](https://github.com/OpenRCT2/OpenRCT2/commit/8694e3483690323b6a75fa7264b6c58116f51f31) Releasse v0.5.5 | P8 / adapt | 2 | T | Record final release/version state, preserving andersonhk protocol flavor and advancing fork revision for accepted simulation/action changes.  |
| U304 | [`d1ec88619d`](https://github.com/OpenRCT2/OpenRCT2/commit/d1ec88619d325d123b7eab96550c3e7999cc5aec) Merge branch 'master' into develop | P0 / history | 2 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U305 | [`ee15855bf3`](https://github.com/OpenRCT2/OpenRCT2/commit/ee15855bf32cce8f986fb675371876f9e957ac02) Start v0.5.6 | P8 / adapt | 0 | T | Record final release/version state, preserving andersonhk protocol flavor and advancing fork revision for accepted simulation/action changes.  |
| U306 | [`369341a019`](https://github.com/OpenRCT2/OpenRCT2/commit/369341a01916a11579a5815caa2f1169b2c94465) Rename DataSerialiser method (#27058) | P2 / adapt | 6 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U307 | [`453ddb49b8`](https://github.com/OpenRCT2/OpenRCT2/commit/453ddb49b8c611c0135051bccdad5294db6b530c) Rename FileScanner fields and methods (#27055) | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U308 | [`48242d86d6`](https://github.com/OpenRCT2/OpenRCT2/commit/48242d86d65a5e8638c8cff7b5194d179a797784) Refactor PatrolArea.h members and constexpr code style (#27050) | P2 / adapt | 3 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U309 | [`9f9b14d0b8`](https://github.com/OpenRCT2/OpenRCT2/commit/9f9b14d0b882296b4250e2ee9b09f4118aec27a4) Allow using arrays for ride ids in scenario patches | P7 / adapt | 1 | T | Extend scenario patch schema for ride names, clearing and ID arrays; preserve fork topology changes and import repair.  |
| U310 | [`6d06f9a15e`](https://github.com/OpenRCT2/OpenRCT2/commit/6d06f9a15ea2ba9525473483d3e2e6325590a39f) Fix unlocalised names in RCT2 base | P7 / port | 0 | T | Adopt final scenario name corrections through scenario import patching; verify affected scenario hashes and custom-name preservation.  |
| U311 | [`99cdd3892a`](https://github.com/OpenRCT2/OpenRCT2/commit/99cdd3892a32b4628b65f8dd4b383582a9da3dc5) Fix unlocalised names in Wacky Worlds | P7 / port | 0 | T | Adopt final scenario name corrections through scenario import patching; verify affected scenario hashes and custom-name preservation.  |
| U312 | [`394e588fc7`](https://github.com/OpenRCT2/OpenRCT2/commit/394e588fc7c94512a0adad3f1fa1f1dc4c964237) Fix unnamed rides appearing in English in base/WW scenarios (#27052) | P0 / history | 1 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U313 | [`e70f51a68e`](https://github.com/OpenRCT2/OpenRCT2/commit/e70f51a68ead08d4722b4827a2b7ebfe133cc8d9) Add changelog entry for #27052 | P1 / port | 0 | T | Apply final documentation/changelog/string correction; distinguish upstream release history from features actually ported.  |
| U314 | [`1cee317188`](https://github.com/OpenRCT2/OpenRCT2/commit/1cee3171888071cf4283a5579007b644b606ae1b) Fix #24457: Yellow is always rendered as third remap in Ride window (#27063) | P6 / adapt | 1 | T | Correct third-remap handling in ride window; verify Vulkan colour remapping and fork vehicle controls.  |
| U315 | [`97b91941eb`](https://github.com/OpenRCT2/OpenRCT2/commit/97b91941eba665b67185c7b8f5f4dff2ab03410d) Rename RideStation struct members in Ride.h (#27072) | P2 / adapt | 29 | T | Rename station fields across fork serialization, directed-leg ratings, platform staging and duration conversions; preserve values and behavior. [D12](upstream-port-plan-2026-09-13.md#d12) [D14](upstream-port-plan-2026-09-13.md#d14) |
| U316 | [`b0b81afcfa`](https://github.com/OpenRCT2/OpenRCT2/commit/b0b81afcfaef418c9fa4825992f9f13319d59b1a) Create function to get active water type | P6 / adapt | 3 | T | Adopt final palette/screen/line header ownership with fork-only GPU consumers; retain immutable presentation state.  |
| U317 | [`49eeb35261`](https://github.com/OpenRCT2/OpenRCT2/commit/49eeb352616444fa3baec7b4184a7d116127b846) Rename ColourPalette.h to PaletteType.h | P6 / adapt | 5 | T | Adopt final palette/screen/line header ownership with fork-only GPU consumers; retain immutable presentation state.  |
| U318 | [`5491820d02`](https://github.com/OpenRCT2/OpenRCT2/commit/5491820d0291e171245d997983057c386daa928e) Move game palette-related stuff into Palette.{cpp.h} | P6 / adapt | 12 | T | Adopt final palette/screen/line header ownership with fork-only GPU consumers; retain immutable presentation state.  |
| U319 | [`ef7707a9df`](https://github.com/OpenRCT2/OpenRCT2/commit/ef7707a9df3b3eec12b522090cfc6a585f70aa11) Merge pull request #27073 from Gymnasiast/refactor/palette-stuff-2 | P0 / history | 15 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U320 | [`83cbac90ec`](https://github.com/OpenRCT2/OpenRCT2/commit/83cbac90ec522b3b4e28141e600c62080a065534) Move stuff that affects the whole screen/viewport out of Drawing.{cpp,h} | P6 / adapt | 24 | T | Adopt final palette/screen/line header ownership with fork-only GPU consumers; retain immutable presentation state.  |
| U321 | [`c90ca119eb`](https://github.com/OpenRCT2/OpenRCT2/commit/c90ca119eb740d15313a7bcb3d0a5047129e342a) Move line drawing stuff into Line.{cpp.h} | P6 / adapt | 5 | T | Adopt final palette/screen/line header ownership with fork-only GPU consumers; retain immutable presentation state.  |
| U322 | [`96ba41b5e6`](https://github.com/OpenRCT2/OpenRCT2/commit/96ba41b5e6aa82222b525d198478aeeb149f9f9a) Move ImageId::GetCatalogue() out of Drawing.cpp | P6 / adapt | 2 | T | Adopt final palette/screen/line header ownership with fork-only GPU consumers; retain immutable presentation state.  |
| U323 | [`fcdf61e009`](https://github.com/OpenRCT2/OpenRCT2/commit/fcdf61e009b2f3da81cf3ba019174c353bad9709) Move GfxFilterPixel to Rectangle.cpp | P6 / adapt | 2 | T | Adopt final palette/screen/line header ownership with fork-only GPU consumers; retain immutable presentation state.  |
| U324 | [`c3a2fe508f`](https://github.com/OpenRCT2/OpenRCT2/commit/c3a2fe508fbf2d583ebe4dc14781dd45006bb5cf) Move screen and line functions out of Drawing.{cpp,h} (#27079) | P0 / history | 26 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U325 | [`b8a5e2431c`](https://github.com/OpenRCT2/OpenRCT2/commit/b8a5e2431cde79b75656d05fc82348d886dac409) Fix #26754: Guard stale hover widget index when switching window pages (#27086) | P5 / port | 0 | T | Close orphan dropdowns and reject stale hover indices after page changes; exercise fork dynamic ride widgets.  |
| U326 | [`092783cc11`](https://github.com/OpenRCT2/OpenRCT2/commit/092783cc1160be24f2acbed023217558bdb1f41a) Rename Location.hpp members to camelCase (#27085) | P2 / adapt | 62 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U327 | [`bcf80e292e`](https://github.com/OpenRCT2/OpenRCT2/commit/bcf80e292e76d1f05fe12e745d1d7a1754f8f390) Fix 'update available' widget position (#27091) | P5 / adapt | 0 | T | Apply final panel invalidation/resize/update-widget fixes on the chosen HUD owners; preserve fork weather timing. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U328 | [`bb11851664`](https://github.com/OpenRCT2/OpenRCT2/commit/bb118516646347d73e7eb0d30de9eef2f5b16043) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U329 | [`7164b12fd6`](https://github.com/OpenRCT2/OpenRCT2/commit/7164b12fd693267d3cfe0c21e681d64158b3c682) Fix #27056: Loading water palettes from plugin would not update the palette correctly (#27057) | P6 / adapt | 0 | C | Reload palette after plugin water object load using final Palette API; refresh owned GPU palette state for single and array loads.  |
| U330 | [`ced6f35f9f`](https://github.com/OpenRCT2/OpenRCT2/commit/ced6f35f9f4d2b138fec70c5d78d044b04a024e0) Move changelog entry for #27056 to correct place | P1 / port | 0 | T | Apply final documentation/changelog/string correction; distinguish upstream release history from features actually ported.  |
| U331 | [`3fb24b8963`](https://github.com/OpenRCT2/OpenRCT2/commit/3fb24b8963f388944986aafc034710b02c0a0efe) Fix references to LoadPalette in ScObjectManager.cpp (#27105) | P6 / adapt | 0 | T | Reload palette after plugin water object load using final Palette API; refresh owned GPU palette state for single and array loads.  |
| U332 | [`c25762467a`](https://github.com/OpenRCT2/OpenRCT2/commit/c25762467a5b44141a73ebc01307703531b4f1e1) Fix #27093: Pause and fast forward buttons are shown in multiplayer | P5 / port | 0 | T | Apply final editor/multiplayer pause-speed-chat visibility rules; preserve fork local speed/turbo behavior. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U333 | [`255928221c`](https://github.com/OpenRCT2/OpenRCT2/commit/255928221c7d4e72d1b8fda1ef7e7f53be4b3612) Merge pull request #27104 from Gymnasiast/fix/27093 | P0 / history | 0 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U334 | [`829dd93bd0`](https://github.com/OpenRCT2/OpenRCT2/commit/829dd93bd03ad79a4fd94803707f83dcc7831c4d) Fix #21320: RCT1 allows more cars per train on some rides than OpenRCT2 (#27106) | P4 / decision | 1 | C | Remove station-length safety margin for non-block modes, raising permissible train/car capacity beyond import fixes. [D02](upstream-port-plan-2026-09-13.md#d02) |
| U335 | [`e93fa5342a`](https://github.com/OpenRCT2/OpenRCT2/commit/e93fa5342a6c447fa0d32492281af9459ceaed24) Close #21400: draw staff patrol area selector on the water (#27067) | P6 / adapt | 1 | T | Draw staff patrol selector on water and ground; integrate with fork surface drawing/cache and patrol permission rules.  |
| U336 | [`3418b4f8bf`](https://github.com/OpenRCT2/OpenRCT2/commit/3418b4f8bf2a74394c1d52e01a603402ee2dacaa) Group themes window tab info into one struct (#27026) | P5 / adapt | 0 | T | Adopt theme colour conversion/tab metadata with final HUD theme migration. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U337 | [`b52dc2d933`](https://github.com/OpenRCT2/OpenRCT2/commit/b52dc2d9333463e26d3aba822eb9453014e28252) Rename INTENT_ACTION_UPDATE_NEWS_TICKER | P5 / decision | 1 | T | Split/reposition HUD, news and editor controls; migrate theme settings and 30-second fork weather preview with the new owners. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U338 | [`212c99ae59`](https://github.com/OpenRCT2/OpenRCT2/commit/212c99ae591925eb22763fc1ad022bb4204fd09b) Move NewsTicker to its own window and rename GameStatusBar | P5 / decision | 8 | T | Split/reposition HUD, news and editor controls; migrate theme settings and 30-second fork weather preview with the new owners. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U339 | [`345f61452e`](https://github.com/OpenRCT2/OpenRCT2/commit/345f61452e9c66a0eec584243933a3563b93e822) Move JSON colour conversion to its own function | P5 / adapt | 0 | T | Adopt theme colour conversion/tab metadata with final HUD theme migration. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U340 | [`be90806c80`](https://github.com/OpenRCT2/OpenRCT2/commit/be90806c808ab3d469d6edd5484472c11b00defd) Convert settings for the bottom toolbar to the new window types | P5 / decision | 0 | T | Split/reposition HUD, news and editor controls; migrate theme settings and 30-second fork weather preview with the new owners. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U341 | [`cd728ac481`](https://github.com/OpenRCT2/OpenRCT2/commit/cd728ac48196898a91ca03f4decf8872bc424ff6) Merge pull request #26947 from AaronVanGeffen/news-ticker | P0 / history | 8 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U342 | [`242b916767`](https://github.com/OpenRCT2/OpenRCT2/commit/242b9167679274b6670c80504850867f13dfc7cc) Fix typo in en-GB | P1 / port | 1 | T | Apply final documentation/changelog/string correction; distinguish upstream release history from features actually ported.  |
| U343 | [`b1175458bf`](https://github.com/OpenRCT2/OpenRCT2/commit/b1175458bfb9481a02e8ac526cdf52c864aa033d) Refactor CoordsRange.hpp/ScreenCoords.hpp members to camelCase (#27112) | P2 / adapt | 12 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U344 | [`fdd84f2612`](https://github.com/OpenRCT2/OpenRCT2/commit/fdd84f26125d49df189f24738ee10a5ca20d3e6e) Refactor ViewportInteractionItems into FlagHolder (#27111) | P2 / adapt | 5 | T | Apply mechanical intent under P2 contract across all fork-only consumers; inspect numeric flags and deleted legacy code before porting.  |
| U345 | [`fe4e93cf0e`](https://github.com/OpenRCT2/OpenRCT2/commit/fe4e93cf0e54736420742c2149185eb5cdd87f44) Split determining peep action and display | P5 / adapt | 5 | T | Split peep action description from display capitalization; preserve platform walking/waiting and transport status semantics.  |
| U346 | [`6e0d62b403`](https://github.com/OpenRCT2/OpenRCT2/commit/6e0d62b4039e7b04cff29afa486805ed141f753b) Fix #1514: Wrong capitalisation for descriptions in Guest List | P5 / adapt | 2 | T | Split peep action description from display capitalization; preserve platform walking/waiting and transport status semantics.  |
| U347 | [`b5ad1e4110`](https://github.com/OpenRCT2/OpenRCT2/commit/b5ad1e41108567b4411496abc1d53742c71792e3) Merge pull request #27061 from Gymnasiast/fix/1514 | P0 / history | 7 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U348 | [`6ab43acb35`](https://github.com/OpenRCT2/OpenRCT2/commit/6ab43acb35c436318cbe0932c42283ac6c43db89) Add missing PermissionType "drag_path_area" (#27084) | P7 / port | 1 | T | Expose drag_path_area permission in plugin declarations; verify mapping to existing server permission.  |
| U349 | [`1cc42279dc`](https://github.com/OpenRCT2/OpenRCT2/commit/1cc42279dcbd40802fc0f60d1674e6fa962a697f) Remove red for negative Gs (#27100) | P4 / decision | 1 | C | Remove negative-G red statistics/graph warning upstream; fork still penalizes harsh negative G and has directed-leg display. [D01](upstream-port-plan-2026-09-13.md#d01) |
| U350 | [`1e7fdd7ba6`](https://github.com/OpenRCT2/OpenRCT2/commit/1e7fdd7ba65378db65290eba994f882542111340) Refactor RideModes into FlagHolder (#27115) | P2 / adapt | 54 | C | Rename rating/RTD/mode symbols only; do not restore upstream BaseRatings, BonusMazeSize, operating modes or default coefficients over fork models. [D12](upstream-port-plan-2026-09-13.md#d12) |
| U351 | [`fdcda7be5d`](https://github.com/OpenRCT2/OpenRCT2/commit/fdcda7be5d5d1293092a5e8fc0fe075579b2f283) RCT1: create function to get number of additional zero cars | P7 / adapt | 0 | C | Use vehicle-type-specific extra zero cars in RCT1 park/design imports; retain cent/time conversion and platform reconstruction.  |
| U352 | [`8eb25bb02f`](https://github.com/OpenRCT2/OpenRCT2/commit/8eb25bb02f707d8199d988e7a7ea4243ea079363) Fix: Some rides in RCT1 saves have a wrong number of cars per train | P7 / adapt | 1 | C | Use vehicle-type-specific extra zero cars in RCT1 park/design imports; retain cent/time conversion and platform reconstruction.  |
| U353 | [`0dfc546c8b`](https://github.com/OpenRCT2/OpenRCT2/commit/0dfc546c8b82a646223a4312f611b15d235756e6) Fix #21576: RCT1 Wooden wild mouse track designs with 2-car trains are not imported correctly | P7 / adapt | 1 | C | Use vehicle-type-specific extra zero cars in RCT1 park/design imports; retain cent/time conversion and platform reconstruction.  |
| U354 | [`831ea32857`](https://github.com/OpenRCT2/OpenRCT2/commit/831ea328574d2c1394b0ae2a2a2cbf8ac94f63bb) Merge pull request #27109 from Gymnasiast/fix/wrong-number-of-cars-per-train-rct1 | P0 / history | 2 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U355 | [`b6235baa9a`](https://github.com/OpenRCT2/OpenRCT2/commit/b6235baa9ac5ffd359605b86100dc98b4c316b53) Merge Localisation/master into OpenRCT2/develop | P1 / port | 0 | T | Single-parent localisation update; use final target blobs after English ID reconciliation.  |
| U356 | [`612b3cb2aa`](https://github.com/OpenRCT2/OpenRCT2/commit/612b3cb2aae32865fbcce2729ae9b638c37b548b) Fix #27113: Chat button is shown in editor mode | P5 / port | 0 | T | Apply final editor/multiplayer pause-speed-chat visibility rules; preserve fork local speed/turbo behavior. [D07](upstream-port-plan-2026-09-13.md#d07) |
| U357 | [`d8345a4d53`](https://github.com/OpenRCT2/OpenRCT2/commit/d8345a4d533288967d689d6613c5719a2b637598) Add #19754 to changelog | P1 / port | 0 | T | Apply final documentation/changelog/string correction; distinguish upstream release history from features actually ported.  |
| U358 | [`a535c9f810`](https://github.com/OpenRCT2/OpenRCT2/commit/a535c9f810bcdb876523094e48253a1080b9f4ea) Merge pull request #27119 from Gymnasiast/fix/27113 | P0 / history | 0 | H | No remerge-resolution delta detected. Account for constituent commits before ancestry reconciliation.  |
| U359 | [`eb7b5feab9`](https://github.com/OpenRCT2/OpenRCT2/commit/eb7b5feab9cbf52cae49059aa28e5871bdf1e10a) Fix #17409: Game defaults to pounds in Canada (#27120) | P8 / adapt | 0 | T | Select Canadian dollars by default for Canadian locale; preserve user-selected currency and fork cent precision.  |
| U360 | [`75c12dbe7d`](https://github.com/OpenRCT2/OpenRCT2/commit/75c12dbe7d091856872da26abb01547cc3a4a0e0) Fix #27070: Gate polled input on window focus (#27078) | P5 / adapt | 1 | C | Fix outside-window mouse release and gate held/gamepad input on focus; clear accumulated scroll while retaining turbo input pumping.  |
| U361 | [`15d4b5e933`](https://github.com/OpenRCT2/OpenRCT2/commit/15d4b5e933555913d216f4548f673cd55cfb0579) Remove AddFuncs calls in ScWidget.hpp (#27044) | P7 / adapt | 1 | T | Replace AddFuncs registration with final prototype/class setup; retain fork QuickJS /WX count fixes and verify teardown/re-registration.  |

## Updating completion evidence

For each row, record the actual fork commit(s), decision disposition, focused verification and residual risks in the JSON evidence slots and reflect them here. For a mechanical group one fork commit may satisfy many rows, but every SHA must retain attribution. A history row is satisfied only after its children and any accepted alternatives/no-ops are accounted for. Do not change every status to complete merely because an ancestry merge makes the behind count zero.

## Merge-probe conflict inventory

These are predicted textual conflicts for a direct merge of the frozen heads, not outstanding index conflicts. Unlisted clean merges still need semantic review.

```text
data/language/en-GB.txt
src/openrct2-ui/UiContext.cpp
src/openrct2-ui/audio/AudioMixer.cpp
src/openrct2-ui/audio/AudioMixer.h
src/openrct2-ui/drawing/engines/DrawingEngineFactory.hpp
src/openrct2-ui/drawing/engines/HardwareDisplayDrawingEngine.cpp
src/openrct2-ui/drawing/engines/opengl/ApplyTransparencyShader.h
src/openrct2-ui/drawing/engines/opengl/CopyRectShader.h
src/openrct2-ui/drawing/engines/opengl/DrawLineShader.cpp
src/openrct2-ui/drawing/engines/opengl/DrawLineShader.h
src/openrct2-ui/drawing/engines/opengl/DrawRectShader.h
src/openrct2-ui/drawing/engines/opengl/OpenGLDrawingEngine.cpp
src/openrct2-ui/drawing/engines/opengl/OpenGLFramebuffer.cpp
src/openrct2-ui/drawing/engines/opengl/OpenGLFramebuffer.h
src/openrct2-ui/drawing/engines/opengl/OpenGLShaderProgram.cpp
src/openrct2-ui/drawing/engines/opengl/OpenGLShaderProgram.h
src/openrct2-ui/drawing/engines/opengl/SwapFramebuffer.cpp
src/openrct2-ui/drawing/engines/opengl/SwapFramebuffer.h
src/openrct2-ui/drawing/engines/opengl/TextureCache.cpp
src/openrct2-ui/drawing/engines/opengl/TextureCache.h
src/openrct2-ui/input/InputManager.h
src/openrct2-ui/ride/VehicleSounds.cpp
src/openrct2-ui/scripting/ScWidget.hpp
src/openrct2-ui/windows/GameBottomToolbar.cpp
src/openrct2-ui/windows/Guest.cpp
src/openrct2-ui/windows/Options.cpp
src/openrct2-ui/windows/Park.cpp
src/openrct2-ui/windows/Ride.cpp
src/openrct2-ui/windows/Staff.cpp
src/openrct2/Context.cpp
src/openrct2/Game.cpp
src/openrct2/OpenRCT2.h
src/openrct2/ReplayManager.cpp
src/openrct2/actions/footpath/FootpathLayoutPlaceAction.cpp
src/openrct2/actions/footpath/FootpathPlaceAction.cpp
src/openrct2/actions/park/ParkEntrancePlaceAction.cpp
src/openrct2/actions/park/ParkSetEntranceFeeAction.cpp
src/openrct2/actions/ride/MazePlaceTrackAction.cpp
src/openrct2/actions/ride/MazeSetTrackAction.cpp
src/openrct2/actions/ride/RideEntranceExitPlaceAction.cpp
src/openrct2/actions/scenery/BannerPlaceAction.cpp
src/openrct2/actions/scenery/BannerRemoveAction.cpp
src/openrct2/actions/scenery/BannerSetStyleAction.cpp
src/openrct2/actions/scenery/LargeSceneryPlaceAction.cpp
src/openrct2/actions/scenery/SmallSceneryPlaceAction.cpp
src/openrct2/actions/scenery/WallPlaceAction.cpp
src/openrct2/actions/scenery/WallRemoveAction.cpp
src/openrct2/actions/track/TrackPlaceAction.cpp
src/openrct2/audio/Audio.cpp
src/openrct2/audio/AudioMixer.h
src/openrct2/config/Config.cpp
src/openrct2/drawing/Drawing.cpp
src/openrct2/drawing/Drawing.h
src/openrct2/drawing/IDrawingEngine.h
src/openrct2/drawing/LightFX.cpp
src/openrct2/drawing/NewDrawing.cpp
src/openrct2/drawing/NewDrawing.h
src/openrct2/entity/EntityList.h
src/openrct2/entity/EntityRegistry.cpp
src/openrct2/entity/EntityRegistry.h
src/openrct2/entity/EntityTweener.cpp
src/openrct2/entity/EntityTweener.h
src/openrct2/entity/Guest.cpp
src/openrct2/entity/Guest.h
src/openrct2/entity/Peep.cpp
src/openrct2/entity/Peep.h
src/openrct2/entity/Staff.cpp
src/openrct2/libopenrct2.vcxproj
src/openrct2/localisation/StringIds.h
src/openrct2/management/Finance.cpp
src/openrct2/network/NetworkBase.cpp
src/openrct2/object/ObjectManager.cpp
src/openrct2/object/PathAdditionObject.cpp
src/openrct2/object/RideObject.cpp
src/openrct2/object/SmallSceneryObject.cpp
src/openrct2/object/WallObject.cpp
src/openrct2/paint/tile_element/Paint.Surface.cpp
src/openrct2/paint/track/gentle/Circus.cpp
src/openrct2/paint/track/gentle/CrookedHouse.cpp
src/openrct2/paint/track/gentle/FerrisWheel.cpp
src/openrct2/paint/track/gentle/HauntedHouse.cpp
src/openrct2/paint/track/gentle/MerryGoRound.cpp
src/openrct2/paint/track/gentle/SpaceRings.cpp
src/openrct2/paint/track/thrill/3dCinema.cpp
src/openrct2/paint/track/thrill/Enterprise.cpp
src/openrct2/paint/track/thrill/MagicCarpet.cpp
src/openrct2/paint/track/thrill/MotionSimulator.cpp
src/openrct2/paint/track/thrill/SwingingInverterShip.cpp
src/openrct2/paint/track/thrill/SwingingShip.cpp
src/openrct2/paint/track/thrill/TopSpin.cpp
src/openrct2/paint/track/thrill/Twist.cpp
src/openrct2/paint/vehicle/Vehicle.MiniGolf.cpp
src/openrct2/paint/vehicle/Vehicle.ReverserRollerCoaster.cpp
src/openrct2/paint/vehicle/Vehicle.SplashBoats.cpp
src/openrct2/paint/vehicle/VehiclePaint.cpp
src/openrct2/park/ParkFile.cpp
src/openrct2/peep/GuestPathfinding.cpp
src/openrct2/peep/GuestPathfinding.h
src/openrct2/platform/Crash.cpp
src/openrct2/rct1/S4Importer.cpp
src/openrct2/rct12/ScenarioPatcher.cpp
src/openrct2/rct2/S6Importer.cpp
src/openrct2/ride/CarEntry.h
src/openrct2/ride/Ride.cpp
src/openrct2/ride/Ride.h
src/openrct2/ride/RideConstruction.cpp
src/openrct2/ride/RideData.h
src/openrct2/ride/RideRatings.cpp
src/openrct2/ride/RideRatings.h
src/openrct2/ride/Station.cpp
src/openrct2/ride/Station.h
src/openrct2/ride/TrainManager.cpp
src/openrct2/ride/TrainManager.h
src/openrct2/ride/Vehicle.Sound.cpp
src/openrct2/ride/Vehicle.Station.cpp
src/openrct2/ride/Vehicle.TrackMotion.cpp
src/openrct2/ride/Vehicle.cpp
src/openrct2/ride/Vehicle.h
src/openrct2/ride/rtd/gentle/CrookedHouse.h
src/openrct2/ride/rtd/gentle/HauntedHouse.h
src/openrct2/ride/rtd/gentle/Maze.h
src/openrct2/scenes/title/TitleScene.cpp
src/openrct2/scripting/bindings/object/ScObject.hpp
src/openrct2/scripting/bindings/ride/ScRideStation.cpp
src/openrct2/scripting/bindings/world/ScTile.cpp
src/openrct2/scripting/bindings/world/ScTileElement.cpp
src/openrct2/world/ConstructionClearance.cpp
src/openrct2/world/Footpath.cpp
src/openrct2/world/Map.cpp
src/openrct2/world/Map.h
src/openrct2/world/Park.cpp
src/openrct2/world/Scenery.cpp
src/openrct2/world/Scenery.h
src/openrct2/world/TileInspector.cpp
src/openrct2/world/Wall.cpp
src/openrct2/world/Weather.cpp
src/openrct2/world/map_generator/TreePlacement.cpp
src/openrct2/world/tile_element/PathElement.h
test/tests/EntityImportTests.cpp
test/tests/PlayTests.cpp
```
