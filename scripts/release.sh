#!/usr/bin/env bash

mkdir -p build
cd build

cmake .. -DGPU_MINER=ON -DCMAKE_BUILD_TYPE=Release
make -j`nproc`

REL_DIR=release/dyn_miner

mkdir -p $REL_DIR
cp dyn_miner/dyn_miner $REL_DIR
cp dyn_miner/dyn_miner.cl $REL_DIR

tar -zcvf release/dyn_miner.tar.gz $REL_DIR
echo "Release created in `pwd`/release/dyn_miner.tar.gz"

