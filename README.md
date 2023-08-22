# sispop-storage-server
Storage server for Sispop Service Nodes

Requirements:
* Boost >= 1.66 (for boost.beast)
* OpenSSL >= 1.1.1a (for X25519 curves)
* sodium >= 1.0.17 (for ed25119 to curve25519 conversion)

You can, however, download and build static versions these dependencies
as part of the build by adding the `-DBUILD_STATIC_DEPS=ON` option to cmake.

Can use `RelWithDebInfo` instead of `Release` if you want to include debug symbols to provide developers with valueable core dumps from crashes.
Also make sure you don't have an older (than 4.3.0) libzmq header in /usr/local/include, if so please install a new version.
```
git submodule update --init --recursive
mkdir build && cd build
cmake -DDISABLE_SNODE_SIGNATURE=OFF -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
./sispop-storage 0.0.0.0 8080
```

The paths for Boost and OpenSSL can be specified by exporting the variables in the terminal before running `make`:
```
export OPENSSL_ROOT_DIR = ...
export BOOST_ROOT= ...
```

Then using something like Postman (https://www.getpostman.com/) you can hit the API:

# post data
```
HTTP POST http://127.0.0.1/store
body: "hello world"
headers:
- X-Sispop-recipient: "mypubkey"
- X-Sispop-ttl: "86400"
- X-Sispop-timestamp: "1540860811000"
- X-Sispop-pow-nonce: "xxxx..."
```
# get data
```
HTTP GET http://127.0.0.1/retrieve
headers:
- X-Sispop-recipient: "mypubkey"
- X-Sispop-last-hash: "" (optional)
```

# unit tests
```
mkdir build_test
cd build_test
cmake ../unit_test -DBOOST_ROOT="path to boost" -DOPENSSL_ROOT_DIR="path to openssl"
cmake --build .
./Test --log_level=all
```
