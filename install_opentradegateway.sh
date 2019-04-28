ECHO 'install open-trade-gatway'
apt update -y && apt install git gcc g++ libcurl4-openssl-dev automake autoconf libtool make -y 



ECHO "install boost 1.70.0" 



cd ~

wget https://dl.bintray.com/boostorg/release/1.70.0/source/boost_1_70_0.tar.gz && tar -zxvf boost_1_70_0.tar.gz

cd boost_1_70_0 && ./bootstrap.sh && ./b2 && ./b2 install && ldconfig



# cd ~
# wget https://www.openssl.org/source/openssl-1.0.2.tar.gz && tar zxvf openssl-1.0.2.tar.gz
# cd ~/openssl-1.0.2 && ./config && make && make install


#cd ~
#git clone https://github.com/shinnytech/open-trade-gateway & cd open-trade-gateway & make & make install
