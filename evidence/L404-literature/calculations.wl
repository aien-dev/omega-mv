(* Inputs evaluated through mcp__wolfram__WolframLanguageEvaluator.
   The report contains selected returned values. Decimal SI byte units.
   Each Module can be evaluated independently. *)

(* Routing and row-tile model. *)
Module[{rows={128,512,1024,2048},tiles={4,16,32,64},expertBytes=3*2048*512,bw=220*10^9,table},
 table=Flatten[Table[Module[{d=BinomialDistribution[c,1/32],tilesMean,distinct,traffic,util},
  tilesMean=Sum[SurvivalFunction[d,j*m],{j,0,Ceiling[c/m]-1}];
  distinct=256*(1-(31/32)^c);
  traffic=40*256*expertBytes*tilesMean/c;
  util=(c/32)/(m*tilesMean);
  <|"chunkTokens"->c,"tileRows"->m,"expectedDistinctExpertsPerLayer"->N[distinct,8],
    "meanTokensPerUsedExpert"->N[(8*c)/distinct,8],"expertWeightMBPerToken"->N[traffic/10^6,8],
    "expertWeightsOnlyRoofTokensPerSecond"->N[bw/traffic,8],"usefulRowFraction"->N[util,8]|>],
  {c,rows},{m,tiles}],1];
 <|"Assumptions"->"uniform independent routing across tokens; each chooses 8 distinct of 256 experts; FP8 raw weights only (no scales, padding, activations, trunk, KV, state); one weight reread per expert row tile; no reuse across tiles or chunks. Roof is conditional, not achieved speed.","Rows"->table|>]

(* Attention IO and arithmetic; hypothetical schedules, not measured DRAM traffic. *)
Module[{n=8192,hq=16,hkv=2,d=256,layers=10,b=2,kv,naive,saved,attentionFlops,reg},
 kv=layers*2*hkv*d*b*n;naive=kv*(hq/hkv);saved=naive-kv;
 attentionFlops=layers*4*hq*d*n*(n+1)/2;
 <|"KVBytesAt8k"->kv,"GQARatio"->hq/hkv,"EightSeparateQueryReadsBytes"->naive,
 "OncePerKVHeadBytes"->kv,"PotentialBytesSavedIfNoCacheReuse"->saved,
 "TimeDifferenceAt220GBpsMilliseconds"->N[1000*saved/(220*10^9),10],
 "CausalAttentionQKandPVFlopsAt8k"->attentionFlops,
 "At118p81DenseBF16TFLOPS_ArithmeticOnlySeconds"->N[attentionFlops/(11881*10^10),10],
 "OneFP32ScoreMatrixPerLayerGiB"->N[hq*n^2*4/2^30,10],
 "All10LayersScoreMatricesBytesWrittenAndReadOnce"->layers*2*hq*n^2*4,
 "Note"->"score matrix is hypothetical naive implementation, not a claim our code materializes it; no softmax/exp/normalization/projection/DeltaNet cost included. GQA benefit requires current code and cache traffic verification."|>]

(* Nontrivial exact rational recurrence check and wrong-order negative control. *)
Module[{s0={{1/3,2/5},{-2/7,3/11},{5/13,-1/17}},ks={{1/2,1/3},{-1/4,2/5},{1/7,-2/9},{3/10,1/6}},
 vs={{1/3,-1/5,2/7},{2/9,1/4,-1/3},{-2/11,3/7,1/5},{1/6,2/13,-3/8}},
 alpha={9/10,4/5,7/8,11/12},beta={1/3,2/5,1/4,3/7},aa,bb,seq,ff,gg,parallel,wrong},
 aa=Table[alpha[[i]]*(IdentityMatrix[2]-beta[[i]]*Outer[Times,ks[[i]],ks[[i]]]),{i,4}];
 bb=Table[beta[[i]]*Outer[Times,vs[[i]],ks[[i]]],{i,4}];
 seq=Fold[#1.aa[[#2]]+bb[[#2]]&,s0,Range[4]];
 ff=Fold[Dot,IdentityMatrix[2],aa];
 gg=Sum[bb[[i]].Fold[Dot,IdentityMatrix[2],Drop[aa,i]],{i,4}];
 parallel=s0.ff+gg;wrong=s0.Fold[Dot,IdentityMatrix[2],Reverse[aa]]+gg;
 <|"ExactRationalRecurrentEqualsExpandedChunk"->(seq===parallel),"AllEntryDifferences"->(seq-parallel),
 "WrongOrderMutantRejected"->(wrong=!=seq),
 "Scope"->"checks the expanded gated-delta recurrence in exact rational arithmetic; not GPU kernel validation and not proof of floating-point bit equality for WY/UT reordering"|>]

(* Checkpoint inventory and unrounded prefill receipt. *)
Module[{gdn={4096,64,65536,64,131072,131072,2048,1024,256,1024,1048576,128,128,128,4096,4096,16777216,8388608,8388608,1048576,1048576,1048576},
 att={4096,1048576,128,128,128,4096,4096,512,128,1024,512,2048,128,1048576,1048576,1048576,1048576,8388608,16777216,1048576},
 expert={128,128,128,1048576,1048576,1048576},head=1017118720,bytes=4120537726976,ms=41631609445/10^6,trunk,active,decode,lower,f},
 trunk=30*Total[gdn]+10*Total[att];active=40*8*Total[expert];decode=trunk+active+head;
 lower=bytes/(220*10^9);f=lower/(ms/1000);
 <|"DecodeWeightInventory"-><|"TrunkIncludesSharedExpertsBytes"->trunk,
 "RoutedEightExpertsPerLayerBytes"->active,"BF16HeadBytes"->head,"SumBytes"->decode,
 "StreamMillisecondsAt207GBps"->N[1000*decode/(207*10^9),12],"At220GBps"->N[1000*decode/(220*10^9),12],
 "At237GBps"->N[1000*decode/(237*10^9),12],
 "Scope"->"40 decoder layers only; excludes MTP, embedding lookup, activations, KV and recurrent state; includes listed quantization scales; assumes all decoder layers share sampled type layout; packed runtime format may differ"|>,
 "Prefill8192Receipt"-><|"MeasuredSeconds"->N[ms/1000,12],"TokensPerSecond"->N[8192/(ms/1000),12],
 "SoftwareAccountedFP8BytesPerToken"->N[bytes/8192,12],"ConditionalWeightOnlyRoofAt220GBps"->N[8192/lower,12],
 "ConditionalWeightTimeSeconds"->N[lower,12],"UnattributedResidualSeconds"->N[ms/1000-lower,12],
 "HalfTrafficSpeedup_AdditiveModelOnly"->N[1/(1-f+f/2),12],
 "QuarterTrafficSpeedup_AdditiveModelOnly"->N[1/(1-f+f/4),12],
 "ZeroTrafficSpeedup_AdditiveModelOnly"->N[1/(1-f),12]|>|>]
