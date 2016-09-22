#!/bin/sh

cd ..
git remote add upstream https://github.com/freeminer/freeminer.git
git fetch --all
git pull
( git merge --no-edit upstream/master | grep -i "conflict" ) && exit
git push
git submodule update --init --recursive
git status


git submodule add https://github.com/sctplab/usrsctp.git src/external/usrsctp
git submodule add https://github.com/proller/android-ifaddrs.git build/android_fm/jni/android-ifaddrs

cd src/external/usrsctp
git co master
git pull
cd ../../..
git commit src/external/usrsctp -m "update submodules"
git push
