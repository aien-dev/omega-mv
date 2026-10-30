(* L524 independent oracle. Run in WolframLanguageEvaluator; no filesystem access. *)
pack[s_] := FromDigits[Reverse[ToCharacterCode[StringTake[s, UpTo[8]], "ASCII"]], 256];
samples = {0, 1, 10, 11, 255, 4294967295};
calc = <|"keywordPacking" -> AssociationMap[pack, {"fn", "trap", "break"}],
  "trapFixture" -> Table[<|"input" -> v, "trap" -> If[v > 10, 3, 0],
    "return" -> If[v > 10, Null, v]|>, {v, samples}], "u32max" -> 2^32 - 1,
  "sourceLines" -> 4551, "functions" -> 185, "elfBytes" -> 116560,
  "headerBytes" -> 176, "instructionWords" -> (116560-176)/4,
  "fixtureRunCount" -> 17*6+1+2, "fixtureChecks" -> 1+17*3+2*2+11|>;
payloads = <|
 "trap03" -> "T1NDMUlSAAEBAAFmAQQEAAQEAQQDAAAAAAAEAAAABAAAAAIAAAAGAAAAAQAAAAcAAAABAQAKAAAAAAAAAAEDAAAAAAAAAAAABQUCAAAAAQAOAgABAAIADAMPAwAPAAAAAAAA",
 "void" -> "T1NDMUlSAAEBAAFmAAAAAAEAAAAAAAIAAAACAAAADAMP//8AAAAA"|>;
decode[s_String] := Module[{x=Normal[ByteArray[s]], le, pos=11, namelen, name, np, rt, nr, nb, tables, ni, ops={}, op, rec},
 le[o_, n_] := FromDigits[Reverse[x[[o+1;;o+n]]],256];
 namelen=le[10,1]; name=FromCharacterCode[x[[12;;11+namelen]]];
 pos=11+namelen; np=le[pos,1]; rt=le[pos+1,1]; nr=le[pos+2,2]; pos=pos+4+nr;
 nb=le[pos,2]; pos=pos+2;
 tables=Table[{le[pos+8*k,4],le[pos+8*k+4,4]},{k,0,nb-1}];
 pos=pos+8*nb; ni=le[pos,4]; pos=pos+4;
 Do[op=le[pos,1]; pos++;
  rec=Switch[op,
   1, With[{r=le[pos,2],v=le[pos+2,8]},pos+=10;{"const",r,v}],
   5, With[{c=le[pos,1],d=le[pos+1,2],a=le[pos+3,2],b=le[pos+5,2]},pos+=7;{"cmp",c,d,a,b}],
   14, With[{c=le[pos,2],a=le[pos+2,2],b=le[pos+4,2]},pos+=6;{"cbr",c,a,b}],
   12, With[{c=le[pos,1]},pos++;{"trap",c}],
   15, With[{r=le[pos,2]},pos+=2;{"ret",r}],
   _, {"unexpected",op}];
  AppendTo[ops,rec],{ni}];
 <|"length"->Length[x],"sha256"->Hash[ByteArray[x],"SHA256","HexString"],
   "name"->name,"params"->np,"returnType"->rt,"registers"->nr,
   "blocks"->tables,"instructions"->ops,"trailingBytes"->Length[x]-pos,
   "zeroContractCounts" -> (Length[x]-pos==4 && le[pos,2]==0 && le[pos+2,2]==0)|>];
<|"calculations"->calc,"independentDecode"->Map[decode,payloads]|> // InputForm
