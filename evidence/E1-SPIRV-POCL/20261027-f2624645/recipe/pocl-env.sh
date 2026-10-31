T=$TOOLS
export SPIRV_VAL=$T/root/usr/bin/spirv-val
export POCL_LIBRARY=$T/pocl/lib/libpocl.so.2.16.0
export POCL_DEVICES=cpu
export LD_LIBRARY_PATH=$T/root/usr/lib/aarch64-linux-gnu
export OPENCL_CFLAGS="-I$T/root/usr/include"
export OPENCL_LIBS="/usr/lib/aarch64-linux-gnu/libOpenCL.so.1"
