# DYNAMO miner

This repository contains reference implementations of DYNAMO miners on both GPU and CPU.

## Linux

### Build dependencies

#### CMake

Requires CMake version >= 3.12.

In order to install CMake 3.21.3 on Linux you can use following command:

```sh
wget https://github.com/Kitware/CMake/releases/download/v3.21.3/cmake-3.21.3-linux-x86_64.sh -O /tmp/cmake-install.sh && \
  chmod +x /tmp/cmake-install.sh && \
  /tmp/cmake-install.sh --skip-license --prefix=/usr
```

#### Clang

Clang is a preferred over GCC. Support of latest C++20 features are expected.

In order to install clang 13 on Ubuntu you can use following command:

```sh
apt-get update && apt-get install -qy lsb-release wget software-properties-common

add-apt-repository ppa:ubuntu-toolchain-r/test
wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
echo "deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-13 main" >> /etc/apt/sources.list.d/llvm-toolchain.list && \
    echo "deb-src http://apt.llvm.org/bionic/ llvm-toolchain-bionic-13 main" >> /etc/apt/sources.list.d/llvm-toolchain.list

apt-get update && apt-get install -qy clang-13 lldb-13 lld-13 libc++-13-dev libc++abi-13-dev

update-alternatives --install /usr/local/bin/clang clang $(which clang-13) 10
update-alternatives --install /usr/local/bin/clang++ clang++ $(which clang++-13) 10
update-alternatives --install /usr/local/bin/lld lld $(which lld-13) 10
```

### Build from source

```sh
git clone -b pool-miner https://github.com/crackcomm/dyn_miner.git
cd dyn_miner
mkdir build
cd build
cmake .. -DGPU_MINER=ON 
make -j`nproc`
```

In order to generate binary for current machine enable `NATIVE_BUILD`.

```sh
cmake .. -DGPU_MINER=ON -DNATIVE_BUILD=ON
```

In order to specify C++ compiler use `CMAKE_CXX_COMPILER`.

```sh
cmake .. -DGPU_MINER=ON -DCMAKE_CXX_COMPILER="$(which clang++)"
```

## Windows

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
    
