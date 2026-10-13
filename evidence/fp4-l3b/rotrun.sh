cd ~/workspace/omega-own30b-fp4
for r in 16 256; do
 OMP_NUM_THREADS=8 build/fp4_convert_lab ~/models/qwen3-30b-a3b-instruct-2507-fp8 ~/l3b/store_r$r.afp4 48 --search -1,1 --rot $r,$r > ~/l3b/conv_r$r.log 2>&1
 build/fp4_study_lab ~/models/qwen3-30b-a3b-instruct-2507-fp8 ~/l3b/store_r$r.afp4 ~/l3b/golden.tsv --threads 12 --cache ~/l3b/cache --noprobe --rot $r,$r --out ~/l3b/runs/r$r.json > ~/l3b/runs/r$r.txt 2> ~/l3b/runs/r$r.err
 rm -f ~/l3b/store_r$r.afp4
done
