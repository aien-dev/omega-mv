# Host-only source accounting of the pinned 48-layer OWN30B decode graph.
function buf(name,size,writer,reader,life,   padded,recompute) {
 padded=int((size+255)/256)*256
 recompute=(name=="x"||name=="attn"||name=="hidden"||name=="token")?"FORBIDDEN_stateful_writer":"UNKNOWN"
 printf "%s\t%.0f\t%.0f\t%.0f\t%s\t%s\t%s\tANALYTICALLY_ESTIMATED\tUNKNOWN\tUNKNOWN\tmodel,input,state,layout,contract\t%s\tunified_uncached\n",name,offset,size,padded,writer,reader,life,recompute > (dir "/intermediates.tsv")
 offset+=padded
}
function op(id,kind,rd,wr,notes) {
 printf "%s\t%s\t%.0f\t%.0f\tANALYTICALLY_ESTIMATED\tUNKNOWN\tUNKNOWN\tUNKNOWN\t%s\n",id,kind,rd,wr,notes > (dir "/operations.tsv")
 printf "%s\t%s\t%d\tUNKNOWN\n",id,(opcount?previous:"host_upload_boundary"),opcount > (dir "/critical-path.tsv")
 previous=id;opcount++
}
BEGIN {
 H=2048;I=768;TK=8;L=48;V=151936;S=2048;A=int((V+S-1)/S)
 print "id\toffset_B\tsize_B\tallocated_B\twriters\tconsumers\tlifetime\tsize_provenance\ttransfer_cost_ns\tlookup_validation_ns\tinvalidation\trecompute_operations\tstorage" > (dir "/intermediates.tsv")
 buf("x",H*4,"host,res_add,combine_norm","norm,res_add,router,combine_norm,read(x)","whole-token+readback")
 buf("xq",H*4,"norm,combine_norm","qkv","producer-to-qkv")
 buf("qkv",5120*4,"qkv","attn","qkv-to-attn")
 buf("attn",4096*4,"attn","o_proj","attn-to-o_proj")
 buf("o",H*4,"o_proj","res_add","o_proj-to-res_add")
 buf("gu",TK*2*I*4,"gate_up","swiglu","gate_up-to-swiglu")
 buf("act",TK*I*4,"swiglu","down","swiglu-to-down")
 buf("dn",TK*H*4,"down","combine_norm","down-to-combine_norm")
 buf("hidden",H*4,"combine_norm(last)","lm_head,read(hidden)","last-layer-to-readback")
 buf("logits",A*S*4,"lm_head,padding-init","argmax0,read(logits)","lm_head-to-readback")
 buf("am_v",S*4,"argmax0,padding-init","argmax1","argmax0-to-argmax1")
 buf("am_i",S*4,"argmax0","argmax1","argmax0-to-argmax1")
 buf("token",16,"host-poison,argmax1","host-read","whole-token")
 buf("norms",L*H*4,"host-set-layer","norm,combine_norm","graph-lifetime")
 buf("fnorm",H*4,"host-set-final","combine_norm(last)","graph-lifetime")
 buf("tabs",L*32,"host-set-layer","qkv,o_proj","graph-lifetime")
 buf("pf_x",4096*4,"host-zero","prefetch(optional)","graph-lifetime")
 buf("pf_y",H*4,"prefetch(optional)","none","graph-lifetime")
 print "id\tkind\tlogical_min_read_B\tlogical_write_B\tbytes_provenance\tDRAM_B\tregister_spills_B\ttime_ns\tnotes" > (dir "/operations.tsv")
 print "id\tpredecessor\tlaunch_index\ttime_ns" > (dir "/critical-path.tsv")
 print "logical_value\tallocation\tfirst_writer_index\tlast_consumer_index\tnotes" > (dir "/lifetimes.tsv")
 print "x\tx\t-1\t436\tupdated_in_place;optional_host_readback;never_alias" > (dir "/lifetimes.tsv")
 op("initial_norm","norm",H*8,H*4,"x+norms;f32-no-reassociation")
 for(l=0;l<L;l++) {
  p=sprintf("L%02d_",l)
  split("xq qkv attn o gu act dn",values," ")
  split("0 1 2 3 6 7 8",starts," ")
  for(v=1;v<=7;v++)printf "%s%s\t%s\t%d\t%d\tlogical_version;physical_arena_persists\n",p,values[v],values[v],9*l+starts[v],9*l+starts[v]+1 > (dir "/lifetimes.tsv")
  op(p "qkv","qkv",H*4,5120*4,"weight+scale-reads-UNKNOWN;already-fused")
  op(p "attn","attn",5120*4,4096*4,"KV-payload-separate;split-reloads-UNKNOWN")
  op(p "o_proj","o_proj",4096*4,H*4,"weight+scale-reads-UNKNOWN")
  op(p "res_add","res_add",H*8,H*4,"x+=o;required-visibility-kept")
  op(p "router","router",H*4,0,"router-owned-outputs+weights-UNKNOWN;zero-is-not-total")
  op(p "gate_up","gate_up",H*4,TK*2*I*4,"router-xout+weights-UNKNOWN;already-fused")
  op(p "swiglu","swiglu",TK*2*I*4,TK*I*4,"one-read-per-element-logical-lower-account")
  op(p "down","down",TK*I*4,TK*H*4,"weights+scales-UNKNOWN")
  op(p "combine_norm","combine_norm",H*4+TK*H*4+TK*4+H*4,H*8,"already-fused;x+dn+wts+norm;reduction-rereads-UNKNOWN")
 }
 op("lm_head","lm_head",H*4,V*4,"resident-bf16-weight-payload=" V*H*2 ";actual-reads-UNKNOWN")
 op("argmax0","argmax",A*S*4,A*8,"padding-included;lowest-index-tie")
 op("argmax1","argmax",S*8,8,"token-index+value")
 print "hidden\thidden\t432\t436\toptional_host_readback;never_alias" > (dir "/lifetimes.tsv")
 print "logits\tlogits\t433\t436\toptional_host_readback;padding_initialized_at_start" > (dir "/lifetimes.tsv")
 print "am_v\tam_v\t434\t435\targmax0-to-argmax1" > (dir "/lifetimes.tsv")
 print "am_i\tam_i\t434\t435\targmax0-to-argmax1" > (dir "/lifetimes.tsv")
 print "token\ttoken\t435\t436\thost_readback" > (dir "/lifetimes.tsv")
 print "oev version=1" > (dir "/decode.oev")
 print "machine id=spark soc=GB10 cores=20 mem_bytes=137438953472 prov=specified src=583-schema-datasheet" > (dir "/decode.oev")
 print "build repo=omega-mv sha=" sha > (dir "/decode.oev")
 print "resource id=gpu kind=compute slots=1 prov=specified src=ordered-command-list" > (dir "/decode.oev")
 print "resource id=unified_mem kind=capacity cap=137438953472 prov=specified src=583-schema-datasheet" > (dir "/decode.oev")
 print "# All durations UNKNOWN. No calibration or traffic-to-DRAM conversion. Macro layers avoid OEV1 max256 operations." > (dir "/decode.oev")
 print "# Incomplete allocation coverage: weights/router/KV/session allocations UNKNOWN, not zero. No ranking permitted." > (dir "/decode.oev")
 print "op id=initial_norm kind=compute engine=gpu prov=unknown src=pinned-decode-source" > (dir "/decode.oev")
 prev="initial_norm"
 for(l=0;l<L;l++){
  id=sprintf("layer%02d",l)
  print "op id=" id " kind=compute engine=gpu prov=unknown src=operations.tsv:9-launches" > (dir "/decode.oev")
  print "dep " prev " " id > (dir "/decode.oev");prev=id
 }
 split("lm_head argmax0 argmax1",last," ")
 for(i=1;i<=3;i++){
  print "op id=" last[i] " kind=compute engine=gpu prov=unknown src=pinned-decode-source" > (dir "/decode.oev")
  print "dep " prev " " last[i] > (dir "/decode.oev");prev=last[i]
 }
 printf "buffer id=decode_arena bytes=%.0f cap=unified_mem\n",offset > (dir "/decode.oev")
 print "energy instrument=none" > (dir "/decode.oev")
 printf "arena_B=%.0f regions=18 layers=48 batch=1 launches=%d prefetch=off\n",offset,9*L+4 > (dir "/summary.txt")
 print "allocation=ANALYTICALLY_ESTIMATED unified_memory_count=1 onchip_residency=UNKNOWN" > (dir "/summary.txt")
 print "DRAM_bytes=UNKNOWN shared_bytes=UNKNOWN spills=UNKNOWN timings=NOT_RUN energy=UNMEASURED" > (dir "/summary.txt")
 print "stage\tm\tcontext_before\tqkv_logical_B\tout_logical_B\tKV_new_payload_B\twhole_path_status" > (dir "/prefill.tsv")
 printf "short\t128\t0\t%d\t%d\t%d\tNOT_RUN\n",128*5120*4,128*4096*4,128*4*256*2 > (dir "/prefill.tsv")
 printf "long\t2048\t32768\t%d\t%d\t%d\tNOT_RUN\n",2048*5120*4,2048*4096*4,2048*4*256*2 > (dir "/prefill.tsv")
}
