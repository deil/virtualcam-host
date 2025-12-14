#!/bin/bash
set -e
echo "Unloading v4l2loopback..."
sudo modprobe -r v4l2loopback || echo "Module not loaded, continuing..."
echo "Loading with exclusive_caps..."
sudo modprobe v4l2loopback devices=1 video_nr=10 card_label="Virtual Camera" exclusive_caps=Y
echo "Verifying..."
cat /sys/module/v4l2loopback/parameters/exclusive_caps
echo "Done: /dev/video10"
