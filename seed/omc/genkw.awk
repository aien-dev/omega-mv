# genkw.awk -- prints the keyword lookup of seed/omc/omc.om (fn kw): one `if eq w <packed word>` per
# word of om4's KWTAB, same order and codes. The language has no hex numbers and no data tables, so the
# packed words are written as decimals. Text generator only: its output is pasted into omc.om, the source.
function pack(w,  v,m,i,c){ v=0; m=1; for(i=1;i<=length(w);i++){ c=index(ORD,substr(w,i,1)); v=v+(c-1)*m; m=m*256 } return v }
BEGIN{
  ORD=""
  for(i=0;i<256;i++) ORD=ORD sprintf("%c",i)
  n=split("bool:1 uchar:2 u8:2 ushort:3 u16:3 uint:4 u32:4 ulong:5 u64:5 byte:6 i8:6 short:7 i16:7 int:8 i32:8 long:9 i64:9 add:33 sub:34 mul:35 div:36 rem:37 and:38 or:39 xor:40 shl:41 shr:42 eq:43 ne:44 lt:45 le:46 gt:47 ge:48 neg:49 not:50 get:51 len:52 read:53 write:54 true:55 false:56 view:57 edit:58 fn:64 to:65 end:66 if:67 else:68 while:69 bound:70 for:71 ret:72 =:73 (:74 ):75 void:76 global:78 text:79 put:80 free:81 exit:82 note:83 new:84 struct:85 break:86 trap:87 own:77 f32:88 f64:89 bitcast:90 fsqrt:91 fabs:92 fmin:93 fmax:94 sizeof:95 offsetof:96 continue:97 random:135", a, " ")
  for(k=1;k<=n;k++){ split(a[k],p,":"); printf "if eq w %d\nret %d\nend\n", pack(p[1]), p[2] }
}
