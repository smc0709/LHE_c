#!/bin/sh

#gnome-terminal -e "ffserver -f ffservermjpeg.conf"
#gnome-terminal -e "ffmpeg -f x11grab -s 1920x1080 -r 60 -i :0.0 http://localhost:8090/feed1.ffm"
#gnome-terminal -e "ffplay http://0.0.0.0:8090/live.mjpg"


#gnome-terminal -e "ffserver -f ffserver.conf"
#gnome-terminal -e "ffmpeg -f x11grab -s 1920x1080 -r 60 -i :0.0 http://localhost:8090/feed1.ffm"
#gnome-terminal -e "ffplay -pixel_format yuv420p http://0.0.0.0:8090/live.mlhe"

gnome-terminal -e "ffserver -f ffserver.conf"
gnome-terminal -e "ffmpeg -f video4linux2 -i /dev/video0 http://localhost:8090/feed1.ffm"
gnome-terminal -e "ffplay http://localhost:8090/live.mlhe"
