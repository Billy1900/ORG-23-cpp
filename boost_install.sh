wget https://boostorg.jfrog.io/artifactory/main/release/1.81.0/source/boost_1_81_0.tar.gz
tar -xvf boost_1_81_0.tar.gz
rm boost_1_81_0.tar.gz
cd boost_1_81_0
./bootstrap.sh
./b2 headers
cpuCores=`cat /proc/cpuinfo | grep "cpu cores" | uniq | awk '{print $NF}'`
sudo ./b2 --with=all -j $cpuCores install
cat /usr/local/include/boost/version.hpp | grep "BOOST_LIB_VERSION"