Hardware Composer for Android* OS
*********************************

Introduction
=============

The Hardware Composer for Android* OS provides a highly optimized implementation of the
Android* Hardware Composer HAL.

License
========

The Hardware Composer for Android* OS is distributed under the Apache 2.0 License.

You may obtain a copy of the License at:

http://www.apache.org/licenses/LICENSE-2.0

Building
========

The Hardware Composer for Android* OS is first of many components for the Broxton platform
to be released to Open Source. As a result, the components needed to build are not yet
distributed.  The following steps support a generic in-tree build for the Hardware Composer
for Android* OS:

Download sources for the AOSP project: 
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

repo init -u https://android.googlesource.com/platform/manifest

repo sync

source build/envsetup.sh

lunch aosp_x86_64-eng

Clone the Hardware Composer for Android* OS into the generic AOSP source tree.
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

git clone https://github.com/intel/hwc.git hardware/intel/hwc

cd hardware/intel/hwc/build

mm

Install
^^^^^^^
**Note**: there are prebuilt libraries in the source tree for compilation purposes only.  Do not deploy those files to your Broxton system. The prebuilt libraries and their headers are distributed under their respective licenses and may or may not be considered as **Open Source**.


With a running Broxton system do the following:

adb push out/target/product/generic_x86_64/system/vendor/lib64/hw/hwcomposer..so /vendor/lib64/hw/hwcomposer.gmin.so  

Supported Platforms
-------------------

The Hardware Composer for Android* OS supports Broxton platforms running Android N and above.

(*) Other names and brands my be claimed as property of others.
---------------------------------------------------------------

