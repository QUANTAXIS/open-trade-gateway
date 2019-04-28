ECHO 'install open-trade-gatway'
apt update -y && apt install git gcc g++ libcurl4-openssl-dev automake autoconf libtool make -y 

ECHO "install boost 1.70.0" 
cd ~
wget https://dl.bintray.com/boostorg/release/1.70.0/source/boost_1_70_0.tar.gz && tar -zxvf boost_1_70_0.tar.gz
cd boost_1_70_0 && ./bootstrap.sh && ./b2 && ./b2 install && ldconfig

ECHO "install openssl/crypto" 
cd ~
wget https://www.openssl.org/source/openssl-1.0.2.tar.gz && tar zxvf openssl-1.0.2.tar.gz
cd ~/openssl-1.0.2 && ./config && make && make install
mv /usr/bin/openssl  /usr/bin/openssl.old
mv /usr/include/openssl  /usr/include/openssl.old
ln -s /usr/local/ssl/bin/openssl  /usr/bin/openssl
ln -s /usr/local/ssl/include/openssl  /usr/include/openssl
ln -s /usr/local/ssl/lib/libssl.so /usr/local/lib/libssl.so
ln -s /usr/local/ssl/lib/libssl.a /usr/local/lib/libssl.a
ln -s /usr/local/ssl/lib/libcrypto.so /usr/local/lib/libcrypto.so
ln -s /usr/local/ssl/lib/libcrypto.a /usr/local/lib/libcrypto.a
echo "/usr/local/ssl/lib" >> /etc/ld.so.conf
ldconfig -v

#ECHO "CLONE OPEN-TRADE-GATEWAY and Install"
#cd ~
#git clone https://github.com/shinnytech/open-trade-gateway & cd open-trade-gateway & make & make install
