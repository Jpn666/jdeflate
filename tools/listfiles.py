#!/usr/bin/env python3
import sys
import shutil
import os


def eachfile(folder, recursive=False):
    for filename in os.listdir(folder):
        path = os.path.join(folder, filename)
        if os.path.isfile(path):
            yield path
        if os.path.isdir(path):
            if recursive:
                yield from eachfile(path, recursive)


def listfiles(folder, ext):
    for f in eachfile(folder):
        if f.endswith(ext):
            print(f)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise Exception("Error: invalid arguments")

    listfiles(sys.argv[1], sys.argv[2])
