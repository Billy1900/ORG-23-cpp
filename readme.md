# 1. Environment requirement
## 1.1 Install cmake and boost
```shell
# install boost
bash boost_install.sh

# install cmake
# Linux
sudo apt-get install cmake
cmake --version
# MAC
brew install cmake
cmake --version
```
**Note: you can directly use** `bash build.sh` **after install cmake and boost.**
## 1.2 Compile
To compile an autotrader:
```shell
cmake -DCMAKE_BUILD_TYPE=Debug -B build
cmake --build build --config Debug
```
Replace "Debug" with "Release" in the above to build with CMake's 'Release' build configuration.

**Note:** Your autotrader will be built using the 'Release' build configuration for the competition.

## 1.3 Python venv
```shell
python3 -m venv venv
source venv/bin/activate
pip3 install PySide6
```

# 2. Run demo
On macOS and Linux, you can use:
```shell
cp build/autotrader .
```

To run a Ready Trader Go match with one or more autotraders, simply run:
```shell
python3 rtg.py run [AUTOTRADER FILENAME [AUTOTRADER FILENAME]]
```

For example:
```shell
python3 rtg.py run autotrader
```
