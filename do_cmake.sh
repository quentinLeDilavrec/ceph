#!/bin/bash -x
git submodule update --init --recursive
if test -e build; then
    echo 'build dir already exists; rm -rf build and re-run'
    exit 1
fi

PYBUILD="2"
source /etc/os-release
case "$ID" in
    fedora)
        if [ "$VERSION_ID" -ge "29" ] ; then
            PYBUILD="3"
        fi
        ;;
    rhel|centos)
        MAJOR_VER=$(echo "$VERSION_ID" | sed -e 's/\..*$//')
        if [ "$MAJOR_VER" -ge "8" ] ; then
            PYBUILD="3"
        fi
        ;;
    opensuse*|suse|sles)
        PYBUILD="3"
        ;;
esac
if [ "$PYBUILD" = "3" ] ; then
    ARGS="$ARGS -DWITH_PYTHON2=OFF -DWITH_PYTHON3=ON -DMGR_PYTHON_VERSION=3"
fi

if type ccache > /dev/null 2>&1 ; then
    echo "enabling ccache"
    ARGS="$ARGS -DWITH_CCACHE=ON"
fi

# Build SimGrid
if [ ! -d simgrid ]; then
    wget https://github.com/simgrid/simgrid/archive/97b4fd8e435a44171d471a245142e6fd0eb992b2.tar.gz
	tar xf 97b4fd8e435a44171d471a245142e6fd0eb992b2.tar.gz simgrid
fi
cd simgrid
mkdir -p build && cd build
cmake -DCMAKE_INSTALL_PREFIX=$PWD/../simgrid-install ..
source ../BuildSimGrid.sh
make -j$(nproc) && make install
cd ..

# Build Remote SimGrid
if [ ! -d remote-simgrid ]; then
    git clone https://framagit.org/simgrid/remote-simgrid.git remote-simgrid
fi
cd remote-simgrid
mkdir -p build && cd build
cmake -DSimGrid_INCLUDE_DIR=$PWD/../simgrid-install/include -DSimGrid_LIBRARY=$PWD/../simgrid-#install/lib/libsimgrid.so -DCMAKE_INSTALL_PREFIX=$PWD../rsg-install ..
make -j$(nproc) && make install
cd ..

mkdir build && cd build
if type cmake3 > /dev/null 2>&1 ; then
    CMAKE=cmake3
else
    CMAKE=cmake
fi
${CMAKE} -DWITH_SYSTEM_BOOST=OFF -DWITH_PYTHON3=ON -WITH_PYTHON2=OFF -DCMAKE_BUILD_TYPE=Debug $ARGS "$@" .. || exit 1

# minimal config to find plugins
cat <<EOF > ceph.conf
plugin dir = lib
erasure code dir = lib
EOF

echo done.
cat <<EOF

****
WARNING: do_cmake.sh now creates debug builds by default. Performance
may be severely affected. Please use -DCMAKE_BUILD_TYPE=RelWithDebInfo
if a performance sensitive build is required.
****
EOF
