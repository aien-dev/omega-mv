# ClusterFuzzLite build image for the om/tls fuzz targets (black-box: driver coverage only).
FROM gcr.io/oss-fuzz-base/base-builder
RUN apt-get update && apt-get install -y qemu-user-static zip
COPY . $SRC/omega-mv
WORKDIR $SRC/omega-mv
COPY .clusterfuzzlite/build.sh $SRC/
