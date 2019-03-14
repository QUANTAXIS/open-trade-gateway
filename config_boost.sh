ECHO "install boost 1.68.0" 
apt update -y && apt install gcc g++  automake autoconf libtool make -y 


cd ~

wget https://dl.bintray.com/boostorg/release/1.68.0/source/boost_1_68_0.tar.gz && tar -zxvf boost_1_68_0.tar.gz

cd boost_1_68_0 && ./bootstrap.sh && ./b2 && ./b2 install && ldconfig



cd ~
wget https://www.openssl.org/source/openssl-1.0.2.tar.gz && tar zxvf openssl-1.0.2.tar.gz
cd ~/openssl-1.0.2 && ./config && make && make install
