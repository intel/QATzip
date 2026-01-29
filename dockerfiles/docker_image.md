# Introduction
QATzip is a user-space library built on top of the Intel® QuickAssist Technology (QAT) user-space library. It provides extended compression and decompression capabilities by offloading these operations to Intel® QAT Accelerators.

# Docker Image
- intel/intel-qatzip:<version>

# How to Use This Image

## BIOS Setting
- Intel VT-d and SR-IOV must be enabled in the platform (BIOS).
For more details, refer to the link [Updating the BIOS settings](https://intel.github.io/quickassist/AppNotes/Containers/setup.html#updating-the-bios-settings).

## Grub Settings
- Intel IOMMU should be enabled and QAT VF to be bind to kernel. Follow the below steps.

### Ubuntu:
- Edit the grub file: `sudo vim /etc/default/grub`
- Modify `GRUB_CMDLINE_LINUX` to add `intel_iommu=on vfio-pci.ids=8086:4941` (SPR)
- Update grub: `sudo update-grub`
- Reboot the system

### CentOS/Fedora:
- Update the kernel: `grubby --update-kernel=/boot/vmlinuz-$(uname -r) --remove-args="intel_iommu=on" --args="intel_iommu=off vfio-pci.ids=8086:4941"`
- Reboot the system

- Ensure VT-d and SRIOV are enabled in the host. The steps can be referred from the link [Configuring the host operating system with SR-IOV IOMMU](https://intel.github.io/quickassist/AppNotes/Containers/setup.html#configuring-the-host-operating-system-with-sr-iov-iommu).

## Docker Environment Setup
- The environment must be set up to build and run containers. Certain packages will need to be loaded and configuration files created and/or updated, which can be system specific.
- Install Docker following the steps in [Docker Package Installation](https://intel.github.io/quickassist/AppNotes/Containers/setup.html#docker).
- Configure the proxy setting following [Proxy settings](https://intel.github.io/quickassist/AppNotes/Containers/setup.html#proxy-settings).

## Enable Intel® QAT Virtual Function (VF) Devices
- Enable the VF devices using the following script:
  ```sh
  for i in `(lspci -D -d :4940 || lspci -D -d :4942) | awk '{print $1}'`; do echo 16 | sudo tee /sys/bus/pci/devices/$i/sriov_numvfs; done
  ```

## Configure Services in the Host
- Stop the Intree or OOT services if any running on the host:
  ```sh
  systemctl stop qat
  ```
  ```sh
  systemctl stop qat_service
  ```
  ```sh
  systemctl disable qat_service
  ```
- Bring the QAT device down:
  ```sh
  for i in `lspci -D -d :4940 | awk '{print $1}'`; do echo down > /sys/bus/pci/devices/$i/qat/state; done
  ```
- Set the correct service in the QAT devices:
  ```sh
  for i in `lspci -D -d :4940 | awk '{print $1}'`; do echo "dc" > /sys/bus/pci/devices/$i/qat/cfg_services; done
  ```
- Bring the QAT device up:
  ```sh
  for i in `lspci -D -d :4940 | awk '{print $1}'`; do echo up > /sys/bus/pci/devices/$i/qat/state; done
  ```
- Check the status:
  ```sh
  for i in `lspci -D -d :4940 | awk '{print $1}'`; do cat /sys/bus/pci/devices/$i/qat/state; done
  ```
- Ensure the GID using the below command in host. The GID of the QAT group on the host should be same as GID of the docker Image. The QAT group of the host should be ‘1001’ :
  ```sh
  getent group qat | cut -d ':' -f 3
  ```
- Enable the VF devices with the previously mentioned script and set correct permissions:
  ```sh
  chown root:qat /dev/vfio/*
  ```
  ```sh
  chmod 660 /dev/vfio/*
  ```

## Build Image
To build the Docker image, use the following command:

```sh
docker build --build-arg GID=$(getent group qat | cut -d ':' -f 3) -t <docker_image_name> <Dockerfile_path>
```

## Run Image
To run the Docker image, use the following command:
```sh

Compress:
docker run --rm -it --cap-add=IPC_LOCK --security-opt seccomp=unconfined --security-opt apparmor=unconfined $(for i in `ls /dev/vfio/*`; do echo --device $i; done) --env QAT_POLICY=1 --ulimit memlock=50000000:50000000 <qatzip-docker-image> qzip -k calgary  -O gzipext -A deflate

Decompress:
docker run --rm -it --cap-add=IPC_LOCK --security-opt seccomp=unconfined --security-opt apparmor=unconfined $(for i in `ls /dev/vfio/*`; do echo --device $i; done) --env QAT_POLICY=1 --ulimit memlock=50000000:50000000 <qatzip-docker-image> qzip -d calgary.gz -A deflate

```

NOTE: "Warning, users who have access to the docker group also have root access"

## Test Commands
```sh

Compress:
docker run --rm -it --cap-add=IPC_LOCK --security-opt seccomp=unconfined --security-opt apparmor=unconfined $(for i in `ls /dev/vfio/*`; do echo --device $i; done) --env QAT_POLICY=1 --ulimit memlock=50000000:50000000 -v /opt/opt --memory=8g -w $PWD  <qatzip-docker-image> qzip -k calgary  -O gzipext -A deflate


Decompress:
docker run --rm -it --cap-add=IPC_LOCK --security-opt seccomp=unconfined --security-opt apparmor=unconfined $(for i in `ls /dev/vfio/*`; do echo --device $i; done) --env QAT_POLICY=1 --ulimit memlock=50000000:50000000 -v /opt/opt --memory=8g -w $PWD  <qatzip-docker-image> qzip -d calgary.gz -A deflate

```

## Legal
Intel, Intel Atom, and Xeon are trademarks of Intel Corporation in the U.S. and/or other countries.

\*Other names and brands may be claimed as the property of others.

Copyright © 2016-2026, Intel Corporation. All rights reserved.
