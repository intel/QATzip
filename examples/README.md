```
cd QATzip
./autogen.sh
./configure
make install -j
cd examples
make
```

```
./compress_lz4 calgary
./decompress_lz4 calgary.lz4
./compress_gzip calgary
./decompress_gzip calgary.gz
```