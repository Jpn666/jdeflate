# Jdeflate

`Jdeflate` is a small stream-oriented compression library (intended to be used as a static library only) with a minimal API. It's not a complete replacement for a more mature and complete library like ZLib but certainly, it can cover some use cases of it.

It's divided into three parts, two of them are for raw deflate encoding and decoding (`deflator.h` and `inflator.h`) and the third one is a high-level API to encode and decode zlib, gzip, and raw deflate streams (`zstrm.h`).

For examples and use you can check the following repository [jdeflate-test](https://github.com/Jpn666/jdeflate-test).


![GitHub License](https://img.shields.io/github/license/Jpn666/jdeflate)  ![GitHub tag (with filter)](https://img.shields.io/github/v/tag/Jpn666/jdeflate)