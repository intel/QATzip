# Intel® QuickAssist Technology(QAT) QATZip\* Container support

QATZip Dockerfiles contains QAT Compression Base (qatzip and zst with zstdplugin) which can be built into docker images on the platforms
with [Intel® QuickAssist 4xxx Series](https://www.intel.com/content/www/us/en/products/details/processors/xeon/scalable.html)
QAT device.

This Dockerfile (QAT Compression Base) is build and tested with software versions mentioned in [software_requirements] (../README.md#software-requirements)section.

## Docker setup and testing

Refer [here](https://intel.github.io/quickassist/AppNotes/Containers/setup.html)
for setting up the host for QAT_HW (qatlib intree) if the platform has QAT 4xxx Hardware
device. Stop QAT service if any running on the host.

### QAT_HW settings
Follow the below steps to enable required service. The service should be dc alone for compression.
in step 2 depending on the particular use case. Configure the required service only to get best performance.

1. Bring down the QAT devices
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo down > /sys/bus/pci/devices/$i/qat/state;done
```

2. Set up the required crypto service(s)
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo “dc“ > /sys/bus/pci/devices/$i/qat/cfg_services;done
```

3. Bring up the QAT devices
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo up> /sys/bus/pci/devices/$i/qat/state;done
```

4. Check the status of the QAT devices
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do cat /sys/bus/pci/devices/$i/qat/state;done
```

5. Enable VF for the PF in the host
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo 16|sudo tee /sys/bus/pci/devices/$i/sriov_numvfs; done
```

6. Add QAT group and Permission to the VF devices in the host
```
    chown root.qat /dev/vfio/*
    chmod 660 /dev/vfio/*
```

### Image creation

Docker images can be build using the below command with appropiate image name.

```
docker build --build-arg GID=$(getent group qat | cut -d ':' -f 3) -t <docker_image_name> <path-to-dockerfile> --no-cache
```
Note: GID is the group id of qat group in the host.

### Testing QAT Compression base using qzip\* utility

##Compression using 7z

```
docker run -it --cap-add=IPC_LOCK --security-opt seccomp=unconfined --security-opt apparmor=unconfined $(for i in `ls /dev/vfio/*`; do echo --device $i; done)  --env QAT_POLICY=1 --ulimit memlock=524288000:524288000 <docker_image_name>  qzip -k $your_input_file  -O gzipext -A deflate
```

##Decompress using 7z

```
docker run -it --cap-add=IPC_LOCK --security-opt seccomp=unconfined --security-opt apparmor=unconfined $(for i in `ls /dev/vfio/*`; do echo --device $i; done)  --env QAT_POLICY=1 --ulimit memlock=524288000:524288000 <docker_image_name> qzip -d $your_input_file.7z
```

NOTE: "Warning, users who have access to the docker group also have root access"
