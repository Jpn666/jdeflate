#!/usr/bin/env python
import sys
import subprocess


def getversion():
    def runcommand(cmd):
        output = "0.0.0"
        try:
            p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
            output, e = p.communicate()
            if p.returncode != 0:
                raise ValueError
        except (FileNotFoundError, ValueError):
            return "0.0.0"
        return output.decode('utf-8').strip()

    v = runcommand(["git", "describe", "--tags"])
    if v.startswith("v"):
        v = v[1:]
    v = v.strip()

    m = v.split("-", 1)
    v = m[0]

    s = v.split(".")
    try:
        int(s[0])
        int(s[1])
        int(s[2])
    except (ValueError, IndexError):
        return "0.0.0"
    return v


if __name__ == "__main__":
    print(getversion())
