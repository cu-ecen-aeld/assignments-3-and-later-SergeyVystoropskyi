#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.
 
set -e
set -u
 
OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath "$(dirname "$0")")
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
 
# ----------------------------------------------------------------------
#  a. Argument handling: outdir (default /tmp/aeld)
# ----------------------------------------------------------------------
if [ $# -lt 1 ]
then
    echo "Using default directory ${OUTDIR} for output"
else
    OUTDIR=$1
    echo "Using passed directory ${OUTDIR} for output"
fi
 
# Make OUTDIR absolute
OUTDIR=$(realpath "${OUTDIR}")
 
# ----------------------------------------------------------------------
#  b. Create outdir (fail if cannot be created)
# ----------------------------------------------------------------------
mkdir -p "${OUTDIR}"
 
cd "${OUTDIR}"
 
echo "Output directory: ${OUTDIR}"
 
# ----------------------------------------------------------------------
#  c. Build kernel
#     - Clone if missing
#     - Checkout specified version
#     - Build Image, dtbs, etc.
# ----------------------------------------------------------------------
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
    git clone "${KERNEL_REPO}" --depth 1 --single-branch --branch "${KERNEL_VERSION}" linux-stable
fi
 
if [ ! -e "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" ]; then
    cd "${OUTDIR}/linux-stable"
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout "${KERNEL_VERSION}"
 
    echo "Cleaning kernel build tree"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
 
    echo "Configuring kernel (defconfig)"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
 
    echo "Building kernel Image and related targets"
    make -j"$(nproc)" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi
 
echo "Adding the Image in outdir"
cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}/"
 
# ----------------------------------------------------------------------
#  e. Build root filesystem in ${OUTDIR}/rootfs
# ----------------------------------------------------------------------
echo "Creating the staging directory for the root filesystem"
 
cd "${OUTDIR}"
 
if [ -d "${OUTDIR}/rootfs" ]
then
    echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm -rf "${OUTDIR}/rootfs"
fi
 
# ----------------------------------------------------------------------
#  e.i. Create necessary base directories
# ----------------------------------------------------------------------
mkdir -p "${OUTDIR}/rootfs"
 
cd "${OUTDIR}/rootfs"
 
# Basic rootfs structure
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/sbin usr/lib
mkdir -p var/log
 
# ----------------------------------------------------------------------
#  BusyBox
# ----------------------------------------------------------------------
cd "${OUTDIR}"
 
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout "${BUSYBOX_VERSION}"
 
    # Configure busybox for target
    make distclean
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
else
    cd busybox
fi
 
# Build and install busybox into rootfs
echo "Building busybox"
make -j"$(nproc)" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
 
echo "Installing busybox to rootfs"
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX="${OUTDIR}/rootfs" install
 
echo "Library dependencies"
${CROSS_COMPILE}readelf -a "${OUTDIR}/rootfs/bin/busybox" | grep "program interpreter" || true
${CROSS_COMPILE}readelf -a "${OUTDIR}/rootfs/bin/busybox" | grep "Shared library" || true
 
# ----------------------------------------------------------------------
#  e.i (continued): Add library dependencies to rootfs
# ----------------------------------------------------------------------
echo "Adding shared library dependencies to rootfs"
 
SYSROOT="$(${CROSS_COMPILE}gcc -print-sysroot)"
 
# Program interpreter (ld-linux)
cp -a "${SYSROOT}/lib/ld-linux-aarch64.so.1" "${OUTDIR}/rootfs/lib/"
 
# Shared libraries used by busybox (typical set)
cp -a "${SYSROOT}/lib64/libm.so.6"        "${OUTDIR}/rootfs/lib64/"
cp -a "${SYSROOT}/lib64/libresolv.so.2"   "${OUTDIR}/rootfs/lib64/"
cp -a "${SYSROOT}/lib64/libc.so.6"        "${OUTDIR}/rootfs/lib64/"
 
# ----------------------------------------------------------------------
#  e.i: Make device nodes
# ----------------------------------------------------------------------
echo "Creating device nodes"
 
cd "${OUTDIR}/rootfs"
 
sudo mknod -m 666 dev/null c 1 3 || true
sudo mknod -m 622 dev/console c 5 1 || true
 
# ----------------------------------------------------------------------
#  e.ii: Clean and build the writer application (cross-compiled)
# ----------------------------------------------------------------------
echo "Building writer application (cross-compiled)"
 
cd "${FINDER_APP_DIR}"
 
# If you have a Makefile supporting CROSS_COMPILE, use it:
if [ -f Makefile ]; then
    make clean || true
    make CROSS_COMPILE=${CROSS_COMPILE}
else
    # Fallback: compile directly
    ${CROSS_COMPILE}gcc -Wall -Wextra -g -o writer writer.c
fi
 
# Copy writer app into rootfs /home
cp "${FINDER_APP_DIR}/writer" "${OUTDIR}/rootfs/home/"
 
# ----------------------------------------------------------------------
#  f. Copy finder scripts and conf files into rootfs/home
# ----------------------------------------------------------------------
echo "Copying finder scripts and configuration into rootfs/home"
 
mkdir -p "${OUTDIR}/rootfs/home/conf"
 
# finder.sh
cp "${FINDER_APP_DIR}/finder.sh" "${OUTDIR}/rootfs/home/"
 
# finder-test.sh (then patch path)
cp "${FINDER_APP_DIR}/finder-test.sh" "${OUTDIR}/rootfs/home/"
sed -i 's|\.\./conf/assignment.txt|conf/assignment.txt|' "${OUTDIR}/rootfs/home/finder-test.sh"
 
# conf files from top-level conf directory (../conf)
cp "${FINDER_APP_DIR}/../conf/username.txt"   "${OUTDIR}/rootfs/home/conf/"
cp "${FINDER_APP_DIR}/../conf/assignment.txt" "${OUTDIR}/rootfs/home/conf/"
 
# ----------------------------------------------------------------------
#  g. Copy autorun-qemu.sh into rootfs/home
# ----------------------------------------------------------------------
echo "Copying autorun-qemu.sh into rootfs/home"
 
cp "${FINDER_APP_DIR}/autorun-qemu.sh" "${OUTDIR}/rootfs/home/"
 
# Make sure everything we expect to run is executable
chmod +x "${OUTDIR}/rootfs/home/"finder.sh
chmod +x "${OUTDIR}/rootfs/home/"finder-test.sh
chmod +x "${OUTDIR}/rootfs/home/"autorun-qemu.sh
chmod +x "${OUTDIR}/rootfs/home/"writer
 
# ----------------------------------------------------------------------
#  h. Chown rootfs to root:root
# ----------------------------------------------------------------------
echo "Setting ownership of rootfs to root:root"
 
cd "${OUTDIR}/rootfs"
sudo chown -R root:root .
 
# ----------------------------------------------------------------------
#  h. Create initramfs.cpio.gz from rootfs
# ----------------------------------------------------------------------
echo "Creating initramfs.cpio.gz"
 
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > "${OUTDIR}/initramfs.cpio"
cd "${OUTDIR}"
gzip -f initramfs.cpio
 
echo "Done."
echo "Kernel Image : ${OUTDIR}/Image"
echo "Initramfs    : ${OUTDIR}/initramfs.cpio.gz"
