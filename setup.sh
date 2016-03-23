#!/bin/bash

if (($EUID != 0)); then
  if [[ -t 1 ]]; then
    sudo "$0" "$@"
  else
    exec 1>output_file
    gksu "$0 $@"
  fi
  exit
fi

echo "apt-get programs"

sudo apt-get upgrade
sudo apt-get install -y bluez pulseaudio-module-bluetooth python-gobject python-gobject-2 bluez-tools sox libsox-fmt-all


cd /home/pi; mkdir fm; cd fm
wget https://raw.githubusercontent.com/novaspirit/bluetooth_fm_pi/master/pi2fm.c

echo "compiling pi2fm"

gcc -lm -std=c99 pi2fm.c -o pi2fm

echo "adding permissions to pulseaudio"
usermod -a -G lp pi

echo "adding attributes to configuration files"

echo 'KERNEL=="input[0-9]*", RUN+="/usr/lib/udev/bluetooth"' >> /etc/udev/rules.d/99-input.rules
echo "Enable=Source,Sink,Media,Socket" >>/etc/bluetooth/audio.conf
echo "speex-float-3" >> /etc/pulse/daemon.conf
echo "resample-method = trivial" >> /etc/pulse/daemon.conf
echo "exit-idle-time = -1" >> /etc/pulse/daemon.conf

echo "copying file to appropriate locations"
mkdir -p /usr/lib/udev/
cp bluetooth /usr/lib/udev/
cp bluetooth-auto /usr/lib/udev/
cp bluetooth-agent /etc/init.d/

echo "setting permissions for files"
chmod 774 /usr/lib/udev/bluetooth

chmod 774 /usr/lib/udev/bluetooth-auto
touch /usr/lib/udev/bluetooth-trust

touch /home/pi/fm/silence
chmod 775 /etc/init.d/bluetooth-agent

echo "adding bluetooth-agent to boot process"
update-rc.d bluetooth-agent defaults
