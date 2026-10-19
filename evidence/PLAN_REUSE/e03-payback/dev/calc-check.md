# aien-calc receipts: net(H) closed form against the direct finite sum

Source of the numbers: 01-prediction.json (development partition). Units: nanoseconds. H = opportunities.


## S1_reverse (p=1.0, Delta=-15167, r=1.0, C=74128, m=0, H=39)
- geometric form at r = 1: refused: division by zero receipt calc:c2f85cd05f37305e47ba1e1d62414dd634b289bf4f14388872680b4086805bb7  (handled by the r = 1 case below)
- closed form (r=1): H*p*Delta - C - m*H: `39*1.0*-15167 - 74128 - 0*39` = **-665641** (calc:755649aed2aa846f5afb8b9fd65e0ff8ab5423beef2c17abf5f7d9ab5e72b666)
- direct sum (r=1): p*Delta*(sum of H ones) - C - m*H: `1.0*-15167*(1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1) - 74128 - 0*39` = **-665641** (calc:4609dba5d23858fb657951ff5b3a2cd618a15084454090a036187e243acf08e8)
- break-even opportunities C/(p*Delta - m): `74128/(1.0*-15167 - 0)` = **-4.88745302301** (calc:8743ba26a7dddb54cfcf57b0a6f689eae882625ae09249077644b726889d6879)

## S1_merge_P+ (p=1.0, Delta=42481, r=1.0, C=91840, m=0, H=39)
- geometric form at r = 1: refused: division by zero receipt calc:0207cd593cab4fd231e0d4648944d9ae8a245983f2c2ff0ebc6aec978a9d8e69  (handled by the r = 1 case below)
- closed form (r=1): H*p*Delta - C - m*H: `39*1.0*42481 - 91840 - 0*39` = **1564919** (calc:51e672032bd738c581467d5971d531b9bd50afee00c0c79cfdb7b5bd9d8ec21f)
- direct sum (r=1): p*Delta*(sum of H ones) - C - m*H: `1.0*42481*(1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1) - 91840 - 0*39` = **1564919** (calc:8c55fb43e9c24a4187b4c97d95d7b46cef58b4b6f8bf21cd593a4275d9f9b16b)
- break-even opportunities C/(p*Delta - m): `91840/(1.0*42481 - 0)` = **2.16190767637** (calc:039ef2d7040fe5703ed95c3b982214f4eb815f4de95ea659ca7e7bfdef002ad6)

## P0_no_payoff (p=0, C=70304, m=63200, H=39)
- closed form (p=0): -C - m*H: `0 - 70304 - 63200*39` = **-2535104** (calc:d0544db743e72eab7812fc294f18f82ee090398ddfcd1385247c728915218ff2)

## model check, r < 1 (p=1, Delta=1000000, r=9/10, C=5000000, m=0, H=39)
- closed form: Delta*(1-(9/10)^39)/(1-9/10) - C: `1000000*(1-(9/10)^39)/(1-9/10) - 5000000` = **4835767.96732** (calc:f46be94d4beb658208239e2aa9f00e419e38d464a9649fe4f45e6273d3e21efb)
- direct sum: Delta*((9/10)^0+...+(9/10)^38) - C: `1000000*((9/10)^0+(9/10)^1+(9/10)^2+(9/10)^3+(9/10)^4+(9/10)^5+(9/10)^6+(9/10)^7+(9/10)^8+(9/10)^9+(9/10)^10+(9/10)^11+(9/10)^12+(9/10)^13+(9/10)^14+(9/10)^15+(9/10)^16+(9/10)^17+(9/10)^18+(9/10)^19+(9/10)^20+(9/10)^21+(9/10)^22+(9/10)^23+(9/10)^24+(9/10)^25+(9/10)^26+(9/10)^27+(9/10)^28+(9/10)^29+(9/10)^30+(9/10)^31+(9/10)^32+(9/10)^33+(9/10)^34+(9/10)^35+(9/10)^36+(9/10)^37+(9/10)^38) - 5000000` = **4835767.96732** (calc:571732255afc7d36e5411527a9a0199fd87f567d47e7e7698bd0bb814cf6c836)
- sup over H of the geometric part: p*Delta/(1-r): `1000000/(1-9/10)` = **10000000** (calc:be2bed24d29d3548b8a8aec6452b5b27f6b9e6972a6368ba97f4df0ed1329670)
- no-finite-payoff condition C >= p*Delta/(1-r): C - p*Delta/(1-r) (>= 0 means never pays): `5000000 - 1000000/(1-9/10)` = **-5000000** (calc:b83749020495ca0a27edfbbc97a9bfb4130fb160d8485bcf5d866f9206f51aed)
- same with C = 12000000: C - p*Delta/(1-r): `12000000 - 1000000/(1-9/10)` = **2000000** (calc:4cb74744ef1c81a8ccb7607248310e721dd9cf5b9dfaab97c50df9ae8434d660)
- r = 0 case: p*Delta*0^0 - C with 0^0 = 1 (one opportunity): Delta - C: `1000000 - 5000000` = **-4000000** (calc:07935c2a5a9bb9949e1c772644d3dbce939505b2fa7b9180537213c2ae6abe1e)

Receipts: evidence/PLAN_REUSE/e03-payback/dev/calc-receipts/<hex>.json; verify with `aien-calc verify calc:<hex>`.
