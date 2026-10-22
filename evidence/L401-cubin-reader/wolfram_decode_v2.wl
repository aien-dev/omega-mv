(* L401 cubin reader: independent decoder used to derive every expected value in
   tests/compiler/slices/cubin_elf.osc. Spec v2 (2026-10-22). The Wolfram evaluator cannot read local
   files, so image bytes travel inline as base64 (ByteArray["..."]) and each image's SHA-256 prefix is
   compared with the file's (see wolfram_v2.json). Wolfram checks the reader; it is never in the program path.

   Layout read: ELF64 header (64 bytes), section header table at e_shoff (64-byte entries),
   section name string table at index e_shstrndx. e_ident[7] is EI_OSABI (not checked; 0x41 from
   CUDA 13 nvcc, 0x33 before), e_ident[8] is EI_ABIVERSION (7: SM in e_flags bits 0..7; 8: bits 8..15). *)

le[y_, o_, n_] := FromDigits[Reverse[y[[o + 1 ;; o + n]]], 256];
fits[n_, o_, z_] := o <= n && z <= n - o;
nm[y_, so_, ss_, off_] := If[off >= ss, "", FromCharacterCode[TakeWhile[y[[so + off + 1 ;; so + ss]], # != 0 &]]];
refused = 2^64 - 1;

hdr[y_] := Module[{n = Length[y]},
  n >= 64 && y[[1 ;; 4]] === {127, 69, 76, 70} && y[[5]] == 2 && y[[6]] == 1 && y[[7]] == 1 &&
   MemberQ[{7, 8}, y[[9]]] && le[y, 16, 2] == 2 && le[y, 18, 2] == 190 && le[y, 20, 4] == 1 &&
   le[y, 52, 2] == 64 && le[y, 58, 2] == 64 &&
   Module[{shn = le[y, 60, 2], shoff = le[y, 40, 8], str = le[y, 62, 2]},
    shn > 0 && str < shn && fits[n, shoff, 64 shn]]];

arch[y_] := If[! hdr[y], refused, BitAnd[BitShiftRight[le[y, 48, 4], If[y[[9]] == 8, 8, 0]], 255]];

layout[y_] := Module[{n = Length[y], shoff, shn, str, tend, so, ss, ok = True},
  If[! hdr[y], Return[False]];
  shoff = le[y, 40, 8]; shn = le[y, 60, 2]; str = le[y, 62, 2]; tend = shoff + 64 shn;
  so = le[y, shoff + 64 str + 24, 8]; ss = le[y, shoff + 64 str + 32, 8];
  If[le[y, shoff + 64 str + 4, 4] != 3 || ss == 0 || ! fits[n, so, ss], Return[False]];
  Do[Module[{typ = le[y, shoff + 64 i + 4, 4], off = le[y, shoff + 64 i + 24, 8],
      sz = le[y, shoff + 64 i + 32, 8], no = le[y, shoff + 64 i, 4]},
    If[no >= ss, ok = False];
    If[typ != 8 && ! fits[n, off, sz], ok = False];
    If[typ != 8 && sz > 0 && off < 64, ok = False];
    If[typ != 8 && sz > 0 && off < tend && off + sz > shoff, ok = False]], {i, 0, shn - 1}];
  ok];

textcount[y_] := Module[{shoff, shn, str, so, ss, c = 0},
  If[! layout[y], Return[refused]];
  shoff = le[y, 40, 8]; shn = le[y, 60, 2]; str = le[y, 62, 2];
  so = le[y, shoff + 64 str + 24, 8]; ss = le[y, shoff + 64 str + 32, 8];
  Do[Module[{s2 = nm[y, so, ss, le[y, shoff + 64 i, 4]], typ = le[y, shoff + 64 i + 4, 4]},
    If[StringStartsQ[s2, ".text."] && typ == 1, c++]], {i, 1, shn - 1}];
  c];

decode[y_] := {StringTake[Hash[ByteArray[y], "SHA256", "HexString"], 16], Length[y],
   Boole[hdr[y]], arch[y], Boole[layout[y]], textcount[y]};

(* usage: decode[Normal[ByteArray["<base64 of the image>"]]]
   The real sm_121 image was rebuilt from the three regions this decoder reads (header 0+64,
   .shstrtab 64+845, section table 13168+2688; zeros elsewhere, length 16136), each region hash-checked. *)
