apt reinstall docker-ce
docker start janus_dev
docker exec janus_dev bash /etc/rcS.d/S99local
# sudo docker start gatewayworker
# sudo docker start gateway-1
# sudo docker start cppbuilder 
