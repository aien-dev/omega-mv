#!/bin/bash -eu
# DRAFT (never submitted). OSS-Fuzz / ClusterFuzzLite build: compile each om/tls fuzz target with the pinned
# omc (aarch64, run through qemu-aarch64-static), then one labelled host-tooling libFuzzer driver per target.
# Guide: https://google.github.io/oss-fuzz/getting-started/new-project-guide/
cd "$SRC/omega-mv"
F=om/tls/test/fuzz
# om/tls/build.sh execs "$1" as one argv. A space-joined qemu command is looked up as a
# single filename and fails with "not found" (PR address check, job 121881757564).
cat > "$WORK/omc-qemu" << EOF
#!/bin/sh
exec qemu-aarch64-static "$SRC/omega-mv/seed/out/omc" "\$@"
EOF
chmod +x "$WORK/omc-qemu"
for t in fz_flight fz_hs fz_x509 fz_pem fz_dns fz_http fz_https; do
  cat seed/net/http.om seed/net/https.om $F/common.om $F/$t.om > "$WORK/$t.prog.om"
  sh om/tls/build.sh "$WORK/omc-qemu" "$WORK/$t.prog.om" "$OUT/$t.om.bin"
  $CC $CFLAGS -c $F/oss-fuzz/omexec_fuzzer.c -o "$WORK/omexec.o"
  $CXX $CXXFLAGS "$WORK/omexec.o" $LIB_FUZZING_ENGINE -o "$OUT/$t"
  case $t in fz_flight|fz_hs|fz_x509) a=fzstore.pem;; fz_pem) a=chain.bin;; *) a=;; esac
  [ -n "$a" ] && cp $F/seeds/$a "$OUT/$a"
  printf '[libfuzzer]\nmax_len = 65536\n' > "$OUT/$t.options"
  [ -n "$a" ] && printf '%s\n' "$OUT/$a" > "$OUT/$t.arg"
  (cd $F/seeds/$t && zip -q "$OUT/${t}_seed_corpus.zip" *)
done
