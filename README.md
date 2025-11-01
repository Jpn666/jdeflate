# Jdeflate

![GitHub tag (with filter)](https://img.shields.io/github/v/tag/Jpn666/jdeflate)  ![GitHub License](https://img.shields.io/github/license/Jpn666/jdeflate)

<p align="center">
    <img src="./.media/jdeflate-logo.svg" alt="logo" style="margin: 0.5em auto 2em auto; width: 340px; height: auto; display: block" >
</p>

---

`Jdeflate` is a compact, stream-focused compression library that offers a simplified interface. It compresses and decompresses data in a stream format, facilitating efficient handling of large datasets. It doesn't replace the comprehensive capabilities of more established libraries such as **zLib**. Nonetheless, this library has the potential to satisfy certain use cases typically addressed by similar libraries while maintaining a clean API and predictable performance.

---

## Table of Contents

- [Features](#features)
- [Basic Usage](#basic-usage)
  - [Including Headers](#including-headers)
  - [Initialization](#initialization)
  - [Compressing Data](#compressing-data)
  - [Decompressing Data](#decompressing-data)
  - [Simplified Buffer Mode](#simplified-buffer-mode)
  - [Cleaning Up](#cleaning-up)
- [ZStrm Usage](#zstrm-usage)
  - [Initialization](#initialization-1)
  - [Callbacks](#callbacks)
  - [Compression and Decompression](#compression-and-decompression)
  - [Flushing and Cleanup](#flushing-and-cleanup)
- [Building](#building)
  - [Build Steps](#build-steps)
- [License](#license)

---

## Features

- Stream-oriented API (suitable for large data)
- Written in C (C99)
- Portable (works on any platform with a C compiler)
- Predictable memory allocation pattern
- Good performance
- **Cute logo**

---

## Basic Usage

Here's how you can use `Jdeflate` in your project.

### Including Headers

Each module is independent, so you can include only what you need.

```c
#include <jdeflate/deflator.h>
#include <jdeflate/inflator.h>
```

For C++:

```c
extern "C" {
    #include <jdeflate/deflator.h>
    #include <jdeflate/inflator.h>
}
```

### Initialization

#### Create a compressor

```c
TDeflator* deflator;
/* flags, compression level, and memory allocator */
deflator = deflator_create(0, 6, NULL);
if (deflator == NULL) {
    /*... handle the error */
}
```

#### Create a decompressor

```c
TInflator* inflator;
/* flags, and memory allocator */
inflator = inflator_create(0, NULL);
if (inflator == NULL) {
    /* ... handle the error */
}
```

### Compressing Data

```c
unsigned int status;
bool final;

final = 0;
do {
    /* ...read or fetch some data into the source buffer */
    deflator_setsrc(deflator, source, sourcesize);
    if (/*...no more input*/) {
        final = 1;
    }

    do {
        deflator_settgt(deflator, target, targetsize);

        status = deflator_deflate(deflator, final);
        /* the target buffer now contains the compressed data */
        /* and to get the number of bytes that have been written */
        /* you can use deflator_tgtend(deflator) */
    } while (status == DEFLT_TGTEXHSTD);
} while (status == DEFLT_SRCEXHSTD);

/* check for errors */
if (status == DEFLT_OK) {
    /* ... compression was successful */
} else {
    /* ... handle the error */
}
```

### Decompressing Data

```c
unsigned int status;
bool final;

final = 0;
do {
    /* ...read or fetch some data into the source buffer */
    inflator_setsrc(inflator, source, sourcesize);
    if (/*...no more input*/) {
        final = 1;
    }

    do {
        inflator_settgt(inflator, target, targetsize);

        status = inflator_inflate(inflator, final);
        /* target buffer now contains the decompressed data */ 
        /* and to get the number of bytes that have been written */
        /* you can use inflator_tgtend(inflator) */
    } while (status == INFLT_TGTEXHSTD);
} while (status == INFLT_SRCEXHSTD);

/* check for errors */
if (status == INFLT_OK) {
    /* ... decompression was successful */
} else {
    /* ... handle the error */
}
```

### Simplified Buffer Mode

For simple operations, you can compress or decompress directly into a buffer without nested loops.

#### Compression

```c
deflator_setsrc(deflator, source, sourcesize);
deflator_settgt(deflator, target, targetsize);
if (deflator_deflate(deflator, 1) != DEFLT_OK) {
    /* ... handle the error */
}
```

#### Decompression

```c
inflator_setsrc(inflator, source, sourcesize);
inflator_settgt(inflator, target, targetsize);
if (inflator_inflate(inflator, 1) != INFLT_OK) {
    /* ... handle the error */
}
```

### Cleaning Up

Finally, it's important to properly manage and free the allocated memory. After you're done compressing or decompressing data, don't forget to destroy the compressor or decompressor objects.

```c
deflator_destroy(deflator);
inflator_destroy(inflator);
```

This ensures that all resources are correctly released and prevents potential memory leaks in your application.

---

## ZStrm Usage

`zstrm` is the high-level API of `Jdeflate`.

It provides a simple interface to handle zlib, gzip, and raw deflate streams using the lower-level functionalities from `deflator.h` and `inflator.h`.

This module simplifies tasks like reading and writing compressed data directly to files or network streams in a non-obstructive way, using a callback.

### Including the Header

```c
#include <jdeflate/zstrm.h>
```

For C++:

```c
extern "C" {
    #include <jdeflate/zstrm.h>
}
```

### Initialization

Initialize the object depending on your requirements, to compress you need to set the flag for the stream type, for decompression you may set it but it's not required. It can be any of:

- `ZSTRM_DFLT` —  raw deflate encoding
- `ZSTRM_ZLIB`  — zlib format
- `ZSTRM_GZIP`  — gzip format

#### Compression Example

```c
const TZStrm* zstrm;

/* creates a GZIP compressor using the compression level 9 */
zstrm = zstrm_create(ZSTRM_DEFLATE | ZSTRM_GZIP, 9, NULL);
if (zstrm == NULL) {
    /* ...handle error */
}
```

#### Decompression Example

Here the stream type is determined from the input but you can restrict what formats it accepts by setting any of the flags mentioned above.

```c
const TZStrm* zstrm;

/* creates a decompressor for any format */
zstrm = zstrm_create(ZSTRM_INFLATE, 0, NULL);
if (zstrm == NULL) {
    /* ...handle error */
}
```

### Callbacks

Next you need to set up the source or target callbacks. These functions will be called by `zstrm` to handle the input and output data. 

```c
/* target function */
intxx targetcallback(const uint8* buffer, uintxx size, void* userpayload);

/* source function */
intxx sourcecallback(uint8* buffer, uintxx size, void* userpayload);
```

You only need to set one.

- **Source callback** (`ZSTRM_INFLATE`) reads data and returns bytes read.
- **Target callback** (`ZSTRM_DEFLATE`) writes data and returns bytes written.
- Both must return a negative value on error.

Alternatively, for decompression (`ZSTRM_INFLATE`) instead of using the callback function you can set a buffer to directly decompress data from.

To compress, you need to use the following:

```c
zstrm_settargetfn(zstrm, targetcallback, userpayload);
```

And to decompress:

```c
zstrm_setsourcefn(zstrm, sourcecallback, userpayload);
```

Or

```c
zstrm_setsource(zstrm, buffer, buffersize);
```

### Compression and Decompression

#### Compression

```c
zstrm_deflate(zstrm, source, sourcesize);
if (zstrm->error) {
    /* ... handle errors */
}
```

#### Decompression

```c
uintxx total;
total = zstrm_inflate(zstrm, target, targetsize);
if (zstrm->error) {
    /* ... handle errors */
}
```

Remember to check the status after each operation and handle any errors accordingly. 

### Flushing and Cleanup

To terminate the compression stream you need to use the flush function. This will emit any pending data and finalize the compression process:

```c
zstrm_flush(zstrm, 1);
if (zstrm->error) {
    /* ... handle errors */
}
```

Finally, after finishing your operations with `zstrm`, don't forget to destroy it to free up resources.

```c
zstrm_destroy(zstrm);
```

For complete examples and usage you can check the following repository [jdeflate-test](https://github.com/Jpn666/jdeflate-test).

---

## Building

This project uses the Meson build system. To build the library, follow these steps:

### Prerequisites

- A C99-compliant compiler
- Python 3.7 or newer
- Meson build system (0.55.0 or newer)
- Ninja build system

### Installing Build Tools

On most Unix-like systems, you can install the required build tools using:

```bash
pip3 install meson ninja
```

For other platforms, please refer to the [Meson installation guide](https://mesonbuild.com/Getting-meson.html).

### Build Steps

1. Configure the build:
   ```bash
   meson setup builddir
   ```

2. Build the project:
   ```bash
   meson compile -C builddir
   ```

3. (Optional) Install the library:
   ```bash
   meson install -C builddir
   ```

### Build Options

You can configure build options using `meson configure`. Common options include:

- `--default-library=static|shared` - Build static or shared library (default: shared)
- `--buildtype=plain|debug|debugoptimized|release` - Set build type (default: debug)

Example:
```bash
meson configure builddir --default-library=static --buildtype=release
```

### Build Output

After a successful build, you'll find:
- Static library: `builddir/libjdeflate.a`
- Shared library: `builddir/libjdeflate.so` (on Linux)

---

## Integration

### Windows DLL Support

When using Jdeflate as a shared library (DLL) on Windows (`MSVC`), you need to define the `JDEFLATE_DLL` macro.

This macro ensures proper import of functions from the DLL.

---

## License

This project is licensed under the **Apache License 2.0**.  
See the [LICENSE](LICENSE) file for details.
