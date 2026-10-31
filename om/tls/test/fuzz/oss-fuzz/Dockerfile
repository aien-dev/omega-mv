# DRAFT (never submitted). https://google.github.io/oss-fuzz/getting-started/new-project-guide/
FROM gcr.io/oss-fuzz-base/base-builder
RUN apt-get update && apt-get install -y qemu-user-static zip
RUN git clone --depth 1 --branch audit/tls-fuzz https://github.com/aien-dev/omega-mv.git omega-mv
WORKDIR omega-mv
COPY om/tls/test/fuzz/oss-fuzz/build.sh $SRC/
