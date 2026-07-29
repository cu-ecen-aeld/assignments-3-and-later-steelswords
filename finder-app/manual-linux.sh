#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR="${1:-/tmp/aeld}"
# Canonicalize OUTDIR to handle relative paths
OUTDIR="$(realpath "$OUTDIR")"
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
HOSTFLAGS="-fcommon"

GITROOT=$(
    git rev-parse --show-toplevel
    if [[ $? -ne 0 ]]; then
        echo "This script is meant to be run inside the assignment-3 repo. Exiting with error."
        exit 5
    fi
)

NUM_JOBS="$(
    if (( $(nproc) < 2 )); then
        echo 1
    else
        echo $(( $(nproc) - 1 ))
    fi
)"
echo "Running with NUM_JOBS=$NUM_JOBS"

# TODO: How will the xtoolchain be configured in the Gitlab runner?

echo "Using directory ${OUTDIR} for output"

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    #make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" HOSTCFLAGS="$HOSTFLAGS" defconfig
    cp "$(realpath "$GITROOT")/kernel-lite-config" "$OUTDIR/linux-stable"
    make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" HOSTCFLAGS="$HOSTFLAGS" all -j "$NUM_JOBS"
    make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" HOSTCFLAGS="$HOSTFLAGS" modules -j "$NUM_JOBS"
    make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" HOSTCFLAGS="$HOSTFLAGS" dtbs -j "$NUM_JOBS"
fi

echo "Adding the Image in outdir"
install "${OUTDIR}/linux-stable/arch/$ARCH/boot/Image" "$OUTDIR"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf "${OUTDIR}/rootfs"
fi

# Create necessary base directories
declare -a base_dirs=(
    bin
    dev
    etc
    home
    opt
    proc
    root
    run
    sys
    tmp
    usr/bin
    usr/lib
    usr/lib64
    var
    # lib and lib64 not included since they are populated by copying from
    # sysroot, below.
)

echo "-> Creating rootfs dirs"
for dir in "${base_dirs[@]}"
do
    if ! mkdir -p "$OUTDIR/rootfs/$dir"; then
        echo "ERR: Could not create rootfs dir \"$dir\""
        exit 12
    fi
done

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
fi
cd busybox

echo "-> Replacing .config file in busybox dir"
cp "$GITROOT/busybox-config" "$OUTDIR/busybox/.config"


# Make and install busybox
echo "-> Building busybox..."
#make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" HOSTCFLAGS="$HOSTFLAGS" DESTDIR="$OUTDIR/rootfs" -j "$NUM_JOBS" distclean
#make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" HOSTCFLAGS="$HOSTFLAGS" DESTDIR="$OUTDIR/rootfs" -j "$NUM_JOBS" defconfig
make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" HOSTCFLAGS="$HOSTFLAGS" DESTDIR="$OUTDIR/rootfs" -j "$NUM_JOBS"
make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" HOSTCFLAGS="$HOSTFLAGS" CONFIG_PREFIX="$OUTDIR/rootfs" -j "$NUM_JOBS" install


cd "$OUTDIR/rootfs"

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# Add library dependencies to rootfs
echo "-> Populating rootfs/lib and rootfs/lib64 from toolchain sysroot"
SYSROOT="$( "$CROSS_COMPILE"gcc -print-sysroot )"
cp --archive "$SYSROOT/lib" "$OUTDIR/rootfs"
cp --archive "$SYSROOT/lib64" "$OUTDIR/rootfs"

# Make device nodes
cd "$OUTDIR/rootfs"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/console c 5 1

# TODO: Clean and build the writer utility
make -C "$GITROOT/finder-app" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" clean
make -C "$GITROOT/finder-app" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE"


# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
echo "-> Installing finder app files to rootfs"
install --mode +x -D \
    "$GITROOT/finder-app/finder.sh" \
    "$GITROOT/finder-app/writer.sh" \
    "$GITROOT/finder-app/writer" \
    "$GITROOT/finder-app/autorun-qemu.sh" \
    "$GITROOT/conf" \
    "$GITROOT/finder-app/finder-test.sh" \
    "$OUTDIR/rootfs/home"

# TODO: Chown the root directory
sudo chown -R root:root "$OUTDIR/rootfs"

# TODO: Create initramfs.cpio.gz
cd "$OUTDIR/rootfs"
echo "-> Creating initramfs.cpio"
find . | sudo cpio -H newc -ov --owner root:root > "$OUTDIR/initramfs.cpio"

cd "$OUTDIR"
echo "-> Compressing initramfs.cpio"
sudo gzip -f initramfs.cpio

echo "Done! "
