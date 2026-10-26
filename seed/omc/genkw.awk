# genkw.awk -- prints the keyword lookup of seed/omc/omc.om (fn kw): one `if eq w <packed word>` per
# word of om4's KWTAB, same order and codes. The language has no hex numbers and no data tables, so the
# packed words are written as decimals. The list covers every id, including the host words 100-177 and flock 197;
# pack uses decimal string arithmetic so 64-bit packs print exactly. Text generator only: its output
# is pasted into omc.om, the source.
function muladd(s, m, a,  r,carry,i,d,t){
  r=""; carry=a
  for(i=length(s);i>=1;i--){ d=substr(s,i,1)+0; t=d*m+carry; r=(t%10) r; carry=int(t/10) }
  while(carry>0){ r=(carry%10) r; carry=int(carry/10) }
  return r
}
function pack(w,  s,i,c){ s="0"; for(i=length(w);i>=1;i--){ c=index(ORD,substr(w,i,1))+31; s=muladd(s,256,c) } return s }
BEGIN{
  ORD=""
  for(i=32;i<127;i++) ORD=ORD sprintf("%c",i)
  n=split("bool:1 uchar:2 u8:2 ushort:3 u16:3 uint:4 u32:4 ulong:5 u64:5 byte:6 i8:6 short:7 i16:7 int:8 i32:8 long:9 i64:9 add:33 sub:34 mul:35 div:36 rem:37 and:38 or:39 xor:40 shl:41 shr:42 eq:43 ne:44 lt:45 le:46 gt:47 ge:48 neg:49 not:50 get:51 len:52 read:53 write:54 true:55 false:56 view:57 edit:58 fn:64 to:65 end:66 if:67 else:68 while:69 bound:70 for:71 ret:72 =:73 (:74 ):75 void:76 global:78 text:79 put:80 free:81 exit:82 note:83 new:84 struct:85 break:86 trap:87 own:77 f32:88 f64:89 bitcast:90 fsqrt:91 fabs:92 fmin:93 fmax:94 sizeof:95 offsetof:96 continue:97 fclose:100 fsync:101 fchmod:102 fseek:103 socknew:104 listen:105 accept:106 fork:107 dupfd:108 monons:109 fopen:110 unlink:111 makedir:112 getdnts:113 connect:114 bind:115 waitpid:116 fstat:117 recv:118 send:119 sleepns:120 argcnt:121 argread:122 waitio:123 execprog:124 renames:125 rmdir:126 killpid:127 pipefds:128 getpid:129 realns:130 sockname:131 cwd:132 readlnk:133 sockopt:134 random:135 envcnt:136 envread:137 chdir:138 ftrunc:139 stat:140 link:141 symlnk:142 chmod:143 faccess:144 getuid:145 geteuid:146 getgid:147 getegid:148 lstat:149 truncate:150 chown:151 const:152 fchdir:153 getppid:154 umask:155 fdsync:156 memfd:157 setsid:158 getpgid:159 setpgid:160 pread:161 pwrite:162 getsid:163 syncfs:164 shutdown:165 devctl:166 mapnew:167 mapdrop:168 mapread:169 mapwrite:170 devcall:171 uvmctl:172 mapaddr:173 mapget:174 mapset:175 maphold:176 mapbind:177 flock:197", a, " ")
  for(k=1;k<=n;k++){ split(a[k],p,":"); printf "if eq w %s\nret %d\nend\n", pack(p[1]), p[2] }
  n=split("peercred:220 pollfds:221 sendmsg:222", a, " ")
  for(k=1;k<=n;k++){ split(a[k],p,":"); printf "if eq w %s\nret %d\nend\n", pack(p[1]), p[2] }
}
