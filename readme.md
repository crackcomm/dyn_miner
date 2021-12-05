# DYNAMO miner

This repository contains reference implementations of DYNAMO miners on both GPU and CPU.

## Build

### Linux

```sh
git clone -b pool-miner https://github.com/crackcomm/dyn_miner.git
cd dyn_miner
mkdir build
cd build
cmake .. -DGPU_MINER=ON
make -j`nproc`
```

### Windows

This repo contains the Windows project files built in Visual Studio 2019.

To build, clone the repo and open the solution in Visual Studio.

The miner will display any OpenCL compatible platforms and will display all devices for any platform found.

It should be possible to build the miner using CMake.

```sh
cd dyn_miner
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build . --config Release
```

## Usage

Miner parameters are as follows:

```sh
./dyn_miner/dyn_miner pool.xyz 8333 username password CPU `nproc` 0
```

* RPC Host
* RPC Port
* RPC Username
* RPC Password
* CPU or GPU
  * If CPU, the next parameter is the number of threads to create (should be less than number of cores on your system)
  * If GPU, the next parameter is the number of compute units - values between 1000 and 2000 seem to work well for modern cards
* Number of threads or compute units, as noted above
* The platform ID to use for GPU mining.  Ignored for CPU.
    
