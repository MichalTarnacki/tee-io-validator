# TEE-IO Device Validation Utility Build

## Setup build environment

```
# For Ubuntu
sudo apt install build-essential

# For CentOS 8
sudo dnf groupinstall 'Development Tools'
sudo dnf install dnf-plugins-core
sudo dnf config-manager --set-enabled powertools
sudo dnf install glibc-devel glibc-static

# For CentOS 9
sudo dnf groupinstall 'Development Tools'
sudo dnf install dnf-plugins-core
sudo dnf config-manager --set-enabled crb
sudo dnf install glibc-devel glibc-static

```

## Build binaries

```
git clone --single-branch -b main https://github.com/intel/tee-io-validator.git
cd tee-io-validator
git submodule update --init --recursive
cd teeio-validator
mkdir build
cd build
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Debug -DCRYPTO=mbedtls ..
make -j
```
Below binaries are generated in `build/bin` directory.
- teeio_validator
- lside
- setide

### Example CMake commands

Here provides more CMake commands to replace the command in [Build binaries](#build-binaries)

```
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Debug -DCRYPTO=mbedtls ..
```

```
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Release -DCRYPTO=mbedtls ..
```

```
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Debug -DCRYPTO=openssl ..
```

```
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Release -DCRYPTO=openssl ..
```

### SPDM 1.4 PQC requester

The ML-DSA-87 and ML-KEM-1024 requester profile requires the OpenSSL backend.
Configure the build with:

```
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Debug -DCRYPTO=openssl ..
```

Use `spdm_version=1.4` in the `[Main]` section to pin negotiation to SPDM 1.4,
or leave the default `spdm_version=auto` to advertise all versions supported by
the bundled libspdm.

### Unsupported critical X.509 extensions

Ignoring unsupported critical X.509 extensions is independent of the SPDM
version and requester algorithms. If the tested device certificate contains
such an extension, append `-DX509_IGNORE_CRITICAL=ON` to the CMake command:

```
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Debug -DCRYPTO=openssl -DX509_IGNORE_CRITICAL=ON ..
```

### DiceTcbInfo extension support

To support DiceTcbInfo extension, please use the following CMake command to replace the command in [Build binaries](#build-binaries)

```
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Debug -DCRYPTO=mbedtls -DX509_IGNORE_CRITICAL=ON ..
```

```
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Release -DCRYPTO=mbedtls -DX509_IGNORE_CRITICAL=ON ..
```

```
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Debug -DCRYPTO=openssl -DX509_IGNORE_CRITICAL=ON ..
```

```
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Release -DCRYPTO=openssl -DX509_IGNORE_CRITICAL=ON ..
```

