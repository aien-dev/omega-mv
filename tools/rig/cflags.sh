# source me: sets RIGCF to the include flags used for host-side rig builds on the Spark
P=${PHYSICS:-$HOME/workspace/physics-tvg-6d7cf0d4}; T=$P/third_party/nvidia-open-580.173.02
RIGCF="-std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -Isrc -Ithird_party/wsl2-dxgkrnl -I$P/m16 -I$P/nvrm -I$T/src/common/sdk/nvidia/inc -I$T/kernel-open/common/inc -I$T/kernel-open/nvidia-uvm -I$T/src/nvidia/arch/nvalloc/unix/include"
